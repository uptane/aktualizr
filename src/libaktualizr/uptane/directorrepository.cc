#include "directorrepository.h"

#include "fetcher.h"
#include "logging/logging.h"
#include "storage/invstorage.h"
#include "uptane/exceptions.h"
#include "utilities/utils.h"

#include <boost/filesystem.hpp>

namespace Uptane {

void DirectorRepository::resetMeta() {
  resetRoot();
  targets = Targets();
  latest_targets = Targets();
#ifdef BUILD_OFFLINE_UPDATES
  offline_snapshot = Snapshot();
#endif
}

void DirectorRepository::checkTargetsExpired(UpdateType utype) {
  if (latest_targets.isExpired(TimeStamp::Now())) {
    if (utype == UpdateType::kOffline) {
      throw Uptane::ExpiredMetadata(type.ToString(), Role::OFFLINEUPDATES);
    } else {
      throw Uptane::ExpiredMetadata(type.ToString(), Role::TARGETS);
    }
  }
}

void DirectorRepository::targetsSanityCheck(UpdateType utype) {
  //  5.4.4.6.6. If checking Targets metadata from the Director repository,
  //  verify that there are no delegations.
  if (!latest_targets.delegated_role_names_.empty()) {
    if (utype == UpdateType::kOffline) {
      throw Uptane::InvalidMetadata(type.ToString(), Role::OFFLINEUPDATES, "Found unexpected delegation.");
    } else {
      throw Uptane::InvalidMetadata(type.ToString(), Role::TARGETS, "Found unexpected delegation.");
    }
  }
  //  5.4.4.6.7. If checking Targets metadata from the Director repository,
  //  check that no ECU identifier is represented more than once.
  std::set<Uptane::EcuSerial> ecu_ids;
  for (const auto& target : targets.targets) {
    for (const auto& ecu : target.ecus()) {
      if (ecu_ids.find(ecu.first) == ecu_ids.end()) {
        ecu_ids.insert(ecu.first);
      } else {
        if (utype == UpdateType::kOffline) {
          LOG_ERROR << "ECU " << ecu.first << " appears twice in Director's Offline Targets";
          throw Uptane::InvalidMetadata(type.ToString(), Role::OFFLINEUPDATES, "Found repeated ECU ID.");
        } else {
          LOG_ERROR << "ECU " << ecu.first << " appears twice in Director's Targets";
          throw Uptane::InvalidMetadata(type.ToString(), Role::TARGETS, "Found repeated ECU ID.");
        }
      }
    }
  }
}

bool DirectorRepository::usePreviousTargets() const {
  // Don't store the new targets if they are empty and we've previously received
  // a non-empty list.
  return !targets.targets.empty() && latest_targets.targets.empty();
}

void DirectorRepository::verifyTargets(const std::string& targets_raw) {
  try {
    // Verify the signature:
    latest_targets = Targets(RepositoryType::Director(), Role::Targets(), Utils::parseJSON(targets_raw),
                             std::make_shared<MetaWithKeys>(root));
    if (!usePreviousTargets()) {
      targets = latest_targets;
    }
  } catch (const Uptane::Exception& e) {
    LOG_ERROR << "Signature verification for Director Targets metadata failed";
    throw;
  }
}

void DirectorRepository::checkMetaOffline(INvStorage& storage) {
  resetMeta();
  // Load Director Root Metadata
  {
    std::string director_root;
    if (!storage.loadLatestRoot(&director_root, RepositoryType::Director())) {
      throw Uptane::SecurityException(RepositoryType::DIRECTOR, "Could not load latest root");
    }

    initRoot(RepositoryType(RepositoryType::DIRECTOR), director_root);

    if (rootExpired()) {
      throw Uptane::ExpiredMetadata(RepositoryType::DIRECTOR, Role::ROOT);
    }
  }

  // Load Director Targets Metadata
  {
    std::string director_targets;

    if (!storage.loadNonRoot(&director_targets, RepositoryType::Director(), Role::Targets())) {
      throw Uptane::SecurityException(RepositoryType::DIRECTOR, "Could not load Targets role");
    }

    verifyTargets(director_targets);

    checkTargetsExpired(UpdateType::kOnline);

    targetsSanityCheck(UpdateType::kOnline);
  }
}

void DirectorRepository::updateMeta(INvStorage& storage, const IMetadataFetcher& fetcher) {
  // Uptane step 2 (download time) is not implemented yet.
  // Uptane step 3 (download metadata)

  // reset Director repo to initial state before starting Uptane iteration
  resetMeta();

  updateRoot(storage, fetcher, RepositoryType::Director());

  // Not supported: 3. Download and check the Timestamp metadata file from the Director repository, following the
  // procedure in Section 5.4.4.4. Not supported: 4. Download and check the Snapshot metadata file from the Director
  // repository, following the procedure in Section 5.4.4.5.

  // Update Director Targets Metadata
  {
    std::string director_targets;

    fetcher.fetchLatestRole(&director_targets, kMaxDirectorTargetsSize, RepositoryType::Director(), Role::Targets());
    int remote_version = extractVersionUntrusted(director_targets);

    int local_version;
    std::string director_targets_stored;
    if (storage.loadNonRoot(&director_targets_stored, RepositoryType::Director(), Role::Targets())) {
      local_version = extractVersionUntrusted(director_targets_stored);
      try {
        verifyTargets(director_targets_stored);
      } catch (const std::exception& e) {
        LOG_WARNING << "Unable to verify stored Director Targets metadata.";
      }
    } else {
      local_version = -1;
    }

    verifyTargets(director_targets);

    // TODO(OTA-4940): check if versions are equal but content is different. In
    // that case, the member variable targets is updated, but it isn't stored in
    // the database, which can cause some minor confusion.
    if (local_version > remote_version) {
      throw Uptane::SecurityException(RepositoryType::DIRECTOR, "Rollback attempt");
    } else if (local_version < remote_version && !usePreviousTargets()) {
      storage.storeNonRoot(director_targets, RepositoryType::Director(), Role::Targets());
    }

    checkTargetsExpired(UpdateType::kOnline);

    targetsSanityCheck(UpdateType::kOnline);
  }
}

void DirectorRepository::dropTargets(INvStorage& storage) {
  try {
    storage.clearNonRootMeta(RepositoryType::Director());
    resetMeta();
  } catch (const Uptane::Exception& ex) {
    LOG_ERROR << "Failed to reset Director Targets metadata: " << ex.what();
  }
}

bool DirectorRepository::matchTargetsWithImageTargets(
    const std::shared_ptr<const Uptane::Targets>& image_targets) const {
  // step 10 of https://uptane.github.io/papers/ieee-isto-6100.1.0.0.uptane-standard.html#rfc.section.5.4.4.2
  // TODO(OTA-4800): support delegations. Consider reusing findTargetInDelegationTree(),
  // but it would need to be moved into a common place to be resued by Primary and Secondary.
  // Currently this is only used by aktualizr-secondary, but according to the
  // Standard, "A Secondary ECU MAY elect to perform this check only on the
  // metadata for the image it will install".
  if (image_targets == nullptr) {
    return false;
  }
  const auto& image_target_array = image_targets->targets;
  const auto& director_target_array = targets.targets;

  for (const auto& director_target : director_target_array) {
    auto found_it = std::find_if(
        image_target_array.begin(), image_target_array.end(),
        [&director_target](const Target& image_target) { return director_target.MatchTarget(image_target); });

    if (found_it == image_target_array.end()) {
      return false;
    }
  }

  return true;
}

#ifdef BUILD_OFFLINE_UPDATES
void DirectorRepository::checkMetaOfflineOffUpd(INvStorage& storage) {
  resetMeta();

  // Load Director Root Metadata
  std::string director_root;
  if (!storage.loadLatestRoot(&director_root, RepositoryType::Director())) {
    throw Uptane::SecurityException(RepositoryType::DIRECTOR, "Could not load latest root");
  }

  initRoot(RepositoryType(RepositoryType::DIRECTOR), director_root);

  if (rootExpired()) {
    throw Uptane::ExpiredMetadata(RepositoryType::DIRECTOR, Role::ROOT);
  }

  // Load Director Offline-Snapshot Metadata
  std::string director_offline_snapshot;
  if (!storage.loadNonRoot(&director_offline_snapshot, RepositoryType::Director(), Role::OfflineSnapshot())) {
    throw Uptane::SecurityException(RepositoryType::DIRECTOR, "Could not load Offline Snapshot role");
  }

  verifyOfflineSnapshot(director_offline_snapshot);

  checkOfflineSnapshotExpired();

  // Load Director Offline-Updates(Targets) Metadata
  std::string director_offline_targets;
  if (!storage.loadNonRoot(&director_offline_targets, RepositoryType::Director(), Role::OfflineUpdates())) {
    throw Uptane::SecurityException(RepositoryType::DIRECTOR, "Could not load Offline Updates role");
  }

  verifyOfflineTargets(director_offline_targets, storage);

  checkTargetsExpired(UpdateType::kOffline);

  targetsSanityCheck(UpdateType::kOffline);
}

void DirectorRepository::updateMetaOffUpd(INvStorage& storage, const OfflineUpdateFetcher& fetcher) {
  // reset Director repo to initial state before starting Uptane iteration
  resetMeta();

  // PURE-2 step 2
  updateRoot(storage, fetcher, RepositoryType::Director());

  // Update Director Offline Snapshot Metadata
  // PURE-2 step 3(i)
  std::string director_offline_snapshot;
  fetcher.fetchLatestRole(&director_offline_snapshot, kMaxSnapshotSize, RepositoryType::Director(),
                          Role::OfflineSnapshot());
  const int fetched_version = extractVersionUntrusted(director_offline_snapshot);

  int local_version;
  std::string director_offline_snapshot_stored;
  if (storage.loadNonRoot(&director_offline_snapshot_stored, RepositoryType::Director(), Role::OfflineSnapshot())) {
    local_version = extractVersionUntrusted(director_offline_snapshot_stored);
  } else {
    local_version = -1;
  }

  if (local_version < fetched_version) {
    verifyOfflineSnapshot(director_offline_snapshot, director_offline_snapshot_stored);
    storage.storeNonRoot(director_offline_snapshot, RepositoryType::Director(), Role::OfflineSnapshot());
  } else {
    // Not required by PURE-2 but does not hurt to verify stored offline snapshot
    verifyOfflineSnapshot(director_offline_snapshot_stored, director_offline_snapshot_stored);
  }

  // PURE-2 step 3(iv)
  checkOfflineSnapshotExpired();

  // TODO: [OFFUPD] This access to the file here may need a review when this method is called for a secondary.
  // Update Director Offline Updates(Targets) Metadata
  // PURE-2 step 4
  boost::filesystem::path offline_target_file;
  std::string offline_target_name;
  int offline_snapshot_version = -1;
  bool found = false;
  for (const auto& role_name : offline_snapshot.role_names()) {
    std::string filename = role_name + ".json";
    offline_target_file = fetcher.getMetadataPath() / "director" / filename;
    if (boost::filesystem::exists(offline_target_file)) {
      offline_snapshot_version = offline_snapshot.role_version(Role(role_name, !Role::IsReserved(role_name)));
      found = true;
      offline_target_name = role_name;
      break;
    }
  }

  if (!found) {
    throw Uptane::SecurityException(RepositoryType::DIRECTOR, "Could not find any valid offline updates metadata file");
  }

  // PURE-2 step 4(i)
  std::string director_offline_targets;

  // We abuse the Delegation role as a way to hold the offline target filename for the fetcher.
  // TODO: Try to handle this in a less "hack-ish" way later.
  Uptane::Role offline_target_role = Uptane::Role::Delegation(offline_target_name);
  fetcher.fetchLatestRole(&director_offline_targets, kMaxDirectorTargetsSize, RepositoryType::Director(),
                          offline_target_role);

  int offline_targets_version = Utils::parseJSON(director_offline_targets)["signed"]["version"].asInt();
  if (offline_targets_version != offline_snapshot_version) {
    throw Uptane::VersionMismatch(RepositoryType::DIRECTOR, Uptane::Role::OFFLINEUPDATES);
  }

  verifyOfflineTargets(director_offline_targets, storage);
  if (!usePreviousTargets()) {
    storage.storeNonRoot(director_offline_targets, RepositoryType::Director(), Role::OfflineUpdates());
  }

  // PURE-2 step 4(iii)
  checkTargetsExpired(UpdateType::kOffline);

  // PURE-2 step 4(iv)
  targetsSanityCheck(UpdateType::kOffline);
}

void DirectorRepository::verifyOfflineSnapshot(const std::string& snapshot_raw_new,
                                               const std::string& snapshot_raw_old) {
  // PURE-2 step 3(ii)
  try {
    offline_snapshot = Snapshot(RepositoryType::Image(), Uptane::Role::OfflineSnapshot(),
                                Utils::parseJSON(snapshot_raw_new), std::make_shared<MetaWithKeys>(root));
  } catch (const Exception& e) {
    LOG_ERROR << "Signature verification for Offline Snapshot metadata failed";
    throw;
  }

  // PURE-2 step 3(iii)
  Json::Value target_list_new = Utils::parseJSON(snapshot_raw_new)["signed"]["meta"];
  Json::Value target_list_old = Utils::parseJSON(snapshot_raw_old)["signed"]["meta"];
  if (target_list_old.isObject()) {
    for (auto next = target_list_new.begin(); next != target_list_new.end(); ++next) {
      for (auto old = target_list_old.begin(); old != target_list_old.end(); ++old) {
        if (next.key().asString() == old.key().asString()) {
          if ((*old)["version"].asInt() > (*next)["version"].asInt()) {
            throw Uptane::SecurityException(RepositoryType::DIRECTOR, "Rollback attempt");
          }
          break;
        }
      }
    }
  }
}

void DirectorRepository::checkOfflineSnapshotExpired() {
  if (offline_snapshot.isExpired(TimeStamp::Now())) {
    throw Uptane::ExpiredMetadata(type.ToString(), Role::OFFLINESNAPSHOT);
  }
}

void DirectorRepository::verifyOfflineTargets(const std::string& targets_raw, INvStorage& storage) {
  // PURE-2 step 4(ii)
  try {
    latest_targets = Targets(RepositoryType::Director(), Role::OfflineUpdates(), Utils::parseJSON(targets_raw),
                             std::make_shared<MetaWithKeys>(root));
    transformOfflineTargets(storage);
    if (!usePreviousTargets()) {
      targets = latest_targets;
    }
  } catch (const Uptane::Exception& e) {
    LOG_ERROR << "Signature verification for Director Targets metadata failed";
    throw;
  }
}

void DirectorRepository::transformOfflineTargets(INvStorage& storage) {
  // [OFFUPD] Not required by PURE-2, but done to make future operations easier.
  // Since offline update targets don't have an ecu-serial -> hwid map we assume one.
  // We do so by matching hwid in each target to a hwid on the system.
  // TODO: This method may not be foolproof should check and see if this causes issues.

  EcuSerials serials;
  if (!storage.getEcuSerialsForHwId(&serials) || serials.empty()) {
    throw std::runtime_error("Unable to load ECU serials");
  }

  for (Uptane::Target& target : latest_targets.targets) {
    std::vector<Uptane::HardwareIdentifier> hwids = target.hardwareIds();
    for (Uptane::HardwareIdentifier& hwid : hwids) {
      for (const auto& s : serials) {
        Uptane::EcuSerial serialNum = s.first;
        Uptane::HardwareIdentifier hw_id = s.second;
        if (hwid == hw_id) {
          std::pair<Uptane::EcuSerial, Uptane::HardwareIdentifier> ecuPair(serialNum, hw_id);
          target.InsertEcu(ecuPair);
        }
      }
    }
  }
}
#endif

}  // namespace Uptane
