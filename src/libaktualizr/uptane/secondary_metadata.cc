#include "secondary_metadata.h"

namespace Uptane {

SecondaryMetadata::SecondaryMetadata(MetaBundle meta_bundle_in) : meta_bundle_(std::move(meta_bundle_in)) {
  try {
    director_root_version_ =
        Version(extractVersionUntrusted(getMetaFromBundle(meta_bundle_, RepositoryType::Director(), Role::Root())));
  } catch (const std::exception& e) {
    LOG_DEBUG << "Failed to read Director Root version: " << e.what();
  }
  try {
    image_root_version_ =
        Version(extractVersionUntrusted(getMetaFromBundle(meta_bundle_, RepositoryType::Image(), Role::Root())));
  } catch (const std::exception& e) {
    LOG_DEBUG << "Failed to read Image repo Root version: " << e.what();
  }
}

void SecondaryMetadata::fetchRole(std::string* result, int64_t maxsize, RepositoryType repo, const Role& role,
                                  Version version) const {
  (void)maxsize;
  getRoleMetadata(result, repo, role, version);
}

void SecondaryMetadata::fetchLatestRole(std::string* result, int64_t maxsize, RepositoryType repo,
                                        const Role& role) const {
  (void)maxsize;
  getRoleMetadata(result, repo, role, Version());
}

void SecondaryMetadata::getRoleMetadata(std::string* result, const RepositoryType& repo, const Role& role,
                                        Version version) const {
  if (role == Role::Root() && version != Version()) {
    // If requesting a Root version beyond what we have available, fail as
    // expected. If requesting a version before what is available, just use what
    // is available, since root rotation isn't supported here.
    if (repo == RepositoryType::Director() && director_root_version_ < version) {
      LOG_DEBUG << "Requested Director Root version " << version << " but only version " << director_root_version_
                << " is available.";
      throw std::runtime_error("Metadata not found");
    } else if (repo == RepositoryType::Image() && image_root_version_ < version) {
      LOG_DEBUG << "Requested Image repo Root version " << version << " but only version " << image_root_version_
                << " is available.";
      throw std::runtime_error("Metadata not found");
    }
  }

  *result = getMetaFromBundle(meta_bundle_, repo, role);
}

}  // namespace Uptane
