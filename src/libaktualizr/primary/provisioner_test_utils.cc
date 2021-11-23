
#include <gtest/gtest.h>
#include "primary/provisioner.h"

// Test utility to run provisioning to completion and check the result
void ExpectProvisionOK(Provisioner&& provisioner) {
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

void ExpectProvisionError(Provisioner&& provisioner, const std::string& match = "") {
  bool last_attempt;
  for (int attempt = 0; attempt < 3; attempt++) {
    EXPECT_TRUE(provisioner.ShouldAttemptAgain());
    last_attempt = provisioner.Attempt();
    EXPECT_FALSE(last_attempt) << "Provisioner::Attempt() should return false iff ShouldAttemptAgain()";
  }
  EXPECT_TRUE(provisioner.ShouldAttemptAgain()) << "Expecting provisioning to fail";
  auto err_message = provisioner.LastError();
  auto matches = err_message.find(match);
  EXPECT_NE(matches, std::string::npos) << "Error message didn't contain " << match << " actual:" << err_message;
}