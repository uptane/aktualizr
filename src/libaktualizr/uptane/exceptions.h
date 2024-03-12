#ifndef UPTANE_EXCEPTIONS_H_
#define UPTANE_EXCEPTIONS_H_

#include <stdexcept>
#include <string>
#include <utility>

namespace Uptane {

class Exception : public std::logic_error {
 public:
  Exception(std::string reponame, const std::string& what_arg)
      : std::logic_error(what_arg.c_str()), reponame_(std::move(reponame)) {}
  virtual std::string getName() const { return reponame_; };

 protected:
  std::string reponame_;
};

class MetadataFetchFailure : public Exception {
 public:
  MetadataFetchFailure(const std::string& reponame, const std::string& role)
      : Exception(reponame, std::string("Failed to fetch role ") + role + " in " + reponame + " repository.") {}
};

class SecurityException : public Exception {
 public:
  SecurityException(const std::string& reponame, const std::string& what_arg) : Exception(reponame, what_arg) {}
};

class TargetContentMismatch : public Exception {
 public:
  explicit TargetContentMismatch(const std::string& targetname)
      : Exception(targetname, "Director Target filename matches currently installed version, but content differs.") {}
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
  IllegalThreshold(const std::string& reponame, const std::string& what_arg) : Exception(reponame, what_arg) {}
};

class MissingRepo : public Exception {
 public:
  explicit MissingRepo(const std::string& reponame) : Exception(reponame, "The " + reponame + " repo is missing.") {}
};

class UnmetThreshold : public Exception {
 public:
  UnmetThreshold(const std::string& reponame, const std::string& role)
      : Exception(reponame, "The " + role + " metadata had an unmet threshold.") {}
};

class ExpiredMetadata : public Exception {
 public:
  ExpiredMetadata(const std::string& reponame, const std::string& role)
      : Exception(reponame, "The " + role + " metadata was expired.") {}
};

class InvalidMetadata : public Exception {
 public:
  InvalidMetadata(const std::string& reponame, const std::string& role, const std::string& reason)
      : Exception(reponame, "The " + role + " metadata failed to parse: " + reason) {}
};

class TargetMismatch : public Exception {
 public:
  explicit TargetMismatch(const std::string& targetname)
      : Exception(targetname, "The target metadata in the Image and Director repos do not match.") {}
};

class NonUniqueSignatures : public Exception {
 public:
  NonUniqueSignatures(const std::string& reponame, const std::string& role)
      : Exception(reponame, "The role " + role + " had non-unique signatures.") {}
};

class BadKeyId : public Exception {
 public:
  explicit BadKeyId(const std::string& reponame) : Exception(reponame, "A key has an incorrect associated key ID") {}
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
  explicit RootRotationError(const std::string& reponame)
      : Exception(reponame, "Version in Root metadata does not match its expected value.") {}
};

class VersionMismatch : public Exception {
 public:
  VersionMismatch(const std::string& reponame, const std::string& role)
      : Exception(reponame, "The version of role " + role + " does not match the entry in Snapshot metadata.") {}
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
  explicit LocallyAborted(const std::string& reponame) : Exception(reponame, "Update was aborted on the client") {}
};

}  // namespace Uptane

#endif
