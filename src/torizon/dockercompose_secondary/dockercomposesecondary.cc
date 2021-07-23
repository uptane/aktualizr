#include "dockercomposesecondary.h"
#include "uptane/manifest.h"
#include "libaktualizr/types.h"
#include "logging/logging.h"
#include "utilities/fault_injection.h"
#include "utilities/utils.h"
#include "compose_manager.h"
#include "storage/invstorage.h"

#include <sstream>

using std::stringstream;

namespace bpo = boost::program_options;

namespace Primary {

const char* const DockerComposeSecondaryConfig::Type = "docker-compose";

DockerComposeSecondaryConfig::DockerComposeSecondaryConfig(const Json::Value& json_config) : ManagedSecondaryConfig(Type) {
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
    : ManagedSecondary(std::move(sconfig_in)) {
  validateInstall();
}

data::InstallationResult DockerComposeSecondary::install(const Uptane::Target &target) {
  auto str = secondary_provider_->getTargetFileHandle(target);

  /* Here we try to make container updates "as atomic as possible". So we save
   * the updated docker-compose file with another name (<firmware_path>.tmp), run
   * docker-compose commands to pull and run the containers, and if it fails
   * we still have the previous docker-compose file to "roolback" to the current
   * version of the containers.
   */

  std::string compose_file = sconfig.firmware_path.string();
  std::string compose_file_new = compose_file + ".tmp";

  std::ofstream out_file(compose_file_new, std::ios::binary);
  out_file << str.rdbuf();
  str.close();
  out_file.close();

  ComposeManager compose = ComposeManager(compose_file, compose_file_new);

  if (compose.update() == true) {
    Utils::writeFile(sconfig.target_name_path, target.filename());
    if (compose.sync_update) {
      return data::InstallationResult(data::ResultCode::Numeric::kNeedCompletion, "");
    }
    else {
      return data::InstallationResult(data::ResultCode::Numeric::kOk, "");
    }
  }
  else {
    compose.roolback();
    return data::InstallationResult(data::ResultCode::Numeric::kInstallFailed, "");
  }
}

bool DockerComposeSecondary::getFirmwareInfo(Uptane::InstalledImageInfo& firmware_info) const {
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

void DockerComposeSecondary::validateInstall() {
  std::string compose_file = sconfig.firmware_path.string();
  std::string compose_file_new = compose_file + ".tmp";
  ComposeManager pending_check(compose_file, compose_file_new);
  if (!pending_check.pendingUpdate()) {
    LOG_ERROR << "Unable to complete pending container update";

    // Pending compose update failed, unset pending flag so that the rest of the Uptane process can go forward again
    Uptane::EcuSerial serial = getSerial();
    std::shared_ptr<INvStorage> storage;
    bpo::variables_map vm;
    Config config(vm);
    storage = INvStorage::newStorage(config.storage);
    boost::optional<Uptane::Target> pending_target;
    storage->loadInstalledVersions(serial.ToString(), nullptr, &pending_target);
    storage->saveEcuInstallationResult(serial, data::InstallationResult(data::ResultCode::Numeric::kInstallFailed, ""));
    storage->saveInstalledVersion(serial.ToString(), *pending_target, InstalledVersionUpdateMode::kNone);

    pending_check.roolback();
   }

}

}  // namespace Primary
