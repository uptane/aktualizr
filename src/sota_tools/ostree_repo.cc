#include "ostree_repo.h"

#include "logging/logging.h"

// NOLINTNEXTLINE(modernize-avoid-c-arrays, cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays)
OSTreeObject::ptr OSTreeRepo::GetObject(const uint8_t sha256[32], const OstreeObjectType type) const {
  return GetObject(OSTreeHash(sha256), type);
}

OSTreeObject::ptr OSTreeRepo::GetObject(const OSTreeHash hash, const OstreeObjectType type) const {
  // If we've already seen this object, return another pointer to it
  otable::const_iterator obj_it = ObjectTable.find(hash);
  if (obj_it != ObjectTable.cend()) {
    return obj_it->second;
  }

  OSTreeObject::ptr object;

  for (int i = 0; i < 3; ++i) {
    if (i > 0) {
      LOG_WARNING << "OSTree hash " << hash << " not found. Retrying (attempt " << i << " of 3)";
    }
    if (type != OSTREE_OBJECT_TYPE_UNKNOWN) {
      if (CheckForObject(hash, type, &object)) {
        return object;
      }
    } else {
      // If we don't know the type for any reason, try the object types we know
      // about.
      if (CheckForObject(hash, OSTREE_OBJECT_TYPE_FILE, &object)) {
        return object;
      }
      if (CheckForObject(hash, OSTREE_OBJECT_TYPE_DIR_META, &object)) {
        return object;
      }
      if (CheckForObject(hash, OSTREE_OBJECT_TYPE_DIR_TREE, &object)) {
        return object;
      }
      if (CheckForObject(hash, OSTREE_OBJECT_TYPE_COMMIT, &object)) {
        return object;
      }
    }
  }
  // We don't already have the object, and can't fetch it after a few retries => fail
  throw OSTreeObjectMissing(hash);
}

bool OSTreeRepo::CheckForObject(const OSTreeHash &hash, OstreeObjectType type, OSTreeObject::ptr *object_out) const {
  boost::filesystem::path path("objects");
  path /= GetPathForHash(hash, type);
  if (FetchObject(path)) {
    auto object = OSTreeObject::ptr(new OSTreeObject(*this, hash, type));
    ObjectTable[hash] = object;
    *object_out = object;
    LOG_DEBUG << "Fetched OSTree object " << path;
    return true;
  }
  return false;
}

/**
 * Get the relative path on disk (or TreeHub) for an object.
 * When an object has been successfully fetched, it will be on disk at
 * <code>root() / GetPathForHash()</code>
 * @param hash
 * @param type
 * @return
 */
boost::filesystem::path OSTreeRepo::GetPathForHash(OSTreeHash hash, OstreeObjectType type) {
  std::string objpath = hash.string().insert(2, 1, '/');
  switch (type) {
    case OstreeObjectType::OSTREE_OBJECT_TYPE_FILE:
      objpath += ".filez";
      break;
    case OstreeObjectType::OSTREE_OBJECT_TYPE_DIR_TREE:
      objpath += ".dirtree";
      break;
    case OstreeObjectType::OSTREE_OBJECT_TYPE_DIR_META:
      objpath += ".dirmeta";
      break;
    case OstreeObjectType::OSTREE_OBJECT_TYPE_COMMIT:
      objpath += ".commit";
      break;
    default:
      throw OSTreeUnsupportedObjectType(type);
  }
  return boost::filesystem::path(objpath);
}