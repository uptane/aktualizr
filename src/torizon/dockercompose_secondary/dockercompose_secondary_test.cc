#include <gtest/gtest.h>

#include "uptane_test_common.h"

// TODO: IMPLEMENT TESTS

#ifndef __NO_MAIN__
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  return RUN_ALL_TESTS();
}
#endif
