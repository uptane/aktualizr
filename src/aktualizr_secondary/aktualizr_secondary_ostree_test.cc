#include <gtest/gtest.h>

#include <ostree.h>
#include <boost/filesystem/string_file.hpp>

#include "boost/algorithm/string/trim.hpp"
#include "boost/process.hpp"

#include "logging/logging.h"
#include "test_utils.h"

#include "aktualizr_secondary_ostree.h"
#include "update_agent_ostree.h"
#include "uptane_repo.h"

class Treehub {
 public:
  explicit Treehub(const std::string& server_path)
      : port_(TestUtils::getFreePort()),
        url_("http://127.0.0.1:" + port_),
        process_(server_path, "-p", port_, "-d", root_dir_.PathString(), "-s0.5", "--create") {
    TestUtils::waitForServer(url() + "/");
    auto rev_process = Process("ostree").run({"rev-parse", "--repo", root_dir_.PathString(), "master"});
    EXPECT_EQ(std::get<0>(rev_process), 0) << std::get<2>(rev_process);
    cur_rev_ = std::get<1>(rev_process);
    boost::trim_right_if(cur_rev_, boost::is_any_of(" \t\r\n"));

    LOG_INFO << "Treehub is running on: " << port_ << " current revision: " << cur_rev_;
  }

  Treehub(const Treehub&) = delete;
  Treehub(Treehub&&) = delete;
  Treehub& operator=(const Treehub&) = delete;
  Treehub& operator=(Treehub&&) = delete;

  ~Treehub() {
    process_.terminate();
    process_.wait_for(std::chrono::seconds(10));
    if (process_.running()) {
      LOG_ERROR << "Failed to stop Treehub server";
    } else {
      LOG_INFO << "Treehub server has been stopped";
    }
  }

  [[nodiscard]] const std::string& url() const { return url_; }
  [[nodiscard]] const std::string& curRev() const { return cur_rev_; }

 private:
  TemporaryDirectory root_dir_;
  const std::string port_;
  const std::string url_;
  boost::process::child process_;
  std::string cur_rev_;
};

class OstreeRootfs {
 public:
  explicit OstreeRootfs(const std::string& rootfs_template) {
    auto sysroot_copy = Process("cp").run({"-r", rootfs_template, getPath().c_str()});
    EXPECT_EQ(std::get<0>(sysroot_copy), 0)
        << "stdout: " << std::get<1>(sysroot_copy) << " stderr:" << std::get<2>(sysroot_copy);
    resetDeployment();
  }

  [[nodiscard]] const boost::filesystem::path& getPath() const { return sysroot_dir_; }
  [[nodiscard]] const char* getDeploymentRev() const { return rev_.c_str(); }
  static int getDeploymentSerial() { return 0; }
  [[nodiscard]] const char* getOSName() const { return os_name_.c_str(); }

  [[nodiscard]] OstreeDeployment* getDeployment() const { return deployment_.get(); }
  void setNewDeploymentRev(const std::string& new_rev) { rev_ = new_rev; }

  void resetDeployment() {
    Process::Result deployment_rev;
    // When reset after the first time, this annoyingly returns an error code
    // despite apparently succeeding. Retry to be extra safe.
    for (int i = 0; i < 2; ++i) {
      deployment_rev = Process("ostree").run(
          {"rev-parse", std::string("--repo"), getPath().string() + "/ostree/repo", "generate-remote/generated"});
      if (std::get<0>(deployment_rev) == 0) {
        break;
      }
    }

    EXPECT_EQ(std::get<0>(deployment_rev), 0)
        << "stdout: " << std::get<1>(deployment_rev) << " stderr:" << std::get<2>(deployment_rev);

    rev_ = std::get<1>(deployment_rev);
    boost::trim_right_if(rev_, boost::is_any_of(" \t\r\n"));

    deployment_.reset(ostree_deployment_new(0, getOSName(), getDeploymentRev(), getDeploymentSerial(),
                                            getDeploymentRev(), getDeploymentSerial()));
  }

 private:
  struct OstreeDeploymentDeleter {
    void operator()(OstreeDeployment* e) const { g_object_unref(reinterpret_cast<gpointer>(e)); }
  };

  const std::string os_name_{"dummy-os"};
  TemporaryDirectory tmp_dir_;
  boost::filesystem::path sysroot_dir_{tmp_dir_ / "ostree-rootfs"};
  std::string rev_;
  std::unique_ptr<OstreeDeployment, OstreeDeploymentDeleter> deployment_;
};

class AktualizrSecondaryWrapper {
 public:
  AktualizrSecondaryWrapper(const OstreeRootfs& sysroot, const Treehub& treehub, const VerificationType vtype) {
    config_.pacman.type = PACKAGE_MANAGER_OSTREE;
    config_.pacman.os = sysroot.getOSName();
    config_.pacman.sysroot = sysroot.getPath();
    config_.pacman.ostree_server = treehub.url();

    config_.bootloader.reboot_sentinel_dir = storage_dir_.Path();
    config_.bootloader.reboot_sentinel_name = "need_reboot";

    config_.storage.path = storage_dir_.Path();
    config_.storage.type = StorageType::kSqlite;

    config_.uptane.verification_type = vtype;

    storage_ = INvStorage::newStorage(config_.storage);
    secondary_ = std::make_shared<AktualizrSecondaryOstree>(config_, storage_);
    secondary_->initialize();
  }

  std::shared_ptr<AktualizrSecondaryOstree>& operator->() { return secondary_; }

  [[nodiscard]] Uptane::Target getPendingVersion() const { return getVersion().first; }

  [[nodiscard]] Uptane::Target getCurrentVersion() const { return getVersion().second; }

  [[nodiscard]] std::pair<Uptane::Target, Uptane::Target> getVersion() const {
    boost::optional<Uptane::Target> current_target;
    boost::optional<Uptane::Target> pending_target;

    storage_->loadInstalledVersions(secondary_->serial().ToString(), &current_target, &pending_target);

    return std::make_pair(!pending_target ? Uptane::Target::Unknown() : *pending_target,
                          !current_target ? Uptane::Target::Unknown() : *current_target);
  }

  [[nodiscard]] std::string hardwareID() const { return secondary_->hwID().ToString(); }

  [[nodiscard]] std::string serial() const { return secondary_->serial().ToString(); }

  void reboot() {
    boost::filesystem::remove(storage_dir_ / config_.bootloader.reboot_sentinel_name);
    secondary_ = std::make_shared<AktualizrSecondaryOstree>(config_, storage_);
    secondary_->initialize();
  }

 private:
  TemporaryDirectory storage_dir_;
  AktualizrSecondaryConfig config_;
  std::shared_ptr<INvStorage> storage_;
  std::shared_ptr<AktualizrSecondaryOstree> secondary_;
};

class UptaneRepoWrapper {
 public:
  UptaneRepoWrapper() { uptane_repo_.generateRepo(KeyType::kED25519); }

  Uptane::SecondaryMetadata addOstreeRev(const std::string& rev, const std::string& hardware_id,
                                         const std::string& serial) {
    uptane_repo_.addCustomImage(rev, Hash(Hash::Type::kSha256, rev), 0, hardware_id);
    uptane_repo_.addTarget(rev, hardware_id, serial);
    uptane_repo_.signTargets();

    return Uptane::SecondaryMetadata(getCurrentMetadata());
  }

  [[nodiscard]] Uptane::MetaBundle getCurrentMetadata() const {
    Uptane::MetaBundle meta_bundle;
    std::string metadata;

    boost::filesystem::load_string_file(director_dir_ / "root.json", metadata);
    meta_bundle.insert({std::make_pair(Uptane::RepositoryType::Director(), Uptane::Role::Root()), std::move(metadata)});
    boost::filesystem::load_string_file(director_dir_ / "targets.json", metadata);
    meta_bundle.insert(
        {std::make_pair(Uptane::RepositoryType::Director(), Uptane::Role::Targets()), std::move(metadata)});

    boost::filesystem::load_string_file(imagerepo_dir_ / "root.json", metadata);
    meta_bundle.insert({std::make_pair(Uptane::RepositoryType::Image(), Uptane::Role::Root()), std::move(metadata)});
    boost::filesystem::load_string_file(imagerepo_dir_ / "timestamp.json", metadata);
    meta_bundle.insert(
        {std::make_pair(Uptane::RepositoryType::Image(), Uptane::Role::Timestamp()), std::move(metadata)});
    boost::filesystem::load_string_file(imagerepo_dir_ / "snapshot.json", metadata);
    meta_bundle.insert(
        {std::make_pair(Uptane::RepositoryType::Image(), Uptane::Role::Snapshot()), std::move(metadata)});
    boost::filesystem::load_string_file(imagerepo_dir_ / "targets.json", metadata);
    meta_bundle.insert({std::make_pair(Uptane::RepositoryType::Image(), Uptane::Role::Targets()), std::move(metadata)});

    return meta_bundle;
  }

  [[nodiscard]] std::shared_ptr<std::string> getImageData(const std::string& targetname) const {
    auto image_data = std::make_shared<std::string>();
    boost::filesystem::load_string_file(root_dir_ / targetname, *image_data);
    return image_data;
  }

 private:
  TemporaryDirectory root_dir_;
  boost::filesystem::path director_dir_{root_dir_ / "repo/director"};
  boost::filesystem::path imagerepo_dir_{root_dir_ / "repo/repo"};
  UptaneRepo uptane_repo_{root_dir_.Path(), "", ""};
};

class SecondaryOstreeTest : public ::testing::TestWithParam<VerificationType> {
 public:
  static const char* curOstreeRootfsRev(OstreeDeployment* ostree_depl) {
    (void)ostree_depl;
    return sysroot_->getDeploymentRev();
  }

  static OstreeDeployment* curOstreeDeployment(OstreeSysroot* ostree_sysroot) {
    (void)ostree_sysroot;
    return sysroot_->getDeployment();
  }

  static void setOstreeRootfsTemplate(const std::string& ostree_rootfs_template) {
    ostree_rootfs_template_ = ostree_rootfs_template;
  }

 protected:
  static void SetUpTestSuite() {
    treehub_ = std::make_shared<Treehub>("tests/sota_tools/treehub_server.py");
    sysroot_ = std::make_shared<OstreeRootfs>(ostree_rootfs_template_);
  }

  static void TearDownTestSuite() {
    treehub_.reset();
    sysroot_.reset();
  }

  SecondaryOstreeTest() {
    if (needs_reset_) {
      sysroot_->resetDeployment();
      needs_reset_ = false;
    }
  }

  Uptane::MetaBundle addDefaultTarget() { return addTarget(treehub_->curRev()); }

  Uptane::MetaBundle addTarget(const std::string& rev = "", const std::string& hardware_id = "",
                               const std::string& serial = "") {
    auto rev_to_apply = rev.empty() ? treehub_->curRev() : rev;
    auto hw_id = hardware_id.empty() ? secondary_.hardwareID() : hardware_id;
    auto serial_id = serial.empty() ? secondary_.serial() : serial;

    uptane_repo_.addOstreeRev(rev, hw_id, serial_id);

    return currentMetadata();
  }

  [[nodiscard]] Uptane::MetaBundle currentMetadata() const { return uptane_repo_.getCurrentMetadata(); }

  static std::string getCredsToSend() {
    std::map<std::string, std::string> creds_map = {
        {"ca.pem", ""}, {"client.pem", ""}, {"pkey.pem", ""}, {"server.url", treehub_->url()}};

    std::stringstream creads_strstream;
    Utils::writeArchive(creds_map, creads_strstream);

    return creads_strstream.str();
  }

  Hash treehubCurRevHash() const { return Hash(Hash::Type::kSha256, treehub_->curRev()); }
  Hash sysrootCurRevHash() const { return Hash(Hash::Type::kSha256, sysroot_->getDeploymentRev()); }
  const std::string& treehubCurRev() const { return treehub_->curRev(); }

  static std::shared_ptr<Treehub> treehub_;
  static std::string ostree_rootfs_template_;
  static std::shared_ptr<OstreeRootfs> sysroot_;
  static bool needs_reset_;

  AktualizrSecondaryWrapper secondary_{*sysroot_, *treehub_, GetParam()};
  UptaneRepoWrapper uptane_repo_;
};

std::shared_ptr<Treehub> SecondaryOstreeTest::treehub_{nullptr};
std::string SecondaryOstreeTest::ostree_rootfs_template_{"./build/ostree_repo"};
std::shared_ptr<OstreeRootfs> SecondaryOstreeTest::sysroot_{nullptr};
bool SecondaryOstreeTest::needs_reset_{false};

TEST_P(SecondaryOstreeTest, fullUptaneVerificationInvalidRevision) {
  EXPECT_TRUE(secondary_->putMetadata(addTarget("invalid-revision")).isSuccess());
  EXPECT_FALSE(secondary_->downloadOstreeUpdate(getCredsToSend()).isSuccess());
}

TEST_P(SecondaryOstreeTest, fullUptaneVerificationInvalidHwID) {
  EXPECT_FALSE(secondary_->putMetadata(addTarget("", "invalid-hardware-id", "")).isSuccess());
}

TEST_P(SecondaryOstreeTest, fullUptaneVerificationInvalidSerial) {
  bool expected_result = false;
  if (GetParam() == VerificationType::kTuf) {
    // Serials aren't checked, so we won't notice the problem.
    expected_result = true;
  }
  EXPECT_EQ(secondary_->putMetadata(addTarget("", "", "invalid-serial-id")).isSuccess(), expected_result);
}

TEST_P(SecondaryOstreeTest, verifyUpdatePositive) {
  // check the version reported in the manifest just after an initial boot
  Uptane::Manifest manifest = secondary_->getManifest();
  EXPECT_TRUE(manifest.verifySignature(secondary_->publicKey()));
  EXPECT_EQ(manifest.installedImageHash(), sysrootCurRevHash());

  // send metadata and do their full Uptane verification
  EXPECT_TRUE(secondary_->putMetadata(addDefaultTarget()).isSuccess());

  // emulate reboot to make sure that we can continue with an update installation after reboot
  secondary_.reboot();

  EXPECT_TRUE(secondary_->downloadOstreeUpdate(getCredsToSend()).isSuccess());
  EXPECT_EQ(secondary_->install().result_code.num_code, data::ResultCode::Numeric::kNeedCompletion);

  // check if the update was installed and pending
  EXPECT_TRUE(secondary_.getPendingVersion().MatchHash(treehubCurRevHash()));
  // manifest should still report the old version
  manifest = secondary_->getManifest();
  EXPECT_TRUE(manifest.verifySignature(secondary_->publicKey()));
  EXPECT_EQ(manifest.installedImageHash(), sysrootCurRevHash());

  // emulate reboot
  sysroot_->setNewDeploymentRev(treehubCurRev());
  secondary_.reboot();

  // check if the version in the DB and reported in the manifest matches with the installed and applied one
  EXPECT_FALSE(secondary_.getPendingVersion().IsValid());
  EXPECT_TRUE(secondary_.getCurrentVersion().MatchHash(treehubCurRevHash()));
  manifest = secondary_->getManifest();
  EXPECT_TRUE(manifest.verifySignature(secondary_->publicKey()));
  EXPECT_EQ(manifest.installedImageHash(), treehubCurRevHash());

  // emulate reboot
  // check if the installed version persists after a reboot
  secondary_.reboot();
  EXPECT_FALSE(secondary_.getPendingVersion().IsValid());
  EXPECT_TRUE(secondary_.getCurrentVersion().MatchHash(treehubCurRevHash()));
  manifest = secondary_->getManifest();
  EXPECT_TRUE(manifest.verifySignature(secondary_->publicKey()));
  EXPECT_EQ(manifest.installedImageHash(), treehubCurRevHash());

  // The next run will require the OSTree deployment to be reset.
  needs_reset_ = true;
}

INSTANTIATE_TEST_SUITE_P(SecondaryTestVerificationType, SecondaryOstreeTest,
                         ::testing::Values(VerificationType::kFull, VerificationType::kTuf));

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  if (argc != 2) {
    std::cerr << "Error: " << argv[0] << " <ostree rootfs path>\n";
    return EXIT_FAILURE;
  }

  SecondaryOstreeTest::setOstreeRootfsTemplate(argv[1]);

  logger_init();
  logger_set_threshold(boost::log::trivial::info);

  return RUN_ALL_TESTS();
}

extern "C" OstreeDeployment* ostree_sysroot_get_booted_deployment(OstreeSysroot* ostree_sysroot) {
  return SecondaryOstreeTest::curOstreeDeployment(ostree_sysroot);
}

extern "C" const char* ostree_deployment_get_csum(OstreeDeployment* ostree_deployment) {
  return SecondaryOstreeTest::curOstreeRootfsRev(ostree_deployment);
}
