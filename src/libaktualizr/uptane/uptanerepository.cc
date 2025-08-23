#include "uptane/uptanerepository.h"

#include <boost/algorithm/string/trim.hpp>

#include "fetcher.h"
#include "logging/logging.h"
#include "storage/invstorage.h"
#include "uptane/exceptions.h"
#include "utilities/utils.h"

namespace Uptane {

void RepositoryCommon::initRoot(RepositoryType repo_type, const std::string& root_raw) {
  try {
    root = Root(type, Utils::parseJSON(root_raw));        // initialization and format check
    root = Root(type, Utils::parseJSON(root_raw), root);  // signature verification against itself
  } catch (const std::exception& e) {
    LOG_ERROR << "Loading initial " << repo_type << " Root metadata failed: " << e.what();
    throw;
  }
}

void RepositoryCommon::verifyRoot(const std::string& root_raw) {
  try {
    int prev_version = rootVersion();
    // 5.4.4.3.2.3. Version N+1 of the Root metadata file MUST have been signed
    // by the following: (1) a threshold of keys specified in the latest Root
    // metadata file (version N), and (2) a threshold of keys specified in the
    // new Root metadata file being validated (version N+1).
    root = Root(type, Utils::parseJSON(root_raw), root);  // double signature verification
    // 5.4.4.3.2.4. The version number of the latest Root metadata file (version
    // N) must be less than or equal to the version number of the new Root
    // metadata file (version N+1). NOTE: we do not accept an equal version
    // number. It must increment.
    if (root.version() != prev_version + 1) {
      LOG_ERROR << "Version " << root.version() << " in Root metadata doesn't match the expected value "
                << prev_version + 1;
      throw Uptane::RootRotationError(type);
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "Signature verification for Root metadata failed: " << e.what();
    throw;
  }
}

void RepositoryCommon::resetRoot() { root = Root(Root::Policy::kAcceptAll); }

void RepositoryCommon::updateRoot(INvStorage& storage, const IMetadataFetcher& fetcher,
                                  const RepositoryType repo_type) {
  // 5.4.4.3.1. Load the previous Root metadata file.
  {
    std::string root_raw;
    if (storage.loadLatestRoot(&root_raw, repo_type)) {
      initRoot(repo_type, root_raw);
    } else {
      fetcher.fetchRole(&root_raw, kMaxRootSize, repo_type, Role::Root(), Version(1));
      initRoot(repo_type, root_raw);
      storage.storeRoot(root_raw, repo_type, Version(1));
    }
  }

  // 5.4.4.3.2. Update to the latest Root metadata file.
  for (int version = rootVersion() + 1;; ++version) {
    // 5.4.4.3.2.2. Try downloading a new version N+1 of the Root metadata file.
    std::string root_raw;
    try {
      fetcher.fetchRole(&root_raw, kMaxRootSize, repo_type, Role::Root(), Version(version));
    } catch (const std::exception& e) {
      break;
    }

    verifyRoot(root_raw);

    // 5.4.4.3.2.5. Set the latest Root metadata file to the new Root metadata
    // file.
    storage.storeRoot(root_raw, repo_type, Version(version));
    storage.clearNonRootMeta(repo_type);
  }

  // 5.4.4.3.3. Check that the current (or latest securely attested) time is
  // lower than the expiration timestamp in the latest Root metadata file.
  // (Checks for a freeze attack.)
  if (rootExpired()) {
    throw Uptane::ExpiredMetadata(repo_type, Role::ROOT);
  }
}

}  // namespace Uptane
