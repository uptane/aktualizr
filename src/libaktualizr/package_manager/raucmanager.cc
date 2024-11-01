#include "raucmanager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "libaktualizr/packagemanagerfactory.h"
#include "logging/logging.h"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
AUTO_REGISTER_PACKAGE_MANAGER(PACKAGE_MANAGER_RAUC, RaucManager);

// Constructor: Initializes RAUC proxy and registers signal handlers
RaucManager::RaucManager(const PackageConfig &pconfig, const BootloaderConfig &bconfig,
                             const std::shared_ptr<INvStorage> &storage, const std::shared_ptr<HttpInterface> &http,
                             Bootloader *bootloader)
    : PackageManagerInterface(pconfig, BootloaderConfig(), storage, http),
      bootloader_(bootloader == nullptr ? new Bootloader(bconfig, *storage) : bootloader) {
  // initializing default variables
  this->currentHash = "";
  this->currentHashCalculated.store(false);
  this->installResultCode = data::ResultCode::Numeric::kUnknown;
  this->installResultDescription = std::string("");

  // Create a proxy to interact with RAUC over D-Bus
  this->raucProxy_ = sdbus::createProxy(this->raucDestination, this->raucObjectPath);

  // Register signal handlers
  this->raucProxy_->uponSignal(this->completedSignal)
      .onInterface(this->installBundleInterface)
      .call([this](const std::int32_t& status) { this->onCompleted(status); });

  this->raucProxy_->uponSignal(this->propertiesChangedSignal)
      .onInterface(this->propertiesChangedInterface)
      .call([this](const std::string& interfaceName, const std::map<std::string, sdbus::Variant>& changedProperties,
                   std::vector<std::string> invalidProperties __attribute__((unused))) {
        this->onProgressChanged(interfaceName, changedProperties);
      });

  this->raucProxy_->finishRegistration();
}

bool RaucManager::fetchTarget(const Uptane::Target& target, Uptane::Fetcher& fetcher, const KeyManager& keys,
                              const FetcherProgressCb& progress_cb, const api::FlowControlToken* token) {
  if (!target.IsRauc()) {
    // The case when the RAUC package manager is set as a package manager for aktualizr
    // while the target is aimed for a Secondary ECU that is configured with another/non-RAUC package manager
    return PackageManagerInterface::fetchTarget(target, fetcher, keys, progress_cb, token);
  }
  LOG_TRACE << "called RaucManager::fetchTarget()";
  installationComplete.store(false);
  installationErrorLogged.store(false);             
  return true;
}

data::InstallationResult RaucManager::install(const Uptane::Target& target) const {
    LOG_TRACE << "called RaucManager::install()";
    // Extract bundle URI from the target object
    std::string bundlePath = target.uri();
    LOG_INFO << "Target image URI: " << bundlePath;
    // if(bundlePath == std::string::empty) {
    //   return data::InstallationResult(data::ResultCode::Numeric::kGeneralError, "Failed to get the bundle path from target");
    // }
    
    // Extract SHA256 hash from the target object
    std::string sha256Hash = target.custom_data()["rauc"]["rawHashes"]["sha256"].asString();  // Assuming Uptane::Target has this structure
    LOG_INFO << "Target Image sha256 digest: " << sha256Hash;
    // if(sha256Hash == std::string::empty) {
    //   return data::InstallationResult(data::ResultCode::Numeric::kGeneralError, "Failed to get the root sha256 hash from target");
    // }

    // Write the SHA256 hash to the expected file
    try {
        writeHashToFile(sha256Hash);
    } catch (const std::exception& e) {
        LOG_ERROR << "Error writing hash to file: " << e.what() << std::endl;
        return data::InstallationResult(data::ResultCode::Numeric::kGeneralError, "Failed to write SHA256 hash to file");
    }

    // Send RAUC installation request
    try {
        sendRaucInstallRequest(bundlePath);
        // std::cout << "Installation request sent for bundle: " << bundlePath << std::endl;
    } catch (const std::runtime_error& e) {
        LOG_ERROR << e.what() << std::endl;
        return data::InstallationResult(data::ResultCode::Numeric::kGeneralError, "Failed to send RAUC installation request");
    }

    // Wait for the 'Completed' signal
    while (!this->installationComplete.load()) {
        sleep(1);
    }

    if(this->installResultCode == data::ResultCode::Numeric::kInstallFailed) {
      return data::InstallationResult(this->installResultCode, this->installResultDescription);
    }

    // set reboot flag to be notified later
    if (bootloader_ != nullptr) {
      bootloader_->rebootFlagSet();
    }

    sync();
    return data::InstallationResult(this->installResultCode, this->installResultDescription);  // The actual result will come from the signal handlers
}

// Send a RAUC install request over DBus
void RaucManager::sendRaucInstallRequest(const std::string& bundlePath) const {
  LOG_TRACE << "called RaucManager::sendRaucInstallRequest()";
  auto method = this->raucProxy_->createMethodCall(this->installBundleInterface, this->installBundleMethod);
  std::map<std::string, sdbus::Variant> args;
  method << bundlePath;
  method << args;

  try {
    this->raucProxy_->callMethod(method);
  } catch (const sdbus::Error& e) {
    throw std::runtime_error("Failed to send RAUC install request: " + e.getMessage());
  }
}

// Signal handler for "Completed" event
void RaucManager::onCompleted(const std::int32_t& status) {
  LOG_TRACE << "called RaucManager::getInstalledPackages() status: " << status;
  if (status == 0) {
      LOG_INFO << "Installation completed successfully with status code: " << status << std::endl;
      this->installResultCode = data::ResultCode::Numeric::kNeedCompletion;
      this->installResultDescription = "Installation Completed Successfully, restart required";
    } else {
      LOG_INFO << "Installation did not complete successfully with status code: " << status << std::endl;
      while(!this->installationErrorLogged) {
        sleep(1);
      }
      this->installResultCode = data::ResultCode::Numeric::kInstallFailed;
      this->installResultDescription = installResultError;
    }
    this->installationComplete.store(true);
    return;
}

// Signal handler for "PropertiesChanged" event (progress updates)
void RaucManager::onProgressChanged(const std::string& interfaceName,
                                    const std::map<std::string, sdbus::Variant>& changedProperties) {
  if (interfaceName == this->installBundleInterface) {
    auto it = changedProperties.find(this->propertiesChangedProgress);
    if (it != changedProperties.end()) {
      auto progress = it->second.get<sdbus::Struct<int32_t, std::string, int32_t>>();
      auto percentage = progress.get<0>();
      auto message = progress.get<1>();
      auto depth = progress.get<2>();
      std::string level = "|";
      for (int i = 1; i < depth; i++) {
        level.append("  |");
      }
      LOG_INFO << level << "-\"" << message << "\" (" << percentage << "%)";
    }

    auto itError = changedProperties.find(this->propertiesChangedError);
    if (itError != changedProperties.end()) {
      std::string lastError = itError->second.get<std::string>();
      LOG_ERROR << "Last Error: " << lastError << std::endl;
      this->installResultError = lastError;
      this->installationErrorLogged.store(true);
    }
  }
}

Json::Value RaucManager::getInstalledPackages() const {
  LOG_TRACE << "called RaucManager::getInstalledPackages()";
  std::string packages_str = Utils::readFile(config.packages_file);
  std::vector<std::string> package_lines;
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  boost::split(package_lines, packages_str, boost::is_any_of("\n"));
  Json::Value packages(Json::arrayValue);
  for (auto it = package_lines.begin(); it != package_lines.end(); ++it) {
    if (it->empty()) {
      continue;
    }
    size_t pos = it->find(" ");
    if (pos == std::string::npos) {
      throw std::runtime_error("Wrong packages file format");
    }
    Json::Value package;
    package["name"] = it->substr(0, pos);
    package["version"] = it->substr(pos + 1);
    packages.append(package);
  }
  return packages;
}

std::string RaucManager::getCurrentHash() const {
  LOG_TRACE << "called RaucManager::getCurrentHash()";

    // Check if the hash has already been calculated
    if (this->currentHashCalculated) {
        return currentHash;
    }

    // Path to the script and the file that contains the hash
    const std::string scriptPath = "/usr/bin/calc-root-hash.sh";
    const std::string hashFilePath = "/run/aktualizr/root-hash";

    // Execute the script to calculate the hash
    int ret = std::system(scriptPath.c_str());
    if (ret != 0) {
        LOG_ERROR << "Failed to execute get current has script: " << scriptPath;
        return "";  // Return empty string on failure
    }

    // Read the hash from /run/aktualizr/root-hash
    std::ifstream hashFile(hashFilePath);
    if (!hashFile.is_open()) {
        LOG_ERROR << "Failed to open hash file: " << hashFilePath;
        return "";  // Return empty string on failure
    }

    // Read the entire contents of the file into currentHash
    std::stringstream buffer;
    buffer << hashFile.rdbuf();
    currentHash = buffer.str();

    // Close the file
    hashFile.close();

    // Remove any trailing newlines or spaces from the hash
    currentHash.erase(currentHash.find_last_not_of(" \n\r\t") + 1);

    // Set the atomic flag to true
    this->currentHashCalculated.store(true);

    LOG_INFO << "current hash: " << currentHash;

    return currentHash;
}

Uptane::Target RaucManager::getCurrent() const {
  LOG_TRACE << "called RaucManager::getCurrent()";
  const std::string current_hash = getCurrentHash();
  boost::optional<Uptane::Target> current_version;
  // This may appear Primary-specific, but since Secondaries only know about
  // themselves, this actually works just fine for them, too.
  storage_->loadPrimaryInstalledVersions(&current_version, nullptr);

  if (!!current_version && current_version->sha256Hash() == current_hash) {
    return *current_version;
  }

  LOG_ERROR << "Current versions in storage and reported by RAUC do not match";

  // Look into installation log to find a possible candidate. Again, despite the
  // name, this will work for Secondaries as well.
  std::vector<Uptane::Target> installed_versions;
  storage_->loadPrimaryInstallationLog(&installed_versions, false);

  // Version should be in installed versions. It's possible that multiple
  // targets could have the same sha256Hash. In this case the safest assumption
  // is that the most recent (the reverse of the vector) target is what we
  // should return.
  std::vector<Uptane::Target>::reverse_iterator it;
  for (it = installed_versions.rbegin(); it != installed_versions.rend(); it++) {
    if (it->sha256Hash() == current_hash) {
      return *it;
    }
  }
  // We haven't found a matching target. This can occur when a device is
  // freshly manufactured and the factory image is in a delegated target.
  // Aktualizr will have had no reason to fetch the relevant delegation, and it
  // doesn't know where in the delegation tree on the server it might be.
  // See https://github.com/uptane/aktualizr/issues/1 for more details. In this
  // case attempt to construct an approximate Uptane target. By getting the
  // hash correct the server has a chance to figure out what is running on the
  // device.
  // Uptane::EcuMap ecus;
  // std::vector<Hash> hashes{Hash(Hash::Type::kSha256, current_hash)};
  return Uptane::Target::Unknown();
}

void RaucManager::completeInstall() const {
  LOG_TRACE << "called RaucManager::completeInstall()";
  LOG_INFO << "About to reboot the system in order to apply pending updates...";
  bootloader_->reboot();
}

// Finalize the installation
data::InstallationResult RaucManager::finalizeInstall(const Uptane::Target& target) {
  LOG_TRACE << "called RaucManager::finalizeInstall()";
  if (!bootloader_->rebootDetected()) {
    return data::InstallationResult(data::ResultCode::Numeric::kNeedCompletion,
                                    "Reboot is required for the pending update application");
  }

  LOG_INFO << "Checking installation of new RAUC image";
  const std::string current_hash = getCurrentHash();

  data::InstallationResult install_result =
      data::InstallationResult(data::ResultCode::Numeric::kOk, "Successfully booted on new version");

  if (current_hash != target.sha256Hash()) {
    LOG_ERROR << "Expected to boot " << target.sha256Hash() << " but found " << current_hash
              << ". The system may have been rolled back.";
    install_result = data::InstallationResult(data::ResultCode::Numeric::kInstallFailed, "Wrong version booted");
  }

  bootloader_->rebootFlagClear();
  return install_result;
}

// Function to create the /run/aktualizr directory if it does not exist
void RaucManager::createDirectoryIfNotExists(const std::string& directoryPath) const
{
    LOG_TRACE << "called RaucManager::createDirectoryIfNotExists()";
    struct stat st;
    if (stat(directoryPath.c_str(), &st) != 0) {
        if (mkdir(directoryPath.c_str(), 0755) != 0) {
            throw std::runtime_error("Failed to create directory: " + directoryPath);
        }
        LOG_DEBUG << "Directory Created: " << directoryPath;
    } else if (!S_ISDIR(st.st_mode)) {
        throw std::runtime_error(directoryPath + " exists but is not a directory");
    } else {
        LOG_DEBUG << "Directory Exists: " << directoryPath;
    }
}

// Function to write the SHA256 hash to /run/aktualizr/expected-digest
void RaucManager::writeHashToFile(const std::string& hash) const
{
    LOG_TRACE << "called RaucManager::writeHashToFile()";
    const std::string directoryPath = "/run/aktualizr";
    const std::string filePath = directoryPath + "/expected-digest";

    // Create the directory if it doesn't exist
    createDirectoryIfNotExists(directoryPath);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filePath);
    }

    file << hash;

    if (!file) {
        throw std::runtime_error("Failed to write to file: " + filePath);
    }

    file.close();

    LOG_INFO << "SHA256 hash written and file closed: " << filePath << std::endl;
    sync();
}



// Verify the target
TargetStatus RaucManager::verifyTarget(const Uptane::Target& target) const {
  if (!target.IsRauc()) {
    // The case when the OSTree package manager is set as a package manager for aktualizr
    // while the target is aimed for a Secondary ECU that is configured with another/non-OSTree package manager
    return PackageManagerInterface::verifyTarget(target);
  }
  LOG_TRACE << "called RaucManager::verifyTarget()";
  return TargetStatus::kGood;
}

bool RaucManager::checkAvailableDiskSpace(uint64_t required_bytes) const {
  LOG_TRACE << "called RaucManager::checkAvailableDiskSpace() required_bytes: " << required_bytes;
  return true;
}