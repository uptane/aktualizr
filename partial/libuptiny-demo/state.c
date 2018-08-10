#include "libuptiny/state_api.h"
#include <string.h>

uptane_root_t* stored_root;

uptane_targets_t* stored_targets;

uptane_installation_state_t stored_installation_state;
bool installation_state_present = false;

uptane_root_t* state_get_root(void) {
  // TODO: deserialization
  return stored_root;
}

void state_set_root(const uptane_root_t* root) {
  // TODO: serialization
  stored_root = (uptane_root_t*) root;
}

uptane_targets_t* state_get_targets(void) {
  return stored_targets;
}

void state_set_targets(const uptane_targets_t* targets) {
  stored_targets = (uptane_targets_t*) targets;
}


const char* state_get_ecuid(void) {
  return "libuptiny_demo_secondary";
}

const char* state_get_hwid(void) {
  return "libuptiny_depo";
}

void state_set_installation_state(const uptane_installation_state_t* state) {
  memcpy(&stored_installation_state, state, sizeof(uptane_installation_state_t));
  installation_state_present = true;
}

uptane_installation_state_t* state_get_installation_state(void) {
  if(!installation_state_present) {
    return NULL;
  } else {
    return &stored_installation_state;
  }
}

crypto_hash_algorithm_t state_get_supported_hash(void) {
  return CRYPTO_HASH_SHA512;
}

void state_set_attack(uptane_attack_t attack) {
  if(!installation_state_present) {
    installation_state_present = true;
  }
  stored_installation_state.attack = attack;
}
