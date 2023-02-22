#include <chrono>
#include <fstream>
#include <unordered_map>

// #define EXTRA_DEBUG

#include <json/json.h>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>

#include "crypto/crypto.h"
#include "uptane/manifest.h"
#include "utilities/utils.h"

#include "torizongenericsecondary.h"

#define CURRENT_INTERFACE_MAJOR 1
#define CURRENT_INTERFACE_MINOR 0

namespace bp = boost::process;
namespace bf = boost::filesystem;

namespace Primary {

constexpr const char* const TorizonGenericSecondaryConfig::Type;

TorizonGenericSecondaryConfig::TorizonGenericSecondaryConfig(const Json::Value& json_config)
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
  action_handler_path = json_config["action_handler_path"].asString();
}

std::vector<TorizonGenericSecondaryConfig> TorizonGenericSecondaryConfig::create_from_file(
    const boost::filesystem::path& file_full_path) {
  Json::Value json_config;
  std::ifstream json_file(file_full_path.string());
  Json::parseFromStream(Json::CharReaderBuilder(), json_file, &json_config, nullptr);
  json_file.close();

  std::vector<TorizonGenericSecondaryConfig> sec_configs;
  sec_configs.reserve(json_config[Type].size());

  for (const auto& item : json_config[Type]) {
    sec_configs.emplace_back(TorizonGenericSecondaryConfig(item));
  }
  return sec_configs;
}

void TorizonGenericSecondaryConfig::dump(const boost::filesystem::path& file_full_path) const {
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
  json_config["action_handler_path"] = action_handler_path.string();

  Json::Value root;
  // Append to the config file if it already exists.
  if (boost::filesystem::exists(file_full_path)) {
    root = Utils::parseJSONFile(file_full_path);
  }
  root[Type].append(json_config);

  Json::StreamWriterBuilder json_bwriter;
  json_bwriter["indentation"] = "\t";
  std::unique_ptr<Json::StreamWriter> const json_writer(json_bwriter.newStreamWriter());

  boost::filesystem::create_directories(file_full_path.parent_path());
  std::ofstream json_file(file_full_path.string());
  json_writer->write(root, &json_file);
  json_file.close();
}

inline boost::filesystem::path addNewExtension(const boost::filesystem::path& fpath) {
  return boost::filesystem::path(fpath.string() + ".new");
}

TorizonGenericSecondary::TorizonGenericSecondary(const Primary::TorizonGenericSecondaryConfig& sconfig_in)
    : ManagedSecondary(dynamic_cast<const ManagedSecondaryConfig&>(sconfig_in)), config_(sconfig_in) {}

bool TorizonGenericSecondary::getFirmwareInfo(Uptane::InstalledImageInfo& firmware_info) const {
  const std::string action{"get-firmware-info"};
  const VarMap vars = {
      // TODO: [TORIZON] RFU.
      {"SECONDARY_FWINFO_DATA", "{}"},
  };

  Json::Value output;
  // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
  ActionHandlerResult handler_result = callActionHandler(action, vars, &output);

  bool proc_output = false;
  switch (handler_result) {
    case ActionHandlerResult::NotAvailable:
    case ActionHandlerResult::ProcNoOutput:
    case ActionHandlerResult::ReqErrorProc:
      // Returning false here tells aktualizr that the information is not available.
      return false;
    case ActionHandlerResult::ReqNormalProc:
      return ManagedSecondary::getFirmwareInfo(firmware_info);
    case ActionHandlerResult::ProcOutput:
      proc_output = true;
      break;
    default:
      LOG_WARNING << action << ": Unhandled action-handler result: " << static_cast<int>(handler_result);
      return false;
  }

  bool result = false;
  if (proc_output) {
    // ---
    // Handle "name" field:
    // ---
    if (output["name"]) {
      firmware_info.name = output["name"].asString();
    } else {
      // Fall-back: mimic behavior from base class.
      if (!boost::filesystem::exists(config_.target_name_path)) {
        firmware_info.name = std::string("noimage");
      } else {
        firmware_info.name = Utils::readFile(config_.target_name_path.string());
      }
    }

    // ---
    // Handle "sha256" and "length" fields:
    // ---
    if (output["sha256"] && output["length"]) {
      // TODO: Should we check if this looks like a SHA-256 string?
      firmware_info.hash = boost::algorithm::to_lower_copy(output["sha256"].asString());
      firmware_info.len = output["length"].asUInt64();
    } else {
      if (output["sha256"] || output["length"]) {
        LOG_WARNING << action << ": Action-handler " << config_.action_handler_path
                    << " should always output both 'sha256' and 'length' fields or none of them";
      }
      std::ifstream source(config_.firmware_path.string(), std::ios::in | std::ios::binary);
      if (!source) {
        // If file cannot be read generate the hash of an empty file mimicking the base class behavior.
        firmware_info.hash = Uptane::ManifestIssuer::generateVersionHashStr("");
        firmware_info.len = 0;
      } else {
        ssize_t nread = 0;
        // TODO: [TORIZON] Consider doing the same in the base class: calculate the hash from the file
        // instead of loading all of the file into memory to determine its hash.
        firmware_info.hash = Uptane::ManifestIssuer::generateVersionHashStr(source, &nread);
        firmware_info.len = static_cast<uint64_t>(nread);
      }
    }

    // ---
    // Handle "status" field (required):
    // ---
    if (output["status"]) {
      const std::string status = output["status"].asString();
      if (status == "ok") {
        result = true;
      } else if (status == "failed") {
        result = false;
      } else {
        LOG_WARNING << action << ": Action-handler " << config_.action_handler_path
                    << " output unexpected value for field 'status'";
      }
    } else {
      LOG_WARNING << action << ": Action-handler " << config_.action_handler_path
                  << " must always output field 'status'";
      result = false;
    }

    // ---
    // Handle "message" field:
    // ---
    if (output["message"]) {
      LOG_INFO << "Action-handler " << config_.action_handler_path << " message: " << output["message"].asString();
    }
  }

  return result;
}

void TorizonGenericSecondary::getInstallVars(VarMap& vars, const Uptane::Target& target,
                                             const InstallInfo& info) const {
  // TODO: [TORIZON] RFU.
  vars["SECONDARY_INSTALL_DATA"] = "{}";
  vars["SECONDARY_UPDATE_TYPE"] = Uptane::UpdateTypeToString(info.getUpdateType());
  vars["SECONDARY_FIRMWARE_PATH_PREV"] = config_.firmware_path.string();
  vars["SECONDARY_FIRMWARE_PATH"] = getNewFirmwarePath().string();  // Override
  if (target.hashes().at(0).type() != Hash::Type::kSha256) {
    throw std::runtime_error("main hash is not SHA-256");
  }
  // Use lower-case hash string to match manifest:
  vars["SECONDARY_FIRMWARE_SHA256"] = boost::algorithm::to_lower_copy(target.hashes().at(0).HashString());
  vars["SECONDARY_CUSTOM_METADATA"] = Utils::jsonToCanonicalStr(target.custom_data());
  // TODO: [TORIZON] Should we also pass the target URI?
  // TODO: [TORIZON] Handle offline-updates on a generic secondary.
  // vars["SECONDARY_IMAGE_PATH_OFFLINE"] = "{}";
  // vars["SECONDARY_METADATA_PATH_OFFLINE"] = "{}";
}

data::InstallationResult TorizonGenericSecondary::install(const Uptane::Target& target, const InstallInfo& info) {
  const std::string action{"install"};

  // Create new firmware file with a temporary name.
  boost::filesystem::path new_fwpath = getNewFirmwarePath();
  {
    LOG_TRACE << "Creating " << new_fwpath;
    auto strm = secondary_provider_->getTargetFileHandle(target);
    std::ofstream out_file(new_fwpath.string(), std::ios::binary);
    out_file << strm.rdbuf();
    strm.close();
    out_file.close();
  }

  // Create new target-name file also with a temporary name.
  boost::filesystem::path new_tgtname = getNewTargetNamePath();
  {
    LOG_TRACE << "Storing target name " << target.filename() << " into " << new_tgtname;
    Utils::writeFile(new_tgtname, target.filename());
  }

  VarMap vars;
  getInstallVars(vars, target, info);

  Json::Value output;
  // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
  ActionHandlerResult handler_result = callActionHandler(action, vars, &output);

  bool proc_output = false;
  data::ResultCode::Numeric result_code = data::ResultCode::Numeric::kUnknown;
  switch (handler_result) {
    case ActionHandlerResult::NotAvailable:
    case ActionHandlerResult::ProcNoOutput:
      // Unexpected condition:
      result_code = data::ResultCode::Numeric::kGeneralError;
      break;
    case ActionHandlerResult::ReqErrorProc:
      result_code = data::ResultCode::Numeric::kInstallFailed;
      break;
    case ActionHandlerResult::ReqNormalProc:
      // Normal processing is handled as okay.
      result_code = data::ResultCode::Numeric::kOk;
      break;
    case ActionHandlerResult::ProcOutput:
      // Perform further processing to decide what to do.
      proc_output = true;
      break;
    default:
      // Unexpected condition:
      LOG_WARNING << action << ": Unhandled action-handler result: " << static_cast<int>(handler_result);
      result_code = data::ResultCode::Numeric::kGeneralError;
      break;
  }

  if (proc_output) {
    // ---
    // Handle "status" field (required):
    // ---
    if (output["status"]) {
      const std::string status = output["status"].asString();
      if (status == "ok") {
        result_code = data::ResultCode::Numeric::kOk;
      } else if (status == "failed") {
        result_code = data::ResultCode::Numeric::kInstallFailed;
      } else if (status == "need-completion") {
        result_code = data::ResultCode::Numeric::kNeedCompletion;
      } else {
        LOG_WARNING << action << ": Action-handler " << config_.action_handler_path
                    << " output unexpected value for field 'status'";
        result_code = data::ResultCode::Numeric::kGeneralError;
      }
    } else {
      LOG_WARNING << action << ": Action-handler " << config_.action_handler_path
                  << " must always output field 'status'";
      result_code = data::ResultCode::Numeric::kGeneralError;
    }

    // ---
    // Handle "message" field:
    // ---
    if (output["message"]) {
      LOG_INFO << "Action-handler " << config_.action_handler_path << " message: " << output["message"].asString();
    }
  }

  maybeFinishInstall(result_code, new_fwpath, new_tgtname);

  return data::InstallationResult(result_code, "");
}

void TorizonGenericSecondary::getCompleteInstallVars(VarMap& vars, const Uptane::Target& target) const {
  // TODO: [TORIZON] RFU.
  vars["SECONDARY_CMPLINSTALL_DATA"] = "{}";
  vars["SECONDARY_FIRMWARE_PATH_PREV"] = config_.firmware_path.string();
  vars["SECONDARY_FIRMWARE_PATH"] = getNewFirmwarePath().string();  // Override
  if (target.hashes().at(0).type() != Hash::Type::kSha256) {
    throw std::runtime_error("main hash is not SHA-256");
  }
  // Use lower-case hash string to match manifest:
  vars["SECONDARY_FIRMWARE_SHA256"] = boost::algorithm::to_lower_copy(target.hashes().at(0).HashString());
  vars["SECONDARY_CUSTOM_METADATA"] = Utils::jsonToCanonicalStr(target.custom_data());
}

data::InstallationResult TorizonGenericSecondary::completeInstall(const Uptane::Target& target) {
  const std::string action{"complete-install"};

  VarMap vars;
  getCompleteInstallVars(vars, target);

  Json::Value output;
  // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
  ActionHandlerResult handler_result = callActionHandler(action, vars, &output);

  bool proc_output = false;
  data::ResultCode::Numeric result_code = data::ResultCode::Numeric::kUnknown;
  switch (handler_result) {
    case ActionHandlerResult::NotAvailable:
    case ActionHandlerResult::ProcNoOutput:
      // Unexpected condition:
      result_code = data::ResultCode::Numeric::kGeneralError;
      break;
    case ActionHandlerResult::ReqErrorProc:
      result_code = data::ResultCode::Numeric::kInstallFailed;
      break;
    case ActionHandlerResult::ReqNormalProc:
      result_code = data::ResultCode::Numeric::kOk;
      break;
    case ActionHandlerResult::ProcOutput:
      // Perform further processing to decide what to do.
      proc_output = true;
      break;
    default:
      // Unexpected condition:
      LOG_WARNING << action << ": Unhandled action-handler result: " << static_cast<int>(handler_result);
      result_code = data::ResultCode::Numeric::kGeneralError;
      break;
  }

  if (proc_output) {
    // ---
    // Handle "status" field (required):
    // ---
    if (output["status"]) {
      const std::string status = output["status"].asString();
      if (status == "ok") {
        result_code = data::ResultCode::Numeric::kOk;
      } else if (status == "failed") {
        result_code = data::ResultCode::Numeric::kInstallFailed;
      } else if (status == "need-completion") {
        result_code = data::ResultCode::Numeric::kNeedCompletion;
      } else {
        LOG_WARNING << action << ": Action-handler " << config_.action_handler_path
                    << " output unexpected value for field 'status'";
        result_code = data::ResultCode::Numeric::kGeneralError;
      }
    } else {
      LOG_WARNING << action << ": Action-handler " << config_.action_handler_path
                  << " must always output field 'status'";
      result_code = data::ResultCode::Numeric::kGeneralError;
    }

    // ---
    // Handle "message" field:
    // ---
    if (output["message"]) {
      LOG_INFO << "Action-handler " << config_.action_handler_path << " message: " << output["message"].asString();
    }
  }

  maybeFinishInstall(result_code, getNewFirmwarePath(), getNewTargetNamePath());

  return data::InstallationResult(result_code, "");
}

const TorizonGenericSecondary::VarMap& TorizonGenericSecondary::getSharedVars(bool update) const {
  if (!update) {
    return shared_vars_;
  }
  shared_vars_.empty();
  shared_vars_["SECONDARY_INTERFACE_MAJOR"] = std::to_string(CURRENT_INTERFACE_MAJOR);
  shared_vars_["SECONDARY_INTERFACE_MINOR"] = std::to_string(CURRENT_INTERFACE_MINOR);
  shared_vars_["SECONDARY_FIRMWARE_PATH"] = config_.firmware_path.string();
  shared_vars_["SECONDARY_HARDWARE_ID"] = config_.ecu_hardware_id;
  shared_vars_["SECONDARY_ECU_SERIAL"] = getSerial().ToString();
  return shared_vars_;
}

boost::filesystem::path TorizonGenericSecondary::getNewFirmwarePath() const {
  if (config_.firmware_path.empty()) {
    throw std::runtime_error(std::string(TorizonGenericSecondaryConfig::Type) + "firmware path not configured");
  }
  return addNewExtension(config_.firmware_path);
}

boost::filesystem::path TorizonGenericSecondary::getNewTargetNamePath() const {
  if (config_.target_name_path.empty()) {
    throw std::runtime_error(std::string(TorizonGenericSecondaryConfig::Type) + "target name path not configured");
  }
  return addNewExtension(config_.target_name_path);
}

void TorizonGenericSecondary::maybeFinishInstall(data::ResultCode::Numeric result_code,
                                                 const boost::filesystem::path& new_fwpath,
                                                 const boost::filesystem::path& new_tgtname) {
  boost::system::error_code ec;
  if (result_code == data::ResultCode::Numeric::kOk) {
    LOG_TRACE << "Renaming " << new_fwpath << " as " << config_.firmware_path;
    boost::filesystem::rename(new_fwpath, config_.firmware_path, ec);
    if (ec) {
      LOG_WARNING << "Error renaming " << new_fwpath << " as " << config_.firmware_path << ": " << ec.message();
    }
    LOG_TRACE << "Renaming " << new_tgtname << " as " << config_.target_name_path;
    boost::filesystem::rename(new_tgtname, config_.target_name_path, ec);
    if (ec) {
      LOG_WARNING << "Error renaming " << new_tgtname << " as " << config_.target_name_path << ": " << ec.message();
    }
  } else if (result_code == data::ResultCode::Numeric::kNeedCompletion) {
    // Postpone decision again.
  } else {
    LOG_TRACE << "Deleting " << new_fwpath;
    boost::filesystem::remove(new_fwpath, ec);
    if (ec) {
      LOG_WARNING << "Error deleting file " << new_fwpath << ": " << ec.message();
    }
    LOG_TRACE << "Deleting " << new_tgtname;
    boost::filesystem::remove(new_tgtname, ec);
    if (ec) {
      LOG_WARNING << "Error deleting file " << new_tgtname << ": " << ec.message();
    }
  }
}

TorizonGenericSecondary::ActionHandlerResult TorizonGenericSecondary::callActionHandler(const std::string& action,
                                                                                        const VarMap& action_vars,
                                                                                        Json::Value* output) const {
  // ---
  // Define action-handler environment.
  // ---

  // Base environment taken from current process.
  bp::environment env = boost::this_process::environment();

  // Add action-independent & action-dependent variables.
  for (const auto& var : getSharedVars()) {
    env[var.first] = var.second;
  }
  for (const auto& var : action_vars) {
    env[var.first] = var.second;
  }

  // ---
  // Start action-handler.
  // ---

  // Create temporary file name to hold program output.
  TemporaryFile temp_file("action");

  // Start action-handler program.
  std::error_code ec;
  auto start_dir_ = (!config_.full_client_dir.empty()) ? config_.full_client_dir : bf::current_path();
  auto start_dir = bp::start_dir(start_dir_);
  bp::child action_proc(config_.action_handler_path, action, start_dir, bp::std_out > temp_file.Path(), env, ec);
  if (ec) {
    LOG_WARNING << "Could not start action-handler " << config_.action_handler_path << ": " << ec.message();
    return ActionHandlerResult::NotAvailable;
  }

  // ---
  // Wait action-handler to finish.
  // ---

  LOG_DEBUG << "Action-handler " << config_.action_handler_path << " (action=" << action << ") started";

  // TODO: [TORIZON] Should we consider a timeout for the command?
  //
  // NOTE: Tests with wait_for(), followed by terminate() and wait() still leave the killed process as defunct:
  // See https://stackoverflow.com/questions/68679894/boost-the-child-process-remains-as-a-zombie; a possible
  // solution without having to change signals mask would be to create a thread to send a terminate signal to
  // the process after a timeout.
  //
  action_proc.wait(ec);
  if (ec) {
    LOG_WARNING << "Error while waiting for action-handler " << config_.action_handler_path << ": " << ec.message();
    return ActionHandlerResult::NotAvailable;
  }

  if (WIFSIGNALED(action_proc.native_exit_code())) {
    LOG_WARNING << "Action-handler " << config_.action_handler_path << " (action=" << action << ")"
                << " terminated by signal #" << WTERMSIG(action_proc.native_exit_code());
    return ActionHandlerResult::NotAvailable;
  }

  LOG_DEBUG << "Action-handler " << config_.action_handler_path << " (action=" << action << ")"
            << " finished with exit code " << action_proc.exit_code();

  // ---
  // Handle action-handler exit codes.
  // ---

  bool parse_output = false;
  ActionHandlerResult handler_result = ActionHandlerResult::Default;
  switch (action_proc.exit_code()) {
    case 0:
      // Some JSON output is always expected in this case.
      parse_output = true;
      break;
    case 64:
      handler_result = ActionHandlerResult::ReqNormalProc;
#ifdef EXTRA_DEBUG
      LOG_TRACE << "Action-handler requests normal processing";
#endif
      break;
    case 65:
      handler_result = ActionHandlerResult::ReqErrorProc;
#ifdef EXTRA_DEBUG
      LOG_TRACE << "Action-handler requests error processing";
#endif
      break;
    default:
      LOG_WARNING << "Action-handler " << config_.action_handler_path << " (action=" << action << ")"
                  << " returned an exit code of " << action_proc.exit_code()
                  << " which is unexpected at the moment and will be handled as an error";
      break;
  }

  if (parse_output) {
    std::string errors;
    std::ifstream stream(temp_file.PathString());
    Json::Value json_output;

    if (Json::parseFromStream(Json::CharReaderBuilder(), stream, &json_output, &errors)) {
      handler_result = ActionHandlerResult::ProcOutput;
      if (output != nullptr) {
        *output = json_output;
      }
#ifdef EXTRA_DEBUG
      LOG_DEBUG << "Action-handler output (JSON): ";
      LOG_DEBUG << json_output;
#endif
    } else {
      handler_result = ActionHandlerResult::ProcNoOutput;
      LOG_WARNING << "Action-handler " << config_.action_handler_path << " (action=" << action << ")"
                  << " output could not be parsed (expecting JSON string)";
      LOG_DEBUG << "JSON parse errors" << errors;
    }
  }

  return handler_result;
}

}  // namespace Primary
