#include "libuptiny/state_api.h"

uptane_root_t* stored_root;

uptane_targets_t* stored_targets;

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
