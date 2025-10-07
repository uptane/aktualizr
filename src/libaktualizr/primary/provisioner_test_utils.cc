#include "provisioner_test_utils.h"

#include <gtest/gtest.h>

void ExpectProvisionOK(Provisioner&& provisioner_in) {
  Provisioner provisioner = std::move(provisioner_in);
  int attempts = 0;
  bool last_attempt = false;
  while (provisioner.ShouldAttemptAgain()) {
    EXPECT_FALSE(last_attempt) << "Provisioner::Attempt() should return false iff ShouldAttemptAgain()";
    last_attempt = provisioner.Attempt();
    // Avoid infinite loops if the ShouldAttemptAgain doesn't work right
    attempts++;
    ASSERT_LE(attempts, 100) << "Far too many Provisioning attempts!";
  }
  EXPECT_TRUE(last_attempt) << "Provisioner::Attempt() should return false iff ShouldAttemptAgain()";
  EXPECT_EQ(provisioner.CurrentState(), Provisioner::State::kOk);
}

void ExpectProvisionError(Provisioner&& provisioner_in, const std::string& match) {
  Provisioner provisioner = std::move(provisioner_in);
  bool last_attempt;
  for (int attempt = 0; attempt < 3; attempt++) {
    EXPECT_TRUE(provisioner.ShouldAttemptAgain());
    last_attempt = provisioner.Attempt();
    ASSERT_FALSE(last_attempt) << "Expecting provisioning to fail with error " << match;
  }
  EXPECT_TRUE(provisioner.ShouldAttemptAgain())
      << "Provisioner::Attempt() should return false iff ShouldAttemptAgain()";
  auto err_message = provisioner.LastError();
  auto matches = err_message.find(match);
  EXPECT_NE(matches, std::string::npos) << "Error message didn't contain " << match << " actual:" << err_message;
}
