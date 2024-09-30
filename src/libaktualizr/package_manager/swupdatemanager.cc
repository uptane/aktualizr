#include "swupdatemanager.h"

#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "logging/logging.h"
#include "storage/invstorage.h"
#include "http/httpclient.h"
#include "swupdate/swupdate_api.h" // Hypothetical SWUpdate API header
#include "utilities/fault_injection.h"
#include "libaktualizr/packagemanagerfactory.h"

AUTO_REGISTER_PACKAGE_MANAGER(PACKAGE_MANAGER_SWUPDATE, SwupdateManager);

Json::Value SwupdateManager::getInstalledPackages() const {
  Json::Value packages(Json::arrayValue);
  Json::Value package;
  package["name"] = "fake-package";
  package["version"] = "1.0";
  packages.append(package);
  return packages;
}

Uptane::Target SwupdateManager::getCurrent() const {
  boost::optional<Uptane::Target> current_version;
  storage_->loadPrimaryInstalledVersions(&current_version, nullptr);

  if (!!current_version) {
    return *current_version;
  }

  return Uptane::Target::Unknown();
}

data::InstallationResult SwupdateManager::install(const Uptane::Target& target) const {
  (void)target;

  // fault injection: only enabled with FIU_ENABLE defined
  if (fiu_fail("fake_package_install") != 0) {
    std::string failure_cause = fault_injection_last_info();
    if (failure_cause.empty()) {
      return {data::ResultCode::Numeric::kInstallFailed, ""};
    }
    LOG_DEBUG << "Causing installation failure with message: " << failure_cause;
    return {data::ResultCode(data::ResultCode::Numeric::kInstallFailed, failure_cause), ""};
  }

  if (config.fake_need_reboot) {
    // set reboot flag to be notified later
    if (bootloader_ != nullptr) {
      bootloader_->rebootFlagSet();
    }
    return {data::ResultCode::Numeric::kNeedCompletion, "Application successful, need reboot"};
  }

  return {data::ResultCode::Numeric::kOk, "Installing package was successful"};
}

void SwupdateManager::completeInstall() const {
  LOG_INFO << "Emulating a system reboot";
  bootloader_->reboot(true);
}

data::InstallationResult SwupdateManager::finalizeInstall(const Uptane::Target& target) {
  if (config.fake_need_reboot && !bootloader_->rebootDetected()) {
    return data::InstallationResult(data::ResultCode::Numeric::kNeedCompletion,
                                    "Reboot is required for the pending update application");
  }

  boost::optional<Uptane::Target> pending_version;
  storage_->loadPrimaryInstalledVersions(nullptr, &pending_version);

  if (!pending_version) {
    throw std::runtime_error("No pending update, nothing to finalize");
  }

  data::InstallationResult install_res;

  if (target.MatchTarget(*pending_version)) {
    if (fiu_fail("fake_install_finalization_failure") != 0) {
      const std::string failure_cause = fault_injection_last_info();
      if (failure_cause.empty()) {
        install_res = data::InstallationResult(data::ResultCode::Numeric::kInstallFailed, "");
      } else {
        install_res =
            data::InstallationResult(data::ResultCode(data::ResultCode::Numeric::kInstallFailed, failure_cause),
                                     "Failed to finalize the pending update installation");
      }
    } else {
      install_res = data::InstallationResult(data::ResultCode::Numeric::kOk, "Installing fake package was successful");
    }

  } else {
    install_res =
        data::InstallationResult(data::ResultCode::Numeric::kInternalError, "Pending and new target do not match");
  }

  if (config.fake_need_reboot) {
    bootloader_->rebootFlagClear();
  }
  return install_res;
}

bool SwupdateManager::fetchTarget(const Uptane::Target& target, Uptane::Fetcher& fetcher, const KeyManager& keys,
                                     const FetcherProgressCb& progress_cb, const api::FlowControlToken* token) {
  // fault injection: only enabled with FIU_ENABLE defined. Note that all
  // exceptions thrown in PackageManagerInterface::fetchTarget are caught by a
  // try in the same function, so we can only emulate the warning and return
  // value.
  if (fiu_fail("fake_package_download") != 0) {
    const std::string failure_cause = fault_injection_last_info();
    if (!failure_cause.empty()) {
      LOG_WARNING << "Error while downloading a target: " << failure_cause;
    } else {
      LOG_WARNING << "Error while downloading a target: forced failure";
    }
    return false;
  }

  // TODO(OTA-4939): Unify this with the check in
  // SotaUptaneClient::getNewTargets() and make it more generic.
//   if (target.IsOstree()) {
//     LOG_ERROR << "Cannot download OSTree target " << target.filename() << " with the fake package manager!";
//     return false;
//   }

  return PackageManagerInterface::fetchTarget(target, fetcher, keys, progress_cb, token);
}