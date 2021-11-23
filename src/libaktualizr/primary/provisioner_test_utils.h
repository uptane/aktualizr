
#ifndef AKTUALIZR_PROVISIONER_TEST_UTILS_H
#define AKTUALIZR_PROVISIONER_TEST_UTILS_H

// TODO documenti
void ExpectProvisionOK(Provisioner&& provisioner);

// TODO documenti
void ExpectProvisionError(Provisioner&& provisioner, const std::string& match = "");

#endif  // AKTUALIZR_PROVISIONER_TEST_UTILS_H