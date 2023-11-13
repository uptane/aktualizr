#include <gtest/gtest.h>

#include "primary/update_lock_file.h"

#include "logging/logging.h"
#include "utilities/utils.h"

TEST(UpdateLockFile, Simple) {
  TemporaryFile lock_file;

  UpdateLockFile dut{lock_file.Path()};
  ASSERT_EQ(dut.ShouldUpdate(), UpdateLockFile::kGoAhead);

  UpdateLockFile dut2{lock_file.Path()};

  ASSERT_EQ(dut2.ShouldUpdate(), UpdateLockFile::kNoUpdate);

  dut.UpdateComplete();
  ASSERT_EQ(dut2.ShouldUpdate(), UpdateLockFile::kGoAhead);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  return RUN_ALL_TESTS();
}