#include <gtest/gtest.h>

#include "httpfake.h"
#include "libaktualizr/secondaryinterface.h"
#include "uptane_test_common.h"
#include "utilities/utils.h"
#include "virtualsecondary.h"

class VirtualSecondaryTest : public ::testing::Test {
 protected:
  VirtualSecondaryTest() {
    config_.partial_verifying = false;
    config_.full_client_dir = temp_dir_.Path();
    config_.ecu_serial = "";
    config_.ecu_hardware_id = "secondary_hardware";
    config_.ecu_private_key = "sec.priv";
    config_.ecu_public_key = "sec.pub";
    config_.firmware_path = temp_dir_.Path() / "firmware.txt";
    config_.target_name_path = temp_dir_.Path() / "firmware_name.txt";
    config_.metadata_path = temp_dir_.Path() / "metadata";
  }

  virtual void SetUp() {}
  virtual void TearDown() {}

 protected:
  TemporaryDirectory temp_dir_;
  Primary::VirtualSecondaryConfig config_;
};

/* Create a virtual secondary for testing. */
TEST_F(VirtualSecondaryTest, Instantiation) { EXPECT_NO_THROW(Primary::VirtualSecondary virtual_sec(config_)); }

/*
 * Rotate both Director and Image repo Root keys twice and make sure the Primary
 * correctly sends the intermediate Roots to the Secondary.
 */
TEST(VirtualSecondary, RootRotation) {
  TemporaryDirectory temp_dir;
  TemporaryDirectory meta_dir;
  auto http = std::make_shared<HttpFake>(temp_dir.Path(), "", meta_dir.Path() / "repo");
  Config conf = UptaneTestCommon::makeTestConfig(temp_dir, http->tls_server);
  logger_set_threshold(boost::log::trivial::trace);

  auto storage = INvStorage::newStorage(conf.storage);
  UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
  aktualizr.Initialize();

  UptaneRepo uptane_repo{meta_dir.PathString(), "", ""};
  uptane_repo.generateRepo(KeyType::kED25519);
  uptane_repo.addImage("tests/test_data/firmware.txt", "firmware.txt", "secondary_hw");
  uptane_repo.addTarget("firmware.txt", "secondary_hw", "secondary_ecu_serial", "");
  uptane_repo.signTargets();

  result::UpdateCheck update_result = aktualizr.CheckUpdates().get();
  EXPECT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
  result::Download download_result = aktualizr.Download(update_result.updates).get();
  ASSERT_EQ(download_result.status, result::DownloadStatus::kSuccess);
  result::Install install_result = aktualizr.Install(download_result.updates).get();
  EXPECT_TRUE(install_result.dev_report.success);

  uptane_repo.rotate(Uptane::RepositoryType::Director(), Uptane::Role::Root(), KeyType::kED25519);
  uptane_repo.rotate(Uptane::RepositoryType::Director(), Uptane::Role::Root(), KeyType::kED25519);
  uptane_repo.emptyTargets();
  uptane_repo.addImage("tests/test_data/firmware_name.txt", "firmware_name.txt", "secondary_hw");
  uptane_repo.addTarget("firmware_name.txt", "secondary_hw", "secondary_ecu_serial", "");
  uptane_repo.signTargets();

  update_result = aktualizr.CheckUpdates().get();
  EXPECT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
  download_result = aktualizr.Download(update_result.updates).get();
  ASSERT_EQ(download_result.status, result::DownloadStatus::kSuccess);
  install_result = aktualizr.Install(download_result.updates).get();
  EXPECT_TRUE(install_result.dev_report.success);

  uptane_repo.rotate(Uptane::RepositoryType::Image(), Uptane::Role::Root(), KeyType::kED25519);
  uptane_repo.rotate(Uptane::RepositoryType::Image(), Uptane::Role::Root(), KeyType::kED25519);
  uptane_repo.emptyTargets();
  uptane_repo.addImage("tests/test_data/firmware.txt", "firmware2.txt", "secondary_hw");
  uptane_repo.addTarget("firmware2.txt", "secondary_hw", "secondary_ecu_serial", "");
  uptane_repo.signTargets();

  update_result = aktualizr.CheckUpdates().get();
  EXPECT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
  download_result = aktualizr.Download(update_result.updates).get();
  ASSERT_EQ(download_result.status, result::DownloadStatus::kSuccess);
  install_result = aktualizr.Install(download_result.updates).get();
  EXPECT_TRUE(install_result.dev_report.success);
}

#ifdef FIU_ENABLE

#include "utilities/fault_injection.h"

/*
 * Verifies that updates fail after Root rotation verification failure reported by Secondaries.
 */
TEST(VirtualSecondary, RootRotationFailure) {
  TemporaryDirectory temp_dir;
  TemporaryDirectory meta_dir;
  auto http = std::make_shared<HttpFake>(temp_dir.Path(), "", meta_dir.Path() / "repo");
  Config conf = UptaneTestCommon::makeTestConfig(temp_dir, http->tls_server);
  logger_set_threshold(boost::log::trivial::trace);

  auto storage = INvStorage::newStorage(conf.storage);
  UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
  aktualizr.Initialize();

  UptaneRepo uptane_repo{meta_dir.PathString(), "", ""};
  uptane_repo.generateRepo(KeyType::kED25519);
  uptane_repo.addImage("tests/test_data/firmware.txt", "firmware.txt", "secondary_hw");
  uptane_repo.addTarget("firmware.txt", "secondary_hw", "secondary_ecu_serial", "");
  uptane_repo.signTargets();

  result::UpdateCheck update_result = aktualizr.CheckUpdates().get();
  EXPECT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
  result::Download download_result = aktualizr.Download(update_result.updates).get();
  ASSERT_EQ(download_result.status, result::DownloadStatus::kSuccess);
  result::Install install_result = aktualizr.Install(download_result.updates).get();
  EXPECT_TRUE(install_result.dev_report.success);

  uptane_repo.rotate(Uptane::RepositoryType::Director(), Uptane::Role::Root(), KeyType::kED25519);
  uptane_repo.rotate(Uptane::RepositoryType::Director(), Uptane::Role::Root(), KeyType::kED25519);
  uptane_repo.emptyTargets();
  uptane_repo.addImage("tests/test_data/firmware_name.txt", "firmware_name.txt", "secondary_hw");
  uptane_repo.addTarget("firmware_name.txt", "secondary_hw", "secondary_ecu_serial", "");
  uptane_repo.signTargets();

  // This causes putRoot to be skipped, which means when the latest (v3)
  // metadata is sent, the Secondary can't verify it, since it only has the v1
  // Root.
  fault_injection_init();
  fiu_enable("secondary_putroot", 1, nullptr, 0);

  update_result = aktualizr.CheckUpdates().get();
  EXPECT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
  download_result = aktualizr.Download(update_result.updates).get();
  ASSERT_EQ(download_result.status, result::DownloadStatus::kSuccess);
  install_result = aktualizr.Install(download_result.updates).get();
  EXPECT_EQ(install_result.dev_report.result_code,
            data::ResultCode(data::ResultCode::Numeric::kVerificationFailed, "secondary_hw:VERIFICATION_FAILED"));
  EXPECT_EQ(install_result.dev_report.description, "Sending metadata to one or more ECUs failed");

  fiu_disable("secondary_putroot");

  // Retry after disabling fault injection to verify the test.
  update_result = aktualizr.CheckUpdates().get();
  EXPECT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
  download_result = aktualizr.Download(update_result.updates).get();
  ASSERT_EQ(download_result.status, result::DownloadStatus::kSuccess);
  install_result = aktualizr.Install(download_result.updates).get();
  EXPECT_TRUE(install_result.dev_report.success);
}

#endif  // FIU_ENABLE

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  return RUN_ALL_TESTS();
}
#endif
