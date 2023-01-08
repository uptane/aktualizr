//
// Created by phil on 07/01/23.
//
#include <gtest/gtest.h>

#include "command_runner.h"

#include <thread>
#include "logging/logging.h"

TEST(CommandRunner, Simple) {
  bool res;

  res = CommandRunner::run("/usr/bin/true");
  EXPECT_TRUE(res);

  res = CommandRunner::run("/usr/bin/false");
  EXPECT_FALSE(res);
}

TEST(CommandRunner, Cancellation) {
  bool res;
  api::FlowControlToken token;

  std::atomic<bool> did_abort;
  did_abort = false;
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(1);

  std::thread t1([&token, end, &did_abort] {
    std::this_thread::sleep_until(end);
    LOG_INFO << "Aborting...";
    token.setAbort();
    did_abort = true;
  });

  res = CommandRunner::run("/usr/bin/sleep 100", &token);
  auto actual_end = std::chrono::steady_clock::now();

  EXPECT_TRUE(did_abort);
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(actual_end - end).count();

  LOG_INFO << "Took:" << diff << "ms to abort";
  EXPECT_LE(-100, diff);
  EXPECT_LE(diff, 1000);
  EXPECT_FALSE(res);
  t1.join();
}

TEST(CommandRunner, CancellationTooLate) {
  bool res;
  api::FlowControlToken token;

  std::atomic<bool> did_abort;
  did_abort = false;
  auto abort_time = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  auto expected_finish_time = std::chrono::steady_clock::now() + std::chrono::seconds(1);

  std::thread t1([&token, abort_time, &did_abort] {
    std::this_thread::sleep_until(abort_time);
    token.setAbort();
    did_abort = true;
  });

  res = CommandRunner::run("/usr/bin/sleep 1", &token);

  auto actual_end = std::chrono::steady_clock::now();
  EXPECT_FALSE(did_abort);
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(actual_end - expected_finish_time).count();

  LOG_INFO << "Command took: 1s + " << diff << "ms";
  EXPECT_LE(-200, diff);
  EXPECT_LE(diff, 200);
  EXPECT_TRUE(res);
  t1.join();
}

#ifndef __NO_MAIN__
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  return RUN_ALL_TESTS();
}
#endif
