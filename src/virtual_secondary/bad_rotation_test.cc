#include <iostream>

#include <gtest/gtest.h>

#include <boost/log/utility/setup/file.hpp>
#include "boost/filesystem.hpp"
#include "httpfake.h"
#include "libaktualizr/secondaryinterface.h"
#include "libaktualizr/types.h"
#include "logging/logging.h"
#include "uptane/tuf.h"
#include "uptane_repo.h"
#include "uptane_test_common.h"
#include "utilities/utils.h"
#include "virtualsecondary.h"

/*
 * Reproduction for TOR-3452. Rotate the root metadata to avoid it expiring, then recover
 */
TEST(VirtualSecondary, RootRotationExpires) {  // NOLINT
  TemporaryDirectory temp_dir;
  TemporaryDirectory meta_dir;
  auto http = std::make_shared<HttpFake>(temp_dir.Path(), "", meta_dir.Path() / "repo");
  Config conf = UptaneTestCommon::makeTestConfig(temp_dir, http->tls_server);
  logger_set_threshold(boost::log::trivial::trace);

  UptaneRepo uptane_repo{meta_dir.PathString(), "", "2023-03-04T16:43:12Z"};
  uptane_repo.generateRepo(KeyType::kED25519);
  uptane_repo.addImage("tests/test_data/firmware.txt", "firmware.txt", "secondary_hw");

  const std::string hwid = "primary_hw";

  Utils::writeFile(meta_dir / "fake_meta/primary_firmware.txt", std::string("asdf"));
  uptane_repo.addImage(meta_dir / "fake_meta/primary_firmware.txt", "primary_firmware.txt", hwid);
  Utils::writeFile(meta_dir / "fake_meta/primary_firmware2.txt", std::string("asdf"));
  uptane_repo.addImage(meta_dir / "fake_meta/primary_firmware2.txt", "primary_firmware2.txt", hwid);
  uptane_repo.addImage("tests/test_data/firmware_name.txt", "firmware_name.txt", "secondary_hw");
  uptane_repo.addImage("tests/test_data/firmware.txt", "firmware2.txt", "secondary_hw");

  time_t new_expiration_time;
  std::time(&new_expiration_time);
  new_expiration_time += 5;  // make it valid for the next 5 seconds
  struct tm new_expiration_time_str {};
  gmtime_r(&new_expiration_time, &new_expiration_time_str);

  auto timestamp = TimeStamp(new_expiration_time_str);
  uptane_repo.refresh(Uptane::RepositoryType::Image(), Uptane::Role::Root(), timestamp);
  uptane_repo.refresh(Uptane::RepositoryType::Director(), Uptane::Role::Root(), timestamp);

  result::UpdateCheck update_result;
  result::Download download_result;
  result::Install install_result;

  uptane_repo.emptyTargets();
  uptane_repo.addTarget("firmware_name.txt", "secondary_hw", "secondary_ecu_serial");
  uptane_repo.signTargets();

  {
    LOG_INFO << "Starting initial run";
    auto storage = INvStorage::newStorage(conf.storage);
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    aktualizr.Initialize();
    update_result = aktualizr.CheckUpdates().get();
    ASSERT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
    download_result = aktualizr.Download(update_result.updates).get();
    ASSERT_EQ(download_result.status, result::DownloadStatus::kSuccess);
    install_result = aktualizr.Install(download_result.updates).get();
    EXPECT_TRUE(install_result.dev_report.success);
  }
  bool expire = true;
  if (expire) {
    LOG_INFO << "Sleeping in a warehouse";
    for (int x = 0; x < 10; x++) {
      sleep(1);
      if (timestamp.IsExpiredAt(TimeStamp::Now())) {
        break;
      }
    }
    ASSERT_TRUE(timestamp.IsExpiredAt(TimeStamp::Now()));
  } else {
    ASSERT_FALSE(timestamp.IsExpiredAt(TimeStamp::Now()));
  }

  uptane_repo.refresh(Uptane::RepositoryType::Image(), Uptane::Role::Root(), TimeStamp("2024-01-01T16:43:12Z"));
  uptane_repo.refresh(Uptane::RepositoryType::Director(), Uptane::Role::Root(), TimeStamp("2024-01-01T16:43:12Z"));
  uptane_repo.refresh(Uptane::RepositoryType::Image(), Uptane::Role::Root(), TimeStamp("2025-01-01T16:43:12Z"));
  uptane_repo.refresh(Uptane::RepositoryType::Director(), Uptane::Role::Root(), TimeStamp("2025-01-01T16:43:12Z"));

  {
    LOG_INFO << "Starting second run";
    auto storage = INvStorage::newStorage(conf.storage);
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    aktualizr.Initialize();

    uptane_repo.emptyTargets();
    uptane_repo.addTarget("primary_firmware.txt", hwid, "CA:FE:A6:D2:84:9D");
    uptane_repo.signTargets();
    update_result = aktualizr.CheckUpdates().get();
    ASSERT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
    download_result = aktualizr.Download(update_result.updates).get();
    ASSERT_EQ(download_result.status, result::DownloadStatus::kSuccess);
    install_result = aktualizr.Install(download_result.updates).get();
    EXPECT_TRUE(install_result.dev_report.success);

    uptane_repo.emptyTargets();
    uptane_repo.addTarget("firmware2.txt", "secondary_hw", "secondary_ecu_serial");
    uptane_repo.signTargets();

    update_result = aktualizr.CheckUpdates().get();
    ASSERT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
    download_result = aktualizr.Download(update_result.updates).get();
    ASSERT_EQ(download_result.status, result::DownloadStatus::kSuccess);
    install_result = aktualizr.Install(download_result.updates).get();
    EXPECT_TRUE(install_result.dev_report.success);
  }
}

#ifndef __NO_MAIN__
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  return RUN_ALL_TESTS();
}
#endif
