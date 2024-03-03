#include <fcntl.h>
#include <gtest/gtest.h>

#include <future>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include "json/json.h"

#include "libaktualizr/config.h"
#include "libaktualizr/events.h"

#include <sys/file.h>
#include "httpfake.h"
#include "metafake.h"
#include "primary/aktualizr_helpers.h"
#include "uptane_test_common.h"
#include "utilities/utils.h"
#include "virtualsecondary.h"

boost::filesystem::path fake_meta_dir;     // NOLINT
boost::filesystem::path uptane_repos_dir;  // NOLINT

TEST(AktualizrUpdateLock, DisableUsingLock) {
  TemporaryDirectory const temp_dir;
  auto http = std::make_shared<HttpFake>(temp_dir.Path(), "hasupdates", fake_meta_dir);
  Config conf = UptaneTestCommon::makeTestConfig(temp_dir, http->tls_server);
  auto lock_file = temp_dir.Path() / "update.lock";
  conf.uptane.update_lock_file = lock_file;

  auto storage = INvStorage::newStorage(conf.storage);
  UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);

  int const fd = open(lock_file.c_str(), O_CREAT | O_RDWR, 0666);
  ASSERT_GE(fd, 0) << "Open lock file failed:" << strerror(errno);
  int const lock_res = flock(fd, LOCK_EX);
  ASSERT_EQ(lock_res, 0) << "flock failed:" << strerror(errno);

  std::atomic<bool> is_locked{true};
  std::atomic<int> installs{0};
  auto f_cb = [&is_locked, &installs](const std::shared_ptr<event::BaseEvent>& event) {
    LOG_INFO << "Got " << event->variant;
    if (event->isTypeOf<event::InstallStarted>()) {
      EXPECT_FALSE(is_locked);
      installs++;
    }
  };
  boost::signals2::connection const conn = aktualizr.SetSignalHandler(f_cb);

  aktualizr.Initialize();
  aktualizr.UptaneCycle();
  EXPECT_EQ(installs, 0);

  int const unlock_res = flock(fd, LOCK_UN);
  ASSERT_EQ(unlock_res, 0) << "flock unlock failed:" << strerror(errno);
  is_locked = false;

  aktualizr.UptaneCycle();

  EXPECT_GT(installs, 0);
}

#ifndef __NO_MAIN__
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "Error: " << argv[0] << " requires the path to the base directory of Uptane repos.\n";
    return EXIT_FAILURE;
  }
  uptane_repos_dir = argv[1];

  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  TemporaryDirectory tmp_dir;
  fake_meta_dir = tmp_dir.Path();

  CreateFakeRepoMetaData(fake_meta_dir);

  return RUN_ALL_TESTS();
}
#endif
