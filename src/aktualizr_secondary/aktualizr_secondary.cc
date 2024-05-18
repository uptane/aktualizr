#include "aktualizr_secondary.h"

#include <sys/types.h>
#include <memory>

#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

#include "crypto/keymanager.h"
#include "libaktualizr/types.h"
#include "logging/logging.h"
#include "storage/invstorage.h"
#include "update_agent.h"
#include "uptane/manifest.h"
#include "utilities/utils.h"

AktualizrSecondary::AktualizrSecondary(AktualizrSecondaryConfig config, std::shared_ptr<INvStorage> storage)
    : config_(std::move(config)),
      storage_(std::move(storage)),
      keys_(std::make_shared<KeyManager>(storage_, config_.keymanagerConfig())) {
  uptaneInitialize();
  manifest_issuer_ = std::make_shared<Uptane::ManifestIssuer>(keys_, ecu_serial_);
  registerHandlers();
}

PublicKey AktualizrSecondary::publicKey() const { return keys_->UptanePublicKey(); }

Uptane::Manifest AktualizrSecondary::getManifest() const {
  Uptane::InstalledImageInfo installed_image_info;
  Uptane::Manifest manifest;

  if (getInstalledImageInfo(installed_image_info)) {
    manifest = manifest_issuer_->assembleAndSignManifest(installed_image_info);
  }

  return manifest;
}

data::InstallationResult AktualizrSecondary::putMetadata(const Uptane::SecondaryMetadata& metadata) {
  return verifyMetadata(metadata);
}

data::InstallationResult AktualizrSecondary::install() {
  if (!pending_target_.IsValid()) {
    LOG_ERROR << "Aborting target image installation; no valid target found.";
    return data::InstallationResult(data::ResultCode::Numeric::kInternalError,
                                    "Aborting target image installation; no valid target found.");
  }

  auto target_name = pending_target_.filename();
  auto result = installPendingTarget(pending_target_);

  switch (result.result_code.num_code) {
    case data::ResultCode::Numeric::kOk: {
      storage_->saveInstalledVersion(ecu_serial_.ToString(), pending_target_, InstalledVersionUpdateMode::kCurrent, "");
      pending_target_ = Uptane::Target::Unknown();
      LOG_INFO << "The target has been successfully installed: " << target_name;
      break;
    }
    case data::ResultCode::Numeric::kNeedCompletion: {
      storage_->saveInstalledVersion(ecu_serial_.ToString(), pending_target_, InstalledVersionUpdateMode::kPending, "");
      LOG_INFO << "The target has been successfully installed, but a reboot is required to be applied: " << target_name;
      break;
    }
    default: {
      LOG_INFO << "Failed to install the target: " << target_name;
    }
  }

  return result;
}

data::InstallationResult AktualizrSecondary::verifyMetadata(const Uptane::SecondaryMetadata& metadata) {
  // 5.4.4.2. Full verification  https://uptane.github.io/uptane-standard/uptane-standard.html#metadata_verification

  // 1. Load and verify the current time or the most recent securely attested time.
  //    We trust the time that the given system/ECU provides.
  TimeStamp now(TimeStamp::Now());

  if (config_.uptane.verification_type == VerificationType::kFull) {
    // 2. Download and check the Root metadata file from the Director repository.
    // 3. NOT SUPPORTED: Download and check the Timestamp metadata file from the Director repository.
    // 4. NOT SUPPORTED: Download and check the Snapshot metadata file from the Director repository.
    // 5. Download and check the Targets metadata file from the Director repository.
    try {
      director_repo_.updateMeta(*storage_, metadata, nullptr);
    } catch (const std::exception& e) {
      LOG_ERROR << "Failed to update Director metadata: " << e.what();
      return data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed,
                                      std::string("Failed to update Director metadata: ") + e.what());
    }
  }

  // 6. Download and check the Root metadata file from the Image repository.
  // 7. Download and check the Timestamp metadata file from the Image repository.
  // 8. Download and check the Snapshot metadata file from the Image repository.
  // 9. Download and check the top-level Targets metadata file from the Image repository.
  try {
    image_repo_.updateMeta(*storage_, metadata, nullptr);
  } catch (const std::exception& e) {
    LOG_ERROR << "Failed to update Image repo metadata: " << e.what();
    return data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed,
                                    std::string("Failed to update Image repo metadata: ") + e.what());
  }

  data::InstallationResult result = findTargets();
  if (result.isSuccess()) {
    LOG_INFO << "Metadata verified, new update found.";
  }
  return result;
}

void AktualizrSecondary::initPendingTargetIfAny() {
  try {
    if (config_.uptane.verification_type == VerificationType::kFull) {
      director_repo_.checkMetaOffline(*storage_);
    }
    image_repo_.checkMetaOffline(*storage_);
  } catch (const std::exception& e) {
    LOG_INFO << "No valid metadata found in storage.";
    return;
  }

  findTargets();
}

data::InstallationResult AktualizrSecondary::findTargets() {
  std::vector<Uptane::Target> targetsForThisEcu;
  if (config_.uptane.verification_type == VerificationType::kFull) {
    // 10. Verify that Targets metadata from the Director and Image repositories match.
    if (!director_repo_.matchTargetsWithImageTargets(image_repo_.getTargets())) {
      LOG_ERROR << "Targets metadata from the Director and Image repositories do not match";
      return data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed,
                                      "Targets metadata from the Director and Image repositories do not match");
    }

    targetsForThisEcu = director_repo_.getTargets(serial(), hwID());
  } else {
    const auto& targets = image_repo_.getTargets()->targets;
    for (auto it = targets.begin(); it != targets.end(); ++it) {
      auto hwids = it->hardwareIds();
      auto found_loc = std::find(hwids.cbegin(), hwids.cend(), hwID());
      if (found_loc != hwids.end()) {
        if (!targetsForThisEcu.empty()) {
          auto previous = boost::make_optional<int>(false, 0);
          auto current = boost::make_optional<int>(false, 0);
          try {
            previous = boost::lexical_cast<int>(targetsForThisEcu[0].custom_version());
          } catch (const boost::bad_lexical_cast&) {
            LOG_TRACE << "Unable to parse Target custom version: " << targetsForThisEcu[0].custom_version();
          }
          try {
            current = boost::lexical_cast<int>(it->custom_version());
          } catch (const boost::bad_lexical_cast&) {
            LOG_TRACE << "Unable to parse Target custom version: " << it->custom_version();
          }
          if (!previous && !current) {  // NOLINT(bugprone-branch-clone)
            // No versions: add this to the vector.
          } else if (!previous) {  // NOLINT(bugprone-branch-clone)
            // Previous Target didn't have a version but this does; replace existing Targets with this.
            targetsForThisEcu.clear();
          } else if (!current) {  // NOLINT(bugprone-branch-clone)
            // Current Target doesn't have a version but previous does; ignore this.
            continue;
          } else if (previous < current) {
            // Current Target is newer; replace existing Targets with this.
            targetsForThisEcu.clear();
          } else if (previous > current) {
            // Current Target is older; ignore it.
            continue;
          } else {
            // Same version: add it to the vector.
          }
        } else {
          // First matching Target found; add it to the vector.
        }

        targetsForThisEcu.push_back(*it);
      }
    }
  }

  if (targetsForThisEcu.size() != 1) {
    LOG_ERROR << "Invalid number of targets (should be 1): " << targetsForThisEcu.size();
    return data::InstallationResult(
        data::ResultCode::Numeric::kVerificationFailed,
        "Invalid number of targets (should be 1): " + std::to_string(targetsForThisEcu.size()));
  }

  if (!isTargetSupported(targetsForThisEcu[0])) {
    LOG_ERROR << "The given target type is not supported: " << targetsForThisEcu[0].type();
    return data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed,
                                    "The given target type is not supported: " + targetsForThisEcu[0].type());
  }

  pending_target_ = targetsForThisEcu[0];
  return data::InstallationResult(data::ResultCode::Numeric::kOk, "");
}

void AktualizrSecondary::uptaneInitialize() {
  if (keys_->generateUptaneKeyPair().empty()) {
    throw std::runtime_error("Failed to generate Uptane key pair");
  }

  // from uptane/initialize.cc but we only take care of our own serial/hwid
  EcuSerials ecu_serials;

  if (storage_->loadEcuSerials(&ecu_serials)) {
    ecu_serial_ = ecu_serials[0].first;
    hardware_id_ = ecu_serials[0].second;
    return;
  }

  std::string ecu_serial_local = config_.uptane.ecu_serial;
  if (ecu_serial_local.empty()) {
    ecu_serial_local = keys_->UptanePublicKey().KeyId();
  }

  std::string ecu_hardware_id = config_.uptane.ecu_hardware_id;
  if (ecu_hardware_id.empty()) {
    ecu_hardware_id = Utils::getHostname();
    if (ecu_hardware_id.empty()) {
      throw std::runtime_error("Failed to define ECU hardware ID");
    }
  }

  ecu_serials.emplace_back(Uptane::EcuSerial(ecu_serial_local), Uptane::HardwareIdentifier(ecu_hardware_id));
  storage_->storeEcuSerials(ecu_serials);
  ecu_serial_ = ecu_serials[0].first;
  hardware_id_ = ecu_serials[0].second;

  // this is a way to find out and store a value of the target name that is installed
  // at the initial/provisioning stage and included into a device manifest
  // i.e. 'filepath' field or ["signed"]["installed_image"]["filepath"]
  // this value must match the value pushed to the backend during the bitbaking process,
  // specifically, at its OSTree push phase and is equal to
  // GARAGE_TARGET_NAME ?= "${OSTREE_BRANCHNAME}" which in turn is equal to OSTREE_BRANCHNAME ?= "${SOTA_HARDWARE_ID}"
  // therefore, by default GARAGE_TARGET_NAME == OSTREE_BRANCHNAME == SOTA_HARDWARE_ID
  // If there is no match then the backend/UI will not render/highlight currently installed version at all/correctly
  storage_->importInstalledVersions(config_.import.base_path);
}

void AktualizrSecondary::registerHandlers() {
  registerHandler(AKIpUptaneMes_PR_getInfoReq,
                  std::bind(&AktualizrSecondary::getInfoHdlr, this, std::placeholders::_1, std::placeholders::_2));

  registerHandler(AKIpUptaneMes_PR_versionReq,
                  std::bind(&AktualizrSecondary::versionHdlr, std::placeholders::_1, std::placeholders::_2));

  registerHandler(AKIpUptaneMes_PR_manifestReq,
                  std::bind(&AktualizrSecondary::getManifestHdlr, this, std::placeholders::_1, std::placeholders::_2));

  registerHandler(AKIpUptaneMes_PR_rootVerReq,
                  std::bind(&AktualizrSecondary::getRootVerHdlr, this, std::placeholders::_1, std::placeholders::_2));

  registerHandler(AKIpUptaneMes_PR_putRootReq,
                  std::bind(&AktualizrSecondary::putRootHdlr, this, std::placeholders::_1, std::placeholders::_2));

  registerHandler(AKIpUptaneMes_PR_putMetaReq2,
                  std::bind(&AktualizrSecondary::putMetaHdlr, this, std::placeholders::_1, std::placeholders::_2));

  registerHandler(AKIpUptaneMes_PR_installReq,
                  std::bind(&AktualizrSecondary::installHdlr, this, std::placeholders::_1, std::placeholders::_2));
}

MsgHandler::ReturnCode AktualizrSecondary::getInfoHdlr(Asn1Message& in_msg, Asn1Message& out_msg) const {
  (void)in_msg;
  LOG_INFO << "Received an information request message; sending requested information.";

  out_msg.present(AKIpUptaneMes_PR_getInfoResp);
  auto info_resp = out_msg.getInfoResp();

  SetString(&info_resp->ecuSerial, serial().ToString());
  SetString(&info_resp->hwId, hwID().ToString());
  info_resp->keyType = static_cast<AKIpUptaneKeyType_t>(publicKey().Type());
  SetString(&info_resp->key, publicKey().Value());

  return ReturnCode::kOk;
}

MsgHandler::ReturnCode AktualizrSecondary::versionHdlr(Asn1Message& in_msg, Asn1Message& out_msg) {
  const uint32_t version = 2;
  auto version_req = in_msg.versionReq();
  const auto primary_version = static_cast<uint32_t>(version_req->version);
  if (primary_version < version) {
    LOG_ERROR << "Primary protocol version is " << primary_version << " but Secondary version is " << version
              << "! Communication will most likely fail!";
  } else if (primary_version > version) {
    LOG_INFO << "Primary protocol version is " << primary_version << " but Secondary version is " << version
             << ". Please consider upgrading the Secondary.";
  }

  auto m = out_msg.present(AKIpUptaneMes_PR_versionResp).versionResp();
  m->version = version;

  return ReturnCode::kOk;
}

AktualizrSecondary::ReturnCode AktualizrSecondary::getManifestHdlr(Asn1Message& in_msg, Asn1Message& out_msg) const {
  (void)in_msg;
  if (last_msg_ != AKIpUptaneMes_PR_manifestReq) {
    LOG_INFO << "Received a manifest request message; sending requested manifest.";
  } else {
    LOG_DEBUG << "Received another manifest request message; sending the same manifest.";
  }

  out_msg.present(AKIpUptaneMes_PR_manifestResp);
  auto manifest_resp = out_msg.manifestResp();
  manifest_resp->manifest.present = manifest_PR_json;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  SetString(&manifest_resp->manifest.choice.json, Utils::jsonToStr(getManifest()));

  LOG_TRACE << "Manifest: \n" << getManifest();
  return ReturnCode::kOk;
}

AktualizrSecondary::ReturnCode AktualizrSecondary::getRootVerHdlr(Asn1Message& in_msg, Asn1Message& out_msg) const {
  LOG_INFO << "Received a Root version request message.";
  auto rv = in_msg.rootVerReq();
  Uptane::RepositoryType repo_type{};
  if (rv->repotype == AKRepoType_director) {
    repo_type = Uptane::RepositoryType::Director();
  } else if (rv->repotype == AKRepoType_image) {
    repo_type = Uptane::RepositoryType::Image();
  } else {
    LOG_WARNING << "Received Root version request with invalid repo type: " << rv->repotype;
    repo_type = Uptane::RepositoryType(-1);
  }

  int32_t root_version = -1;
  if (repo_type == Uptane::RepositoryType::Director()) {
    root_version = director_repo_.rootVersion();
  } else if (repo_type == Uptane::RepositoryType::Image()) {
    root_version = image_repo_.rootVersion();
  }
  LOG_DEBUG << "Current " << repo_type << " repo Root metadata version: " << root_version;

  auto m = out_msg.present(AKIpUptaneMes_PR_rootVerResp).rootVerResp();
  m->version = root_version;

  return ReturnCode::kOk;
}

AktualizrSecondary::ReturnCode AktualizrSecondary::putRootHdlr(Asn1Message& in_msg, Asn1Message& out_msg) {
  LOG_INFO << "Received a put Root request message; verifying contents...";
  auto pr = in_msg.putRootReq();
  Uptane::RepositoryType repo_type{};
  if (pr->repotype == AKRepoType_director) {
    repo_type = Uptane::RepositoryType::Director();
  } else if (pr->repotype == AKRepoType_image) {
    repo_type = Uptane::RepositoryType::Image();
  } else {
    repo_type = Uptane::RepositoryType(-1);
  }

  const std::string json = ToString(pr->json);
  LOG_DEBUG << "Received " << repo_type << " repo Root metadata:\n" << json;
  data::InstallationResult result(data::ResultCode::Numeric::kOk, "");

  if (repo_type == Uptane::RepositoryType::Director()) {
    if (config_.uptane.verification_type == VerificationType::kTuf) {
      LOG_WARNING << "Ignoring new Director Root metadata as it is unnecessary for TUF verification.";
      result =
          data::InstallationResult(data::ResultCode::Numeric::kInternalError,
                                   "Ignoring new Director Root metadata as it is unnecessary for TUF verification.");
    } else {
      try {
        director_repo_.verifyRoot(json);
        storage_->storeRoot(json, repo_type, Uptane::Version(director_repo_.rootVersion()));
        storage_->clearNonRootMeta(repo_type);
      } catch (const std::exception& e) {
        LOG_ERROR << "Failed to update Director Root metadata: " << e.what();
        result = data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed,
                                          std::string("Failed to update Director Root metadata: ") + e.what());
      }
    }
  } else if (repo_type == Uptane::RepositoryType::Image()) {
    try {
      image_repo_.verifyRoot(json);
      storage_->storeRoot(json, repo_type, Uptane::Version(image_repo_.rootVersion()));
      storage_->clearNonRootMeta(repo_type);
    } catch (const std::exception& e) {
      LOG_ERROR << "Failed to update Image repo Root metadata: " << e.what();
      result = data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed,
                                        std::string("Failed to update Image repo Root metadata: ") + e.what());
    }
  } else {
    LOG_WARNING << "Received Root version request with invalid repo type: " << pr->repotype;
    result = data::InstallationResult(
        data::ResultCode::Numeric::kInternalError,
        "Received Root version request with invalid repo type: " + std::to_string(pr->repotype));
  }

  auto m = out_msg.present(AKIpUptaneMes_PR_putRootResp).putRootResp();
  m->result = static_cast<AKInstallationResultCode_t>(result.result_code.num_code);
  SetString(&m->description, result.description);

  return ReturnCode::kOk;
}

void AktualizrSecondary::copyMetadata(Uptane::MetaBundle& meta_bundle, const Uptane::RepositoryType repo,
                                      const Uptane::Role& role, std::string& json) {
  auto key = std::make_pair(repo, role);
  if (meta_bundle.count(key) > 0) {
    LOG_WARNING << repo << " metadata in contains multiple " << role << " objects.";
    return;
  }
  meta_bundle.emplace(key, std::move(json));
}

AktualizrSecondary::ReturnCode AktualizrSecondary::putMetaHdlr(Asn1Message& in_msg, Asn1Message& out_msg) {
  LOG_INFO << "Received a put metadata request message; verifying contents...";
  auto md = in_msg.putMetaReq2();
  Uptane::MetaBundle meta_bundle;

  if (config_.uptane.verification_type == VerificationType::kFull &&
      md->directorRepo.present == directorRepo_PR_collection) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    const int director_meta_count = md->directorRepo.choice.collection.list.count;
    for (int i = 0; i < director_meta_count; i++) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-union-access)
      const AKMetaJson_t object = *md->directorRepo.choice.collection.list.array[i];
      const std::string role = ToString(object.role);
      std::string json = ToString(object.json);
      LOG_DEBUG << "Received Director repo " << role << " metadata:\n" << json;
      if (role == Uptane::Role::ROOT) {
        copyMetadata(meta_bundle, Uptane::RepositoryType::Director(), Uptane::Role::Root(), json);
      } else if (role == Uptane::Role::TARGETS) {
        copyMetadata(meta_bundle, Uptane::RepositoryType::Director(), Uptane::Role::Targets(), json);
      } else {
        LOG_WARNING << "Director metadata in unknown format:" << md->directorRepo.present;
      }
    }
  }

  if (md->imageRepo.present == imageRepo_PR_collection) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    const int image_meta_count = md->imageRepo.choice.collection.list.count;
    for (int i = 0; i < image_meta_count; i++) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-union-access)
      const AKMetaJson_t object = *md->imageRepo.choice.collection.list.array[i];
      const std::string role = ToString(object.role);
      std::string json = ToString(object.json);
      LOG_DEBUG << "Received Image repo " << role << " metadata:\n" << json;
      if (role == Uptane::Role::ROOT) {
        copyMetadata(meta_bundle, Uptane::RepositoryType::Image(), Uptane::Role::Root(), json);
      } else if (role == Uptane::Role::TIMESTAMP) {
        copyMetadata(meta_bundle, Uptane::RepositoryType::Image(), Uptane::Role::Timestamp(), json);
      } else if (role == Uptane::Role::SNAPSHOT) {
        copyMetadata(meta_bundle, Uptane::RepositoryType::Image(), Uptane::Role::Snapshot(), json);
      } else if (role == Uptane::Role::TARGETS) {
        copyMetadata(meta_bundle, Uptane::RepositoryType::Image(), Uptane::Role::Targets(), json);
      } else {
        LOG_WARNING << "Image metadata in unknown format:" << md->imageRepo.present;
      }
    }
  }

  size_t expected_items;
  if (config_.uptane.verification_type == VerificationType::kTuf) {
    expected_items = 4;
  } else {
    expected_items = 6;
  }
  if (meta_bundle.size() != expected_items) {
    LOG_WARNING << "Metadata received from Primary is incomplete. Expected size: " << expected_items
                << " Received: " << meta_bundle.size();
  }

  data::InstallationResult result = putMetadata(meta_bundle);

  auto m = out_msg.present(AKIpUptaneMes_PR_putMetaResp2).putMetaResp2();
  m->result = static_cast<AKInstallationResultCode_t>(result.result_code.num_code);
  SetString(&m->description, result.description);

  return ReturnCode::kOk;
}

AktualizrSecondary::ReturnCode AktualizrSecondary::installHdlr(Asn1Message& in_msg, Asn1Message& out_msg) {
  (void)in_msg;
  LOG_INFO << "Received an installation request message; attempting installation...";
  auto result = install();

  auto m = out_msg.present(AKIpUptaneMes_PR_installResp2).installResp2();
  m->result = static_cast<AKInstallationResultCode_t>(result.result_code.num_code);
  SetString(&m->description, result.description);

  if (data::ResultCode::Numeric::kNeedCompletion == result.result_code.num_code) {
    return ReturnCode::kRebootRequired;
  }

  return ReturnCode::kOk;
}
