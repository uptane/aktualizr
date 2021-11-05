#include "managedsecondary.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/algorithm/hex.hpp>
#include <boost/filesystem.hpp>

#include "crypto/crypto.h"
#include "logging/logging.h"
#include "storage/invstorage.h"
#include "uptane/directorrepository.h"
#include "uptane/imagerepository.h"
#include "uptane/manifest.h"
#include "uptane/tuf.h"
#include "utilities/exceptions.h"
#include "utilities/fault_injection.h"
#include "utilities/utils.h"

namespace Primary {

ManagedSecondary::ManagedSecondary(Primary::ManagedSecondaryConfig sconfig_in) : sconfig(std::move(sconfig_in)) {
  struct stat st {};
  if (!boost::filesystem::is_directory(sconfig.metadata_path)) {
    Utils::createDirectories(sconfig.metadata_path, S_IRWXU);
  }
  if (stat(sconfig.metadata_path.c_str(), &st) < 0) {
    throw std::runtime_error(std::string("Could not check metadata directory permissions: ") + std::strerror(errno));
  }
  if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
    throw std::runtime_error("Secondary metadata directory has unsafe permissions");
  }

  if (!boost::filesystem::is_directory(sconfig.full_client_dir)) {
    Utils::createDirectories(sconfig.full_client_dir, S_IRWXU);
  }
  if (stat(sconfig.full_client_dir.c_str(), &st) < 0) {
    throw std::runtime_error(std::string("Could not check Secondary storage directory permissions: ") +
                             std::strerror(errno));
  }
  if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
    throw std::runtime_error("Secondary storage directory has unsafe permissions");
  }

  std::string public_key_string;
  if (!loadKeys(&public_key_string, &private_key)) {
    if (!Crypto::generateKeyPair(sconfig.key_type, &public_key_string, &private_key)) {
      LOG_ERROR << "Could not generate RSA keys for secondary " << ManagedSecondary::getSerial() << "@"
                << sconfig.ecu_hardware_id;
      throw std::runtime_error("Unable to generate secondary RSA keys");
    }
  }
  public_key_ = PublicKey(public_key_string, sconfig.key_type);

  storeKeys(public_key_.Value(), private_key);

  storage_config_.path = sconfig.full_client_dir;
  storage_ = INvStorage::newStorage(storage_config_);

  director_repo_ = std_::make_unique<Uptane::DirectorRepository>();
  image_repo_ = std_::make_unique<Uptane::ImageRepository>();

  try {
    director_repo_->checkMetaOffline(*storage_);
    image_repo_->checkMetaOffline(*storage_);
  } catch (const std::exception &e) {
    LOG_INFO << "No valid metadata found in storage.";
  }
}

ManagedSecondary::~ManagedSecondary() {}  // NOLINT(modernize-use-equals-default, hicpp-use-equals-default)

data::InstallationResult ManagedSecondary::putMetadata(const Uptane::Target &target) {
  detected_attack = "";

  Uptane::MetaBundle bundle;
  if (!secondary_provider_->getMetadata(&bundle, target)) {
    return data::InstallationResult(data::ResultCode::Numeric::kInternalError,
                                    "Unable to load stored metadata from Primary");
  }
  Uptane::SecondaryMetadata metadata(bundle);

  // 2. Download and check the Root metadata file from the Director repository.
  // 3. NOT SUPPORTED: Download and check the Timestamp metadata file from the Director repository.
  // 4. NOT SUPPORTED: Download and check the Snapshot metadata file from the Director repository.
  // 5. Download and check the Targets metadata file from the Director repository.
  try {
    director_repo_->updateMeta(*storage_, metadata);
  } catch (const std::exception &e) {
    detected_attack = std::string("Failed to update Director metadata: ") + e.what();
    LOG_ERROR << detected_attack;
    return data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed, detected_attack);
  }

  // 6. Download and check the Root metadata file from the Image repository.
  // 7. Download and check the Timestamp metadata file from the Image repository.
  // 8. Download and check the Snapshot metadata file from the Image repository.
  // 9. Download and check the top-level Targets metadata file from the Image repository.
  try {
    image_repo_->updateMeta(*storage_, metadata);
  } catch (const std::exception &e) {
    detected_attack = std::string("Failed to update Image repo metadata: ") + e.what();
    LOG_ERROR << detected_attack;
    return data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed, detected_attack);
  }

  // 10. Verify that Targets metadata from the Director and Image repositories match.
  if (!director_repo_->matchTargetsWithImageTargets(image_repo_->getTargets())) {
    detected_attack = "Targets metadata from the Director and Image repositories do not match";
    LOG_ERROR << detected_attack;
    return data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed, detected_attack);
  }

  return data::InstallationResult(data::ResultCode::Numeric::kOk, "");
}

int ManagedSecondary::getRootVersion(const bool director) const {
  if (director) {
    return director_repo_->rootVersion();
  }
  return image_repo_->rootVersion();
}

data::InstallationResult ManagedSecondary::putRoot(const std::string &root, const bool director) {
  const Uptane::RepositoryType repo_type =
      (director) ? Uptane::RepositoryType::Director() : Uptane::RepositoryType::Image();
  const int prev_version = getRootVersion(director);

  LOG_DEBUG << "Updating " << repo_type << " Root with current version " << std::to_string(prev_version) << ": "
            << root;

  if (director) {
    try {
      director_repo_->verifyRoot(root);
    } catch (const std::exception &e) {
      detected_attack = "Failed to update Director Root from version " + std::to_string(prev_version) + ": " + e.what();
      LOG_ERROR << detected_attack;
      return data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed, detected_attack);
    }
    storage_->storeRoot(root, repo_type, Uptane::Version(director_repo_->rootVersion()));
    storage_->clearNonRootMeta(repo_type);
  } else {
    try {
      image_repo_->verifyRoot(root);
    } catch (const std::exception &e) {
      detected_attack = "Failed to update Image Root from version " + std::to_string(prev_version) + ": " + e.what();
      LOG_ERROR << detected_attack;
      return data::InstallationResult(data::ResultCode::Numeric::kVerificationFailed, detected_attack);
    }
    storage_->storeRoot(root, repo_type, Uptane::Version(image_repo_->rootVersion()));
    storage_->clearNonRootMeta(repo_type);
  }

  return data::InstallationResult(data::ResultCode::Numeric::kOk, "");
}

data::InstallationResult ManagedSecondary::sendFirmware(const Uptane::Target &target) {
  (void)target;
  return data::InstallationResult(data::ResultCode::Numeric::kOk, "");
}

data::InstallationResult ManagedSecondary::install(const Uptane::Target &target) {
  // TODO: check that the target is actually valid.
  auto str = secondary_provider_->getTargetFileHandle(target);
  std::ofstream out_file(sconfig.firmware_path.string(), std::ios::binary);
  out_file << str.rdbuf();
  str.close();
  out_file.close();

  Utils::writeFile(sconfig.target_name_path, target.filename());
  return data::InstallationResult(data::ResultCode::Numeric::kOk, "");
}

Uptane::Manifest ManagedSecondary::getManifest() const {
  Uptane::InstalledImageInfo firmware_info;
  if (!getFirmwareInfo(firmware_info)) {
    return Json::Value(Json::nullValue);
  }

  Json::Value manifest = Uptane::ManifestIssuer::assembleManifest(firmware_info, getSerial());
  // consider updating Uptane::ManifestIssuer functionality to fulfill the given use-case
  // and removing the following code from here so we encapsulate manifest generation
  // and signing functionality in one place
  manifest["attacks_detected"] = detected_attack;

  Json::Value signed_ecu_version;

  std::string b64sig = Utils::toBase64(Crypto::RSAPSSSign(nullptr, private_key, Utils::jsonToCanonicalStr(manifest)));
  Json::Value signature;
  signature["method"] = "rsassa-pss";
  signature["sig"] = b64sig;

  signature["keyid"] = public_key_.KeyId();
  signed_ecu_version["signed"] = manifest;
  signed_ecu_version["signatures"] = Json::Value(Json::arrayValue);
  signed_ecu_version["signatures"].append(signature);

  return signed_ecu_version;
}

bool ManagedSecondary::getFirmwareInfo(Uptane::InstalledImageInfo &firmware_info) const {
  std::string content;

  if (!boost::filesystem::exists(sconfig.target_name_path) || !boost::filesystem::exists(sconfig.firmware_path)) {
    firmware_info.name = std::string("noimage");
    content = "";
  } else {
    firmware_info.name = Utils::readFile(sconfig.target_name_path.string());
    content = Utils::readFile(sconfig.firmware_path.string());
  }
  firmware_info.hash = Uptane::ManifestIssuer::generateVersionHashStr(content);
  firmware_info.len = content.size();

  return true;
}

void ManagedSecondary::storeKeys(const std::string &pub_key, const std::string &priv_key) {
  Utils::writeFile((sconfig.full_client_dir / sconfig.ecu_private_key), priv_key);
  Utils::writeFile((sconfig.full_client_dir / sconfig.ecu_public_key), pub_key);
}

bool ManagedSecondary::loadKeys(std::string *pub_key, std::string *priv_key) {
  boost::filesystem::path public_key_path = sconfig.full_client_dir / sconfig.ecu_public_key;
  boost::filesystem::path private_key_path = sconfig.full_client_dir / sconfig.ecu_private_key;

  if (!boost::filesystem::exists(public_key_path) || !boost::filesystem::exists(private_key_path)) {
    return false;
  }

  *priv_key = Utils::readFile(private_key_path.string());
  *pub_key = Utils::readFile(public_key_path.string());
  return true;
}

}  // namespace Primary
