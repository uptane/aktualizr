#ifndef UPTANE_EXCEPTIONS_H_
#define UPTANE_EXCEPTIONS_H_

#include <stdexcept>
#include <string>
#include <utility>
#include "libaktualizr/types.h"
#include "uptane/tuf.h"

namespace Uptane {

class Exception : public std::logic_error {
 public:
  Exception(RepositoryType repo_type, const std::string& what_arg)
      : std::logic_error(what_arg.c_str()), subject_{repo_type.ToString()} {}
  Exception(std::string subject, const std::string& what_arg)
      : std::logic_error(what_arg.c_str()), subject_{std::move(subject)} {}
  [[nodiscard]] virtual std::string getName() const { return subject_; };

 protected:
  std::string subject_;
};

class MetadataFetchFailure : public Exception {
 public:
  MetadataFetchFailure(RepositoryType repo_type, const std::string& role)
      : Exception(repo_type,
                  std::string("Failed to fetch role ") + role + " in " + repo_type.ToString() + " repository.") {}
};

class SecurityException : public Exception {
 public:
  SecurityException(RepositoryType repo_type, const std::string& what_arg) : Exception(repo_type, what_arg) {}
};

class TargetContentMismatch : public Exception {
 public:
  explicit TargetContentMismatch(const std::string& targetname)
      : Exception(RepositoryType::Director(), "Director Target filename " + targetname +
                                                  " matches currently installed version, but content differs.") {}
};

class TargetHashMismatch : public Exception {
 public:
  explicit TargetHashMismatch(const std::string& targetname)
      : Exception(targetname, "The target's calculated hash did not match the hash in the metadata.") {}
};

class OversizedTarget : public Exception {
 public:
  explicit OversizedTarget(const std::string& reponame)
      : Exception(reponame, "The target's size was greater than the size in the metadata.") {}
};

class IllegalThreshold : public Exception {
 public:
  IllegalThreshold(RepositoryType repo_type, const std::string& what_arg) : Exception(repo_type, what_arg) {}
};

class UnmetThreshold : public Exception {
 public:
  UnmetThreshold(RepositoryType repo_type, const std::string& role)
      : Exception(repo_type, "The " + role + " metadata had an unmet threshold.") {}
};

class ExpiredMetadata : public Exception {
 public:
  ExpiredMetadata(RepositoryType repo_type, const std::string& role)
      : Exception(repo_type, "The " + role + " metadata was expired.") {}
};

class InvalidMetadata : public Exception {
 public:
  InvalidMetadata(RepositoryType repo_type, const std::string& role, const std::string& reason)
      : Exception(repo_type, "The " + role + " metadata failed to parse: " + reason) {}
  explicit InvalidMetadata(const std::string& reason) : Exception("", "The metadata failed to parse: " + reason) {}
};

class TargetMismatch : public Exception {
 public:
  explicit TargetMismatch(const std::string& targetname)
      : Exception(targetname, "The target metadata in the Image and Director repos do not match.") {}
};

class NonUniqueSignatures : public Exception {
 public:
  NonUniqueSignatures(RepositoryType repo_type, const std::string& role)
      : Exception(repo_type, "The role " + role + " had non-unique signatures.") {}
};

class BadKeyId : public Exception {
 public:
  explicit BadKeyId(RepositoryType repo_type) : Exception(repo_type, "A key has an incorrect associated key ID") {}
};

class BadEcuId : public Exception {
 public:
  explicit BadEcuId(const std::string& reponame)
      : Exception(reponame, "The target had an ECU ID that did not match the client's configured ECU ID.") {}
};

class BadHardwareId : public Exception {
 public:
  explicit BadHardwareId(const std::string& reponame)
      : Exception(reponame, "The target had a hardware ID that did not match the client's configured hardware ID.") {}
};

class RootRotationError : public Exception {
 public:
  explicit RootRotationError(RepositoryType repo_type)
      : Exception(repo_type, "Version in Root metadata does not match its expected value.") {}
};

class VersionMismatch : public Exception {
 public:
  VersionMismatch(RepositoryType repo_type, const std::string& role)
      : Exception(repo_type, "The version of role " + role + " does not match the entry in Snapshot metadata.") {}
};

class DelegationHashMismatch : public Exception {
 public:
  explicit DelegationHashMismatch(const std::string& delegation_name)
      : Exception("image", "The calculated hash of delegated role " + delegation_name +
                               " did not match the hash in the metadata.") {}
};

class DelegationMissing : public Exception {
 public:
  explicit DelegationMissing(const std::string& delegation_name)
      : Exception("image", "The delegated role " + delegation_name + " is missing.") {}
};

class InvalidTarget : public Exception {
 public:
  explicit InvalidTarget(const std::string& reponame)
      : Exception(reponame, "The target had a non-OSTree package that can not be installed on an OSTree system.") {}
};

class LocallyAborted : public Exception {
 public:
  explicit LocallyAborted(RepositoryType repo_type) : Exception(repo_type, "Update was aborted on the client") {}
};

}  // namespace Uptane

#endif
