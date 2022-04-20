#ifndef SOTA_CLIENT_TOOLS_OSTREE_REPO_H_
#define SOTA_CLIENT_TOOLS_OSTREE_REPO_H_

#include <map>
#include <string>

#include <boost/filesystem/path.hpp>

#include "garage_common.h"
#include "ostree_hash.h"
#include "ostree_object.h"

class OSTreeRef;

/**
 * A source repository to read OSTree objects from. This can be either a directory
 * on disk, or a URL in the garage-deploy case.
 */
class OSTreeRepo {
 public:
  using ptr = std::shared_ptr<OSTreeRepo>;
  OSTreeRepo() = default;
  // Non-copyable, Non-movable
  OSTreeRepo(const OSTreeRepo&) = delete;
  OSTreeRepo(OSTreeRepo&&) = delete;
  OSTreeRepo& operator=(const OSTreeRepo&) = delete;
  OSTreeRepo& operator=(OSTreeRepo&&) = delete;

  virtual ~OSTreeRepo() = default;
  virtual bool LooksValid() const = 0;
  virtual boost::filesystem::path root() const = 0;
  virtual OSTreeRef GetRef(const std::string& refname) const = 0;

  OSTreeObject::ptr GetObject(OSTreeHash hash, OstreeObjectType type) const;
  // NOLINTNEXTLINE(modernize-avoid-c-arrays, cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays)
  OSTreeObject::ptr GetObject(const uint8_t sha256[32], OstreeObjectType type) const;

  static boost::filesystem::path GetPathForHash(OSTreeHash hash, OstreeObjectType type);

 protected:
  /**
   * Look for an object with a given path, downloading it if necessary and
   * possible.
   * For OSTreeDirRepo, this is a simple check to see if the file exists on
   * disk. OSTreeHttpRepo will attempt to fetch the file from the remote
   * server to a temporary directory (if it hasn't already been fetched).
   * In either case, the following post-conditions hold
   * FetchObject() returns false => The object is not available at all
   * FetchObject() returns true => The object is on the local file system.
   * */
  virtual bool FetchObject(const boost::filesystem::path& path) const = 0;

  bool CheckForObject(const OSTreeHash& hash, OstreeObjectType type, OSTreeObject::ptr* object) const;

  using otable = std::map<OSTreeHash, OSTreeObject::ptr>;
  mutable otable ObjectTable;  // Makes sure that the same commit object is not added twice
};

/**
 * Thrown by GetObject when the object requested is not present in the
 * repository.
 */
class OSTreeObjectMissing : std::exception {
 public:
  explicit OSTreeObjectMissing(const OSTreeHash _missing_object) : missing_object_(_missing_object) {}

  const char* what() const noexcept override { return "OSTree repository is missing an object"; }

  OSTreeHash missing_object() const { return missing_object_; }

 private:
  OSTreeHash missing_object_;
};

class OSTreeUnsupportedObjectType : std::exception {
 public:
  explicit OSTreeUnsupportedObjectType(OstreeObjectType bad_type) : bad_type_(bad_type) {}

  const char* what() const noexcept override { return "Unknown OstreeObjectType"; }

  OstreeObjectType bad_type() const { return bad_type_; }

 private:
  OstreeObjectType bad_type_;
};

// vim: set tabstop=2 shiftwidth=2 expandtab:
#endif  // SOTA_CLIENT_TOOLS_OSTREE_REPO_H_
