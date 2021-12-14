#ifndef AKTUALIZR_PROVISIONER_TEST_UTILS_H
#define AKTUALIZR_PROVISIONER_TEST_UTILS_H

#include "primary/provisioner.h"

// Test utility to run provisioning to completion and check the result
void ExpectProvisionOK(Provisioner&& provisioner);

void ExpectProvisionError(Provisioner&& provisioner, const std::string& match = "");

#endif  // AKTUALIZR_PROVISIONER_TEST_UTILS_H
