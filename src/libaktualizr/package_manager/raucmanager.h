#ifndef RAUC_H_
#define RAUC_H_

#include <sdbus-c++/sdbus-c++.h>
#include <sys/stat.h>
#include <atomic>
#include <boost/algorithm/string.hpp>
#include <boost/optional/optional.hpp>
#include <string>
#include "bootloader/bootloader.h"
#include "json/json.h"
#include "libaktualizr/packagemanagerinterface.h"
#include "libaktualizr/types.h"
#include "storage/invstorage.h"
#include "utilities/utils.h"

class RaucManager : public PackageManagerInterface {
 public:
  // Constructor, using PackageManagerInterface's constructor
  RaucManager(const PackageConfig& pconfig, const BootloaderConfig& bconfig, const std::shared_ptr<INvStorage>& storage,
              const std::shared_ptr<HttpInterface>& http, Bootloader* bootloader = nullptr);

  // Destructor
  ~RaucManager() override = default;

  RaucManager(const RaucManager&) = delete;
  RaucManager(RaucManager&&) = delete;
  RaucManager& operator=(const RaucManager&) = delete;
  RaucManager& operator=(RaucManager&&) = delete;

  // Overriding necessary functions from PackageManagerInterface
  std::string name() const override { return "rauc"; };
  Json::Value getInstalledPackages() const override;
  virtual std::string getCurrentHash() const;
  Uptane::Target getCurrent() const override;
  data::InstallationResult install(const Uptane::Target& target) const override;
  void completeInstall() const override;
  data::InstallationResult finalizeInstall(const Uptane::Target& target) override;
  bool fetchTarget(const Uptane::Target& target, Uptane::Fetcher& fetcher, const KeyManager& keys,
                   const FetcherProgressCb& progress_cb, const api::FlowControlToken* token) override;
  TargetStatus verifyTarget(const Uptane::Target& target) const override;
  bool checkAvailableDiskSpace(uint64_t required_bytes) const override;

 private:
  // Signal handlers for installation progress and completion
  void onCompleted(const std::int32_t& status);
  void onProgressChanged(const std::string& interfaceName, const std::map<std::string, sdbus::Variant>& changedProps);

  // Method to send the installation request to RAUC via D-Bus
  void sendRaucInstallRequest(const std::string& bundlePath) const;

  // Method to write the rootImage hash to desired file
  void writeHashToFile(const std::string& hash) const;
  void createDirectoryIfNotExists(const std::string& directoryPath) const;
  // RAUC-related configurations and proxy object for DBus communication
  data::ResultCode::Numeric installResultCode;
  std::string installResultDescription;
  std::string installResultError;
  std::shared_ptr<sdbus::IProxy> raucProxy_;
  std::unique_ptr<Bootloader> bootloader_;
  // Atomic flag to indicate whether the installation is complete
  std::atomic<bool> installationComplete;
  std::atomic<bool> installationErrorLogged;

  mutable std::string currentHash;
  mutable std::atomic<bool> currentHashCalculated;

  const std::string raucDestination = "de.pengutronix.rauc";
  const std::string raucObjectPath = "/";
  const std::string installBundleInterface = "de.pengutronix.rauc.Installer";
  const std::string installBundleMethod = "InstallBundle";
  const std::string completedSignal = "Completed";
  const std::string propertiesChangedProgress = "Progress";
  const std::string propertiesChangedError = "LastError";
  const std::string propertiesChangedSignal = "PropertiesChanged";
  const std::string propertiesChangedInterface = "org.freedesktop.DBus.Properties";
};

#endif  // RAUC_H_
