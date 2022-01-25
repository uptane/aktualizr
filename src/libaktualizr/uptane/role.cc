#include "uptane/tuf.h"

#include <sstream>

using Uptane::Role;
using Uptane::Version;

const std::string Role::ROOT = "root";
const std::string Role::SNAPSHOT = "snapshot";
const std::string Role::TARGETS = "targets";
const std::string Role::TIMESTAMP = "timestamp";
const std::string Role::OFFLINESNAPSHOT = "offline-snapshot";
const std::string Role::OFFLINEUPDATES = "offline-updates";

Role::Role(const std::string &role_name, const bool delegation) {
  std::string role_name_lower;
  std::transform(role_name.begin(), role_name.end(), std::back_inserter(role_name_lower), ::tolower);
  name_ = role_name_lower;
  if (delegation) {
    if (IsReserved(name_)) {
      throw Uptane::Exception("", "Delegated role name " + role_name + " is reserved.");
    }
    role_ = RoleEnum::kDelegation;
    name_ = role_name;
  } else if (role_name_lower == ROOT) {
    role_ = RoleEnum::kRoot;
  } else if (role_name_lower == SNAPSHOT) {
    role_ = RoleEnum::kSnapshot;
  } else if (role_name_lower == TARGETS) {
    role_ = RoleEnum::kTargets;
  } else if (role_name_lower == TIMESTAMP) {
    role_ = RoleEnum::kTimestamp;
  } else if (role_name_lower == OFFLINESNAPSHOT) {
    role_ = RoleEnum::kOfflineSnapshot;
  } else if (role_name_lower ==  OFFLINEUPDATES) {
    role_ = RoleEnum::kOfflineUpdates;
  } else {
    role_ = RoleEnum::kInvalidRole;
    name_ = "invalidrole";
  }
}

std::string Role::ToString() const { return name_; }

std::ostream &Uptane::operator<<(std::ostream &os, const Role &role) {
  os << role.ToString();
  return os;
}

std::string Version::RoleFileName(const Role &role) const {
  std::stringstream ss;
  if (version_ != Version::ANY_VERSION) {
    ss << version_ << ".";
  }
  ss << role << ".json";
  return ss.str();
}
