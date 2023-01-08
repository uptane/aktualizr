#include <boost/filesystem/path.hpp>
#include <sstream>

#include "compose_manager.h"
#include "dockercomposesecondary.h"
#include "dockerofflineloader.h"
#include "libaktualizr/types.h"
#include "logging/logging.h"
#include "uptane/manifest.h"
#include "utilities/fault_injection.h"
#include "utilities/utils.h"

using std::stringstream;

namespace Primary {

constexpr const char* const DockerComposeSecondaryConfig::Type;

DockerComposeSecondaryConfig::DockerComposeSecondaryConfig(const Json::Value& json_config)
    : ManagedSecondaryConfig(Type) {
  partial_verifying = json_config["partial_verifying"].asBool();
  ecu_serial = json_config["ecu_serial"].asString();
  ecu_hardware_id = json_config["ecu_hardware_id"].asString();
  full_client_dir = json_config["full_client_dir"].asString();
  ecu_private_key = json_config["ecu_private_key"].asString();
  ecu_public_key = json_config["ecu_public_key"].asString();
  firmware_path = json_config["firmware_path"].asString();
  target_name_path = json_config["target_name_path"].asString();
  metadata_path = json_config["metadata_path"].asString();
}

std::vector<DockerComposeSecondaryConfig> DockerComposeSecondaryConfig::create_from_file(
    const boost::filesystem::path& file_full_path) {
  Json::Value json_config;
  std::ifstream json_file(file_full_path.string());
  Json::parseFromStream(Json::CharReaderBuilder(), json_file, &json_config, nullptr);
  json_file.close();

  std::vector<DockerComposeSecondaryConfig> sec_configs;
  sec_configs.reserve(json_config[Type].size());

  for (const auto& item : json_config[Type]) {
    sec_configs.emplace_back(DockerComposeSecondaryConfig(item));
  }
  return sec_configs;
}

void DockerComposeSecondaryConfig::dump(const boost::filesystem::path& file_full_path) const {
  Json::Value json_config;

  json_config["partial_verifying"] = partial_verifying;
  json_config["ecu_serial"] = ecu_serial;
  json_config["ecu_hardware_id"] = ecu_hardware_id;
  json_config["full_client_dir"] = full_client_dir.string();
  json_config["ecu_private_key"] = ecu_private_key;
  json_config["ecu_public_key"] = ecu_public_key;
  json_config["firmware_path"] = firmware_path.string();
  json_config["target_name_path"] = target_name_path.string();
  json_config["metadata_path"] = metadata_path.string();

  Json::Value root;
  root[Type].append(json_config);

  Json::StreamWriterBuilder json_bwriter;
  json_bwriter["indentation"] = "\t";
  std::unique_ptr<Json::StreamWriter> const json_writer(json_bwriter.newStreamWriter());

  boost::filesystem::create_directories(file_full_path.parent_path());
  std::ofstream json_file(file_full_path.string());
  json_writer->write(root, &json_file);
  json_file.close();
}

DockerComposeSecondary::DockerComposeSecondary(Primary::DockerComposeSecondaryConfig sconfig_in)
    : ManagedSecondary(std::move(sconfig_in)) {}

data::InstallationResult DockerComposeSecondary::sendFirmware(const Uptane::Target& target,
                                                              const InstallInfo& install_info,
                                                              const api::FlowControlToken* flow_control) {
  if (flow_control != nullptr && flow_control->hasAborted()) {
    return data::InstallationResult(data::ResultCode::Numeric::kOperationCancelled, "");
  }

  auto tgt_stream = secondary_provider_->getTargetFileHandle(target);

  /* Here we try to make container updates "as atomic as possible". So we save
   * the updated docker-compose file with another name (<firmware_path>.tmp), run
   * docker-compose commands to pull and run the containers, and if it fails
   * we still have the previous docker-compose file to "rollback" to the current
   * version of the containers.
   */

  // Just a temp file used to atomically write the "tmp" compose file
  std::string compose_temp = composeFile().string();
  compose_temp += ".temporary";

  // Save new compose file in a temporary file.
  std::ofstream out_file(compose_temp, std::ios::binary);
  out_file << tgt_stream.rdbuf();
  tgt_stream.close();
  out_file.close();

  boost::filesystem::rename(compose_temp, composeFileNew());

  switch (install_info.getUpdateType()) {
    case UpdateType::kOnline:
      // Only try to pull images upon an online update.
      if (!compose_manager_.pull(composeFileNew(), flow_control)) {
        if (flow_control != nullptr && flow_control->hasAborted()) {
          return data::InstallationResult(data::ResultCode::Numeric::kOperationCancelled, "Aborted in docker-pull");
        }
        LOG_ERROR << "Error running docker-compose pull";
        return data::InstallationResult(data::ResultCode::Numeric::kDownloadFailed, "docker compose pull failed");
      }
      break;

    case UpdateType::kOffline: {
      auto img_path = install_info.getImagesPathOffline() / (target.sha256Hash() + ".images");
      auto man_path = install_info.getMetadataPathOffline() / "docker" / (target.sha256Hash() + ".manifests");
      boost::filesystem::path compose_out;

      if (!loadDockerImages(composeFileNew(), target.sha256Hash(), img_path, man_path, &compose_out)) {
        return data::InstallationResult(data::ResultCode::Numeric::kInstallFailed,
                                        "Loading offline docker images failed");
      }
      // Docker images loaded and an "offline" version of compose-file available.
      // Overwrite the new compose file with that "offline" version.
      boost::filesystem::rename(compose_out, composeFileNew());
      break;
    }
    default:
      return data::InstallationResult(data::ResultCode::Numeric::kInternalError, "Unknown UpdateType");
  }

  return data::InstallationResult(data::ResultCode::Numeric::kOk, "");
}

data::InstallationResult DockerComposeSecondary::install(const Uptane::Target& target, const InstallInfo& info,
                                                         const api::FlowControlToken* flow_control) {
  (void)info;
  LOG_INFO << "Updating containers via docker-compose";

  bool sync_update = secondary_provider_->pendingPrimaryUpdate();
  if (sync_update) {
    // For a synchronous update, most of this step happens on reboot.
    LOG_INFO << "OSTree update pending. This is a synchronous update transaction.";
    return data::InstallationResult(data::ResultCode::Numeric::kNeedCompletion, "");
  }

  // This is the case that applies without a reboot (non-synchronous)
  return installCommon(target, flow_control);
}

// TODO: Consider returning a different result code to ask for a reboot
// OR giving a delay for the reboot: `shutdown +1`.
boost::optional<data::InstallationResult> DockerComposeSecondary::completePendingInstall(const Uptane::Target& target) {
  LOG_INFO << "Finishing pending container updates via docker-compose";
  if (!boost::filesystem::exists(composeFileNew())) {
    // Should never reach here in normal operation.
    LOG_ERROR << "ComposeManager::pendingUpdate : " << composeFileNew() << " does not exist";
    return data::InstallationResult(data::ResultCode::Numeric::kInternalError,
                                    "completePendingInstall can't find composeFileNew()");
  }

  if (compose_manager_.checkRollback()) {
    return data::InstallationResult(data::ResultCode::Numeric::kInstallFailed, "bootloader rolled back OS update");
  }

  return installCommon(target, nullptr);
}

// Shared logic between completePendingInstall() and install()
data::InstallationResult DockerComposeSecondary::installCommon(const Uptane::Target& target,
                                                               const api::FlowControlToken* flow_control) {
  (void)flow_control;  // TODO

  if (boost::filesystem::exists(composeFile())) {
    if (!compose_manager_.down(composeFile())) {
      LOG_ERROR << "docker-compose down of old image failed";
      return data::InstallationResult(data::ResultCode::Numeric::kInstallFailed, "Docker compose down failed");
    }
  }

  if (!compose_manager_.up(composeFileNew())) {
    // Attempt recovery
    boost::filesystem::remove(composeFileNew());
    const char* description;
    if (!compose_manager_.up(composeFile())) {
      LOG_ERROR << "docker-compose up of new image failed, and "
                   "also could not recover by docker-compose up on the old image";
      description = "Docker compose up failed, and restore failed";
    } else {
      description = "Docker compose up failed (restore ok)";
    }
    compose_manager_.cleanup();
    return data::InstallationResult(data::ResultCode::Numeric::kInstallFailed, description);
  }

  boost::filesystem::rename(composeFileNew(), composeFile());
  Utils::writeFile(sconfig.target_name_path, target.filename());
  return data::InstallationResult(data::ResultCode::Numeric::kOk, "");
}

void DockerComposeSecondary::rollbackPendingInstall() {
  LOG_INFO << "Rolling back container update";
  // This function handles a failed sync update and
  // performs a rollback on the needed ECUs to ensure sync.

  boost::filesystem::remove(composeFileNew());

  if (compose_manager_.checkRollback()) {
    // OS rollback detected, need to rollback just the compose secondary.
    compose_manager_.up(composeFile());
    compose_manager_.cleanup();
  } else {
    // Failed to complete pending compose update, need to rollback
    // the primary, compose secondary, and reboot the system.
    compose_manager_.cleanup();
    CommandRunner::run("fw_setenv rollback 1");
    CommandRunner::run("reboot");
  }
}

bool DockerComposeSecondary::loadDockerImages(const boost::filesystem::path& compose_in,
                                              const std::string& compose_sha256,
                                              const boost::filesystem::path& images_path,
                                              const boost::filesystem::path& manifests_path,
                                              boost::filesystem::path* compose_out) {
  if (compose_out != nullptr) {
    compose_out->clear();
  }

  boost::filesystem::path compose_new = compose_in;
  compose_new.replace_extension(".off");

  try {
    auto dmcache = std::make_shared<DockerManifestsCache>(manifests_path);

    DockerComposeOfflineLoader dcloader(images_path, dmcache);
    dcloader.loadCompose(compose_in, compose_sha256);
    dcloader.dumpReferencedImages();
    dcloader.dumpImageMapping();
    dcloader.installImages();
    dcloader.writeOfflineComposeFile(compose_new);
    // TODO: [OFFUPD] Define how to perform the offline-online transformation (related to getFirmwareInfo()).

  } catch (std::runtime_error& exc) {
    // TODO: Consider throwing/handling custom exception types from dockerofflineloader and dockertarballloader.
    LOG_WARNING << "Offline loading failed: " << exc.what();
    return false;
  }

  if (compose_out != nullptr) {
    *compose_out = compose_new;
  }

  return true;
}

bool DockerComposeSecondary::getFirmwareInfo(Uptane::InstalledImageInfo& firmware_info) const {
  std::string content;

  if (!boost::filesystem::exists(sconfig.firmware_path)) {
    firmware_info.name = std::string("noimage");
    content = "";
  } else {
    if (!boost::filesystem::exists(sconfig.target_name_path)) {
      firmware_info.name = std::string("docker-compose.yml");
    } else {
      firmware_info.name = Utils::readFile(sconfig.target_name_path.string());
    }

    // Read compose-file and transform it into its original form in memory.
    DockerComposeFile dcfile;
    if (!dcfile.read(sconfig.firmware_path)) {
      LOG_WARNING << "Could not read compose " << sconfig.firmware_path;
      return false;
    }
    dcfile.backwardTransform();
    content = dcfile.toString();
  }

  firmware_info.hash = Uptane::ManifestIssuer::generateVersionHashStr(content);
  firmware_info.len = content.size();

  LOG_TRACE << "DockerComposeSecondary::getFirmwareInfo: hash=" << firmware_info.hash;

  return true;
}

}  // namespace Primary
