#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/process.hpp>

#include "aktualizr_secondary_file.h"
#include "crypto/keymanager.h"
#include "libaktualizr/types.h"
#include "storage/invstorage.h"
#include "test_utils.h"
#include "update_agent.h"
#include "update_agent_file.h"
#include "uptane_repo.h"
#include "utilities/utils.h"

using ::testing::NiceMock;

class UpdateAgentMock : public FileUpdateAgent {
 public:
  UpdateAgentMock(boost::filesystem::path target_filepath, std::string target_name)
      : FileUpdateAgent(std::move(target_filepath), std::move(target_name)) {
    ON_CALL(*this, receiveData).WillByDefault([this](const Uptane::Target& target, const uint8_t* data, size_t size) {
      return FileUpdateAgent::receiveData(target, data, size);
    });
    ON_CALL(*this, install).WillByDefault([this](const Uptane::Target& target) {
      return FileUpdateAgent::install(target);
    });
  }

  MOCK_METHOD(data::InstallationResult, receiveData, (const Uptane::Target& target, const uint8_t* data, size_t size));
  MOCK_METHOD(data::InstallationResult, install, (const Uptane::Target& target));
};

class AktualizrSecondaryWrapper {
 public:
  AktualizrSecondaryWrapper(VerificationType verification_type) {
    AktualizrSecondaryConfig config;
    config.pacman.type = PACKAGE_MANAGER_NONE;
    config.uptane.verification_type = verification_type;
    config.storage.path = storage_dir_.Path();
    config.storage.type = StorageType::kSqlite;

    storage_ = INvStorage::newStorage(config.storage);

    update_agent_ = std::make_shared<NiceMock<UpdateAgentMock>>(config.storage.path / "firmware.txt", "");

    secondary_ = std::make_shared<AktualizrSecondaryFile>(config, storage_, update_agent_);
    secondary_->initialize();
  }

  std::shared_ptr<AktualizrSecondaryFile>& operator->() { return secondary_; }

  Uptane::Target getPendingVersion() const {
    boost::optional<Uptane::Target> pending_target;

    storage_->loadInstalledVersions(secondary_->serial().ToString(), nullptr, &pending_target);
    return *pending_target;
  }

  std::string hardwareID() const { return secondary_->hwID().ToString(); }

  std::string serial() const { return secondary_->serial().ToString(); }

  boost::filesystem::path targetFilepath() const {
    return storage_dir_.Path() / AktualizrSecondaryFile::FileUpdateDefaultFile;
  }

  std::shared_ptr<NiceMock<UpdateAgentMock>> update_agent_;

 private:
  TemporaryDirectory storage_dir_;
  std::shared_ptr<AktualizrSecondaryFile> secondary_;
  std::shared_ptr<INvStorage> storage_;
};

class UptaneRepoWrapper {
 public:
  UptaneRepoWrapper() { uptane_repo_.generateRepo(KeyType::kED25519); }

  Uptane::SecondaryMetadata addImageFile(const std::string& targetname, const std::string& hardware_id,
                                         const std::string& serial, size_t size = 2049, bool add_and_sign_target = true,
                                         bool add_invalid_images = false, size_t delta = 2) {
    const auto image_file_path = root_dir_ / targetname;
    generateRandomFile(image_file_path, size);

    uptane_repo_.addImage(image_file_path, targetname, hardware_id);
    if (add_and_sign_target) {
      uptane_repo_.addTarget(targetname, hardware_id, serial, "");
      uptane_repo_.signTargets();
    }

    if (add_and_sign_target && add_invalid_images) {
      const auto smaller_image_file_path = image_file_path.string() + ".smaller";
      const auto bigger_image_file_path = image_file_path.string() + ".bigger";
      const auto broken_image_file_path = image_file_path.string() + ".broken";

      boost::filesystem::copy(image_file_path, smaller_image_file_path);
      boost::filesystem::copy(image_file_path, bigger_image_file_path);
      boost::filesystem::copy(image_file_path, broken_image_file_path);

      if (!boost::filesystem::exists(smaller_image_file_path)) {
        LOG_ERROR << "File does not exists: " << smaller_image_file_path;
      }

      boost::filesystem::resize_file(smaller_image_file_path, size - delta);
      boost::filesystem::resize_file(bigger_image_file_path, size + delta);

      std::ofstream broken_image{broken_image_file_path,
                                 std::ios_base::in | std::ios_base::out | std::ios_base::ate | std::ios_base::binary};
      unsigned char data_to_inject[]{0xFF};
      broken_image.seekp(static_cast<long>(-sizeof(data_to_inject)), std::ios_base::end);
      broken_image.write(reinterpret_cast<const char*>(data_to_inject), sizeof(data_to_inject));
      broken_image.close();
    }

    return getCurrentMetadata();
  }

  void addCustomImageMetadata(const std::string& targetname, const std::string& hardware_id,
                              const std::string& custom_version) {
    auto custom = Json::Value();
    custom["targetFormat"] = "BINARY";
    custom["version"] = custom_version;
    // Don't use the custom_version since it only allows integers and we want to
    // be able to put garbage there.
    uptane_repo_.addCustomImage(targetname, Hash(Hash::Type::kSha256, targetname), 1, hardware_id, "", 0, Delegation(),
                                custom);
  }

  Uptane::MetaBundle getCurrentMetadata() const {
    Uptane::MetaBundle meta_bundle;
    std::string metadata;

    boost::filesystem::load_string_file(director_dir_ / "root.json", metadata);
    meta_bundle.emplace(std::make_pair(Uptane::RepositoryType::Director(), Uptane::Role::Root()), std::move(metadata));
    boost::filesystem::load_string_file(director_dir_ / "targets.json", metadata);
    meta_bundle.emplace(std::make_pair(Uptane::RepositoryType::Director(), Uptane::Role::Targets()),
                        std::move(metadata));

    boost::filesystem::load_string_file(imagerepo_dir_ / "root.json", metadata);
    meta_bundle.emplace(std::make_pair(Uptane::RepositoryType::Image(), Uptane::Role::Root()), std::move(metadata));
    boost::filesystem::load_string_file(imagerepo_dir_ / "timestamp.json", metadata);
    meta_bundle.emplace(std::make_pair(Uptane::RepositoryType::Image(), Uptane::Role::Timestamp()),
                        std::move(metadata));
    boost::filesystem::load_string_file(imagerepo_dir_ / "snapshot.json", metadata);
    meta_bundle.emplace(std::make_pair(Uptane::RepositoryType::Image(), Uptane::Role::Snapshot()), std::move(metadata));
    boost::filesystem::load_string_file(imagerepo_dir_ / "targets.json", metadata);
    meta_bundle.emplace(std::make_pair(Uptane::RepositoryType::Image(), Uptane::Role::Targets()), std::move(metadata));

    return meta_bundle;
  }

  std::string getTargetImagePath(const std::string& targetname) const { return (root_dir_ / targetname).string(); }

  void refreshRoot(Uptane::RepositoryType repo) { uptane_repo_.refresh(repo, Uptane::Role::Root()); }

 private:
  static void generateRandomFile(const boost::filesystem::path& filepath, size_t size) {
    std::ofstream file{filepath.string(), std::ofstream::binary};

    if (!file.is_open() || !file.good()) {
      throw std::runtime_error("Failed to create a file: " + filepath.string());
    }

    const unsigned char symbols[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuv";
    unsigned char cur_symbol;

    for (unsigned int ii = 0; ii < size; ++ii) {
      cur_symbol = symbols[static_cast<unsigned int>(rand()) % sizeof(symbols)];
      file.put(static_cast<char>(cur_symbol));
    }

    file.close();
  }

 private:
  TemporaryDirectory root_dir_;
  boost::filesystem::path director_dir_{root_dir_ / "repo/director"};
  boost::filesystem::path imagerepo_dir_{root_dir_ / "repo/repo"};
  UptaneRepo uptane_repo_{root_dir_.Path(), "", ""};
  Uptane::DirectorRepository director_repo_;
};

class SecondaryTest : public ::testing::Test {
 public:
  SecondaryTest(VerificationType verification_type = VerificationType::kFull, bool default_target = true)
      : secondary_(verification_type), update_agent_(*(secondary_.update_agent_)) {
    if (default_target) {
      uptane_repo_.addImageFile(default_target_, secondary_->hwID().ToString(), secondary_->serial().ToString(),
                                target_size, true, true, inavlid_target_size_delta);
    }
  }

 private:
  std::vector<Uptane::Target> getCurrentTargets() {
    auto targets = Uptane::Targets(Utils::parseJSON(getMetaFromBundle(
        uptane_repo_.getCurrentMetadata(), Uptane::RepositoryType::Director(), Uptane::Role::Targets())));
    return targets.getTargets(secondary_->serial(), secondary_->hwID());
  }

  Uptane::Target getDefaultTarget() {
    auto targets = getCurrentTargets();
    EXPECT_GT(targets.size(), 0);
    return targets[0];
  }

  Hash getDefaultTargetHash() { return Hash(Hash::Type::kSha256, getDefaultTarget().sha256Hash()); }

 protected:
  data::ResultCode::Numeric sendImageFile(std::string target_name = default_target_) {
    auto image_path = uptane_repo_.getTargetImagePath(target_name);
    size_t total_size = boost::filesystem::file_size(image_path);

    std::ifstream file{image_path};

    uint8_t buf[send_buffer_size];
    size_t read_and_send_data_size = 0;

    while (read_and_send_data_size < total_size) {
      auto read_bytes = file.readsome(reinterpret_cast<char*>(buf), sizeof(buf));
      if (read_bytes < 0) {
        file.close();
        return data::ResultCode::Numeric::kGeneralError;
      }

      auto result = secondary_->receiveData(buf, static_cast<size_t>(read_bytes));
      if (!result.isSuccess()) {
        file.close();
        return result.result_code.num_code;
      }
      read_and_send_data_size += static_cast<size_t>(read_bytes);
    }

    file.close();

    data::ResultCode::Numeric result{data::ResultCode::Numeric::kGeneralError};
    if (read_and_send_data_size == total_size) {
      result = data::ResultCode::Numeric::kOk;
    }

    return result;
  }

  void verifyTargetAndManifest() {
    // check if a file was actually updated
    ASSERT_TRUE(boost::filesystem::exists(secondary_.targetFilepath()));
    auto target = getDefaultTarget();

    // check the updated file hash
    auto target_hash = Hash(Hash::Type::kSha256, target.sha256Hash());
    auto target_file_hash = Hash::generate(Hash::Type::kSha256, Utils::readFile(secondary_.targetFilepath()));
    EXPECT_EQ(target_hash, target_file_hash);

    // check the secondary manifest
    auto manifest = secondary_->getManifest();
    EXPECT_EQ(manifest.installedImageHash(), target_file_hash);
    EXPECT_EQ(manifest.filepath(), target.filename());
  }

  static constexpr const char* const default_target_{"default-target"};
  static constexpr const char* const bigger_target_{"default-target.bigger"};
  static constexpr const char* const smaller_target_{"default-target.smaller"};
  static constexpr const char* const broken_target_{"default-target.broken"};

  static const size_t target_size{2049};
  static const size_t inavlid_target_size_delta{2};
  static const size_t send_buffer_size{1024};

  AktualizrSecondaryWrapper secondary_;
  UptaneRepoWrapper uptane_repo_;
  NiceMock<UpdateAgentMock>& update_agent_;
  TemporaryDirectory image_dir_;
};

class SecondaryTestNegative
    : public SecondaryTest,
      public ::testing::WithParamInterface<std::tuple<Uptane::RepositoryType, Uptane::Role, VerificationType, bool>> {
 public:
  SecondaryTestNegative() : SecondaryTest(std::get<2>(GetParam())), success_expected_(std::get<3>(GetParam())) {}

 protected:
  class MetadataInvalidator : public Uptane::SecondaryMetadata {
   public:
    MetadataInvalidator(const Uptane::MetaBundle& valid_metadata, const Uptane::RepositoryType& repo,
                        const Uptane::Role& role)
        : Uptane::SecondaryMetadata(valid_metadata), repo_type_(repo), role_(role) {}

    void getRoleMetadata(std::string* result, const Uptane::RepositoryType& repo, const Uptane::Role& role,
                         Uptane::Version version) const override {
      Uptane::SecondaryMetadata::getRoleMetadata(result, repo, role, version);
      if (!(repo_type_ == repo && role_ == role)) {
        return;
      }
      (*result)[10] = 'f';
    }

   private:
    Uptane::RepositoryType repo_type_;
    Uptane::Role role_;
  };

  MetadataInvalidator currentMetadata() const {
    return MetadataInvalidator(uptane_repo_.getCurrentMetadata(), std::get<0>(GetParam()), std::get<1>(GetParam()));
  }

  bool success_expected_;
};

/**
 * This test is parameterized to control which metadata to malform. See
 * INSTANTIATE_TEST_SUITE_P for the list of test instantiations with concrete
 * parameter values.
 */
TEST_P(SecondaryTestNegative, MalformedMetadaJson) {
  data::ResultCode::Numeric result{data::ResultCode::Numeric::kGeneralError};
  if (!success_expected_) {
    EXPECT_CALL(update_agent_, receiveData).Times(0);
    EXPECT_CALL(update_agent_, install).Times(0);
  } else {
    EXPECT_CALL(update_agent_, receiveData)
        .Times(target_size / send_buffer_size + (target_size % send_buffer_size ? 1 : 0));
    EXPECT_CALL(update_agent_, install).Times(1);
    result = data::ResultCode::Numeric::kOk;
  }

  EXPECT_EQ(secondary_->putMetadata(currentMetadata()).isSuccess(), success_expected_);
  ASSERT_EQ(sendImageFile(), result);
  EXPECT_EQ(secondary_->install().isSuccess(), success_expected_);

  if (success_expected_) {
    verifyTargetAndManifest();
  }
}

/**
 * Instantiates the parameterized test for each specified value of
 * std::tuple<Uptane::RepositoryType, Uptane::Role, VerificationType, success_expected>.
 * The parameter value indicates which metadata to malform. Anything that
 * expects success (true) can be considered something like a failure to detect
 * an attack.
 */
INSTANTIATE_TEST_SUITE_P(
    SecondaryTestMalformedMetadata, SecondaryTestNegative,
    ::testing::Values(
        std::make_tuple(Uptane::RepositoryType::Director(), Uptane::Role::Root(), VerificationType::kFull, false),
        std::make_tuple(Uptane::RepositoryType::Director(), Uptane::Role::Targets(), VerificationType::kFull, false),
        std::make_tuple(Uptane::RepositoryType::Image(), Uptane::Role::Root(), VerificationType::kFull, false),
        std::make_tuple(Uptane::RepositoryType::Image(), Uptane::Role::Timestamp(), VerificationType::kFull, false),
        std::make_tuple(Uptane::RepositoryType::Image(), Uptane::Role::Snapshot(), VerificationType::kFull, false),
        std::make_tuple(Uptane::RepositoryType::Image(), Uptane::Role::Targets(), VerificationType::kFull, false),
        std::make_tuple(Uptane::RepositoryType::Director(), Uptane::Role::Root(), VerificationType::kTuf, true),
        std::make_tuple(Uptane::RepositoryType::Director(), Uptane::Role::Targets(), VerificationType::kTuf, true),
        std::make_tuple(Uptane::RepositoryType::Image(), Uptane::Role::Root(), VerificationType::kTuf, false),
        std::make_tuple(Uptane::RepositoryType::Image(), Uptane::Role::Timestamp(), VerificationType::kTuf, false),
        std::make_tuple(Uptane::RepositoryType::Image(), Uptane::Role::Snapshot(), VerificationType::kTuf, false),
        std::make_tuple(Uptane::RepositoryType::Image(), Uptane::Role::Targets(), VerificationType::kTuf, false)));

class SecondaryTestVerification : public SecondaryTest, public ::testing::WithParamInterface<VerificationType> {
 public:
  SecondaryTestVerification() : SecondaryTest(GetParam()){};
};

/**
 * This test is parameterized with VerificationType to indicate what level of
 * metadata verification to perform. See INSTANTIATE_TEST_SUITE_P for the list
 * of test instantiations with concrete parameter values.
 */
TEST_P(SecondaryTestVerification, VerificationPositive) {
  EXPECT_CALL(update_agent_, receiveData)
      .Times(target_size / send_buffer_size + (target_size % send_buffer_size ? 1 : 0));
  EXPECT_CALL(update_agent_, install).Times(1);

  ASSERT_TRUE(secondary_->putMetadata(uptane_repo_.getCurrentMetadata()).isSuccess());
  ASSERT_EQ(sendImageFile(), data::ResultCode::Numeric::kOk);
  ASSERT_TRUE(secondary_->install().isSuccess());

  verifyTargetAndManifest();
}

/**
 * Instantiates the parameterized test for each specified value of VerificationType.
 */
INSTANTIATE_TEST_SUITE_P(SecondaryTestVerificationType, SecondaryTestVerification,
                         ::testing::Values(VerificationType::kFull, VerificationType::kTuf));

TEST_F(SecondaryTest, TwoImagesAndOneTarget) {
  // two images for the same ECU, just one of them is added as a target and signed
  // default image and corresponding target has been already added, just add another image
  auto metadata = uptane_repo_.addImageFile("second_image_00", secondary_->hwID().ToString(),
                                            secondary_->serial().ToString(), target_size, false, false);
  EXPECT_TRUE(secondary_->putMetadata(metadata).isSuccess());
}

TEST_F(SecondaryTest, IncorrectTargetQuantity) {
  const std::string hwid{secondary_->hwID().ToString()};
  const std::string serial{secondary_->serial().ToString()};
  {
    // two targets for the same ECU
    auto metadata = uptane_repo_.addImageFile("second_target", hwid, serial);
    EXPECT_FALSE(secondary_->putMetadata(metadata).isSuccess());
  }

  {
    // zero targets for the ECU being tested
    auto metadata = uptane_repo_.addImageFile("mytarget", hwid, "non-existing-serial");
    EXPECT_FALSE(secondary_->putMetadata(metadata).isSuccess());
  }

  {
    // zero targets for the ECU being tested
    auto metadata = uptane_repo_.addImageFile("mytarget", "non-existig-hwid", serial);
    EXPECT_FALSE(secondary_->putMetadata(metadata).isSuccess());
  }
}

TEST_F(SecondaryTest, DirectorRootVersionIncremented) {
  uptane_repo_.refreshRoot(Uptane::RepositoryType::Director());
  EXPECT_TRUE(secondary_->putMetadata(uptane_repo_.getCurrentMetadata()).isSuccess());
}

TEST_F(SecondaryTest, ImageRootVersionIncremented) {
  uptane_repo_.refreshRoot(Uptane::RepositoryType::Image());
  EXPECT_TRUE(secondary_->putMetadata(uptane_repo_.getCurrentMetadata()).isSuccess());
}

TEST_F(SecondaryTest, SmallerImageFileSize) {
  EXPECT_CALL(update_agent_, receiveData)
      .Times((target_size - inavlid_target_size_delta) / send_buffer_size +
             ((target_size - inavlid_target_size_delta) % send_buffer_size ? 1 : 0));
  EXPECT_CALL(update_agent_, install).Times(1);

  EXPECT_TRUE(secondary_->putMetadata(uptane_repo_.getCurrentMetadata()).isSuccess());

  EXPECT_EQ(sendImageFile(smaller_target_), data::ResultCode::Numeric::kOk);
  EXPECT_FALSE(secondary_->install().isSuccess());
}

TEST_F(SecondaryTest, BiggerImageFileSize) {
  EXPECT_CALL(update_agent_, receiveData)
      .Times((target_size + inavlid_target_size_delta) / send_buffer_size +
             ((target_size + inavlid_target_size_delta) % send_buffer_size ? 1 : 0));
  EXPECT_CALL(update_agent_, install).Times(1);

  EXPECT_TRUE(secondary_->putMetadata(uptane_repo_.getCurrentMetadata()).isSuccess());

  EXPECT_EQ(sendImageFile(bigger_target_), data::ResultCode::Numeric::kOk);
  EXPECT_FALSE(secondary_->install().isSuccess());
}

TEST_F(SecondaryTest, InvalidImageData) {
  EXPECT_CALL(update_agent_, receiveData)
      .Times(target_size / send_buffer_size + (target_size % send_buffer_size ? 1 : 0));
  EXPECT_CALL(update_agent_, install).Times(1);

  EXPECT_TRUE(secondary_->putMetadata(uptane_repo_.getCurrentMetadata()).isSuccess());
  EXPECT_EQ(sendImageFile(broken_target_), data::ResultCode::Numeric::kOk);
  EXPECT_FALSE(secondary_->install().isSuccess());
}

class SecondaryTestTuf
    : public SecondaryTest,
      public ::testing::WithParamInterface<std::pair<std::vector<std::string>, boost::optional<std::string>>> {
 public:
  // No default Targets so as to be able to more thoroughly test the Target
  // comparison.
  SecondaryTestTuf() : SecondaryTest(VerificationType::kTuf, false){};
};

/**
 * This test is parameterized with a series of Targets with custom versions and
 * which one should be considered the latest, if any. See
 * INSTANTIATE_TEST_SUITE_P for the list of test instantiations with concrete
 * parameter values.
 */
TEST_P(SecondaryTestTuf, TufVersions) {
  const std::string hwid{secondary_->hwID().ToString()};
  {
    int counter = 0;
    for (const auto& version : GetParam().first) {
      // Add counter so we can add multiple Targets with the same version.
      uptane_repo_.addCustomImageMetadata("v" + version + "-" + std::to_string(++counter), hwid, version);
    }
    auto metadata = uptane_repo_.getCurrentMetadata();
    auto expected = GetParam().second;
    EXPECT_EQ(secondary_->putMetadata(metadata).isSuccess(), !!expected);
    if (!!expected) {
      EXPECT_EQ(secondary_->getPendingTarget().custom_version(), expected);
      // Ignore the initial "v" and the counter suffix.
      EXPECT_EQ(secondary_->getPendingTarget().filename().compare(1, expected->size(), expected.get()), 0);
    }
  }
}

/**
 * Instantiates the parameterized test for each specified value of
 * std::pair<std::vector<std::string>, boost::optional<std::string>>>.
 * The first parameter value is a list of Targets with custom versions and the
 * second paramter is which one should be considered the latest, if any.
 */
INSTANTIATE_TEST_SUITE_P(SecondaryTestTufVersions, SecondaryTestTuf,
                         ::testing::Values(std::make_pair(std::vector<std::string>{"1"}, "1"),
                                           std::make_pair(std::vector<std::string>{"1", "2"}, "2"),
                                           std::make_pair(std::vector<std::string>{"1", "2", "3"}, "3"),
                                           std::make_pair(std::vector<std::string>{"3", "2", "1"}, "3"),
                                           std::make_pair(std::vector<std::string>{"2", "3", "1"}, "3"),
                                           std::make_pair(std::vector<std::string>{"invalid", "1"}, "1"),
                                           std::make_pair(std::vector<std::string>{"1", "invalid"}, "1"),
                                           std::make_pair(std::vector<std::string>{"invalid", "1", "2"}, "2"),
                                           std::make_pair(std::vector<std::string>{"1", "2", "invalid"}, "2"),
                                           std::make_pair(std::vector<std::string>{"1", "invalid", "2"}, "2"),
                                           std::make_pair(std::vector<std::string>{"1", "invalid1", "invalid2"}, "1"),
                                           std::make_pair(std::vector<std::string>{"invalid1", "1", "invalid2"}, "1"),
                                           std::make_pair(std::vector<std::string>{"invalid1", "invalid2", "1"}, "1"),
                                           std::make_pair(std::vector<std::string>{"1", "1", "2"}, "2"),
                                           std::make_pair(std::vector<std::string>{"2", "1", "1"}, "2"),
                                           std::make_pair(std::vector<std::string>{"1", "2", "1"}, "2"),
                                           std::make_pair(std::vector<std::string>{"1", "2", "2"}, boost::none),
                                           std::make_pair(std::vector<std::string>{"2", "2", "1"}, boost::none),
                                           std::make_pair(std::vector<std::string>{"2", "1", "2"}, boost::none),
                                           std::make_pair(std::vector<std::string>{""}, ""),
                                           std::make_pair(std::vector<std::string>{"text"}, "text"),
                                           std::make_pair(std::vector<std::string>{"invalid1", "invalid2"},
                                                          boost::none)));

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  logger_init();
  logger_set_threshold(boost::log::trivial::info);

  return RUN_ALL_TESTS();
}
