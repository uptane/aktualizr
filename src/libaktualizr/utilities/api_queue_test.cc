#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include "utilities/apiqueue.h"

using std::cout;
using std::future;
using std::future_status;

class CheckLifetime {
 public:
  CheckLifetime() { cout << "ctor\n"; }
  ~CheckLifetime() {
    valid = 999;
    cout << "dtor\n";
  }
  CheckLifetime(const CheckLifetime& other) {
    (void)other;
    cout << "copy-ctor\n";
  }
  CheckLifetime& operator=(const CheckLifetime&) = delete;
  CheckLifetime& operator=(CheckLifetime&&) = delete;
  CheckLifetime(CheckLifetime&&) = delete;

  int valid{100};
};

TEST(ApiQueue, Simple) {
  api::CommandQueue dut;
  future<int> result;
  {
    CheckLifetime checkLifetime;
    std::function<int()> task([checkLifetime] {
      cout << "Running task..." << checkLifetime.valid << "\n";
      return checkLifetime.valid;
    });
    result = dut.enqueue(std::move(task));
    cout << "Leaving scope..";
  }
  EXPECT_EQ(result.wait_for(std::chrono::milliseconds(100)), future_status::timeout);

  dut.run();
  // Include a timeout to avoid a failing test handing forever
  ASSERT_EQ(result.wait_for(std::chrono::seconds(10)), future_status::ready);
  EXPECT_EQ(result.get(), 100);
}

#ifndef __NO_MAIN__
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#endif
