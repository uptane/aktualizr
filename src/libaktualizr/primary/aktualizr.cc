#include <boost/filesystem.hpp>
#include <chrono>

#include <sodium.h>

#include "libaktualizr/aktualizr.h"
#include "libaktualizr/events.h"
#include "primary/sotauptaneclient.h"
#include "utilities/apiqueue.h"
#include "utilities/timer.h"

using std::make_shared;
using std::move;
using std::shared_ptr;

namespace bf = boost::filesystem;

Aktualizr::Aktualizr(const Config &config)
    : Aktualizr(config, INvStorage::newStorage(config.storage), std::make_shared<HttpClient>()) {}

Aktualizr::Aktualizr(Config config, std::shared_ptr<INvStorage> storage_in,
                     const std::shared_ptr<HttpInterface> &http_in)
    : config_{move(config)}, sig_{new event::Channel()}, api_queue_{new api::CommandQueue()} {
  if (sodium_init() == -1) {  // Note that sodium_init doesn't require a matching 'sodium_deinit'
    throw std::runtime_error("Unable to initialize libsodium");
  }

  storage_ = move(storage_in);
  storage_->importData(config_.import);

  uptane_client_ = std::make_shared<SotaUptaneClient>(config_, storage_, http_in, sig_);

  updates_disabled_ = false;
}

Aktualizr::~Aktualizr() { api_queue_.reset(nullptr); }

void Aktualizr::Initialize() {
  uptane_client_->initialize();
  api_queue_->run();
}

void Aktualizr::DisableUpdates(bool status) { updates_disabled_ = status; }

bool Aktualizr::UptaneCycle() {
  result::UpdateCheck update_result = CheckUpdates().get();
  if (update_result.updates.empty() || updates_disabled_) {
    if (update_result.status == result::UpdateStatus::kError) {
      // If the metadata verification failed, inform the backend immediately.
      SendManifest().get();
    }
    return true;
  }

  result::Download download_result = Download(update_result.updates).get();
  if (download_result.status != result::DownloadStatus::kSuccess || download_result.updates.empty()) {
    if (download_result.status != result::DownloadStatus::kNothingToDownload) {
      // If the download failed, inform the backend immediately.
      SendManifest().get();
    }
    return true;
  }

  Install(download_result.updates).get();

  if (uptane_client_->isInstallCompletionRequired()) {
    // If there are some pending updates then effectively either reboot (OSTree) or aktualizr restart (fake pack mngr)
    // is required to apply the update(s)
    LOG_INFO << "Exiting aktualizr so that pending updates can be applied after reboot";
    return false;
  }

  if (!uptane_client_->hasPendingUpdates()) {
    // If updates were applied and no any reboot/finalization is required then send/put manifest
    // as soon as possible, don't wait for config_.uptane.polling_sec
    SendManifest().get();
  }

  return true;
}

std::future<void> Aktualizr::RunForever() {
  std::future<void> future = std::async(std::launch::async, [this]() {
    // TODO: [OFFUPD] Move to inside the loop if throwing an error (hopefully not needed).
    CompleteSecondaryUpdates().get();

    std::unique_lock<std::mutex> l(exit_cond_.m);
    bool have_sent_device_data = false;
    while (true) {
      try {
        if (!have_sent_device_data) {
          // Can throw SotaUptaneClient::ProvisioningFailed
          SendDeviceData().get();
          have_sent_device_data = true;
        }

        // TODO: [OFFUPD] The "!enable_offline_updates" below should be removed after the MVP.
        if (config_.uptane.enable_online_updates && !config_.uptane.enable_offline_updates) {
          if (!UptaneCycle()) {
            break;
          }
        }
      } catch (SotaUptaneClient::ProvisioningFailed &e) {
        LOG_DEBUG << "Not provisioned yet: " << e.what();
      }

#if 1  // TODO: [OFFUPD] #ifdef BUILD_OFFLINE_UPDATES
      if (config_.uptane.enable_offline_updates) {
        // Check update directory while waiting for next polling cycle.
        bool quit = false;
        for (auto loop = config_.uptane.polling_sec; loop > 0; loop--) {
          try {
            if (OfflineUpdateAvailable()) {
              if (!CheckAndInstallOffline(config_.uptane.offline_updates_source)) {
                quit = true;
              }
            }
          } catch (SotaUptaneClient::ProvisioningFailed &e) {
            // This should never happen in the offline-updates call-chain.
            LOG_INFO << "Offline-update loop: Not provisioned yet: " << e.what();
          }
          if (exit_cond_.cv.wait_for(l, std::chrono::seconds(1), [this] { return exit_cond_.flag; })) {
            quit = true;
            break;
          }
        }
        if (quit) {
          break;
        }
      } else
#endif
      {
        // Wait for next polling cycle.
        if (exit_cond_.cv.wait_for(l, std::chrono::seconds(config_.uptane.polling_sec),
                                   [this] { return exit_cond_.flag; })) {
          break;
        }
      }
    }
    // FIXME: Is it correct to call a method of the uptane_client w/o going through the CommandQueue?
    uptane_client_->completeInstall();
  });
  return future;
}

void Aktualizr::Shutdown() {
  {
    std::lock_guard<std::mutex> g(exit_cond_.m);
    exit_cond_.flag = true;
  }
  exit_cond_.cv.notify_all();
}

void Aktualizr::AddSecondary(const std::shared_ptr<SecondaryInterface> &secondary) {
  uptane_client_->addSecondary(secondary);
}

void Aktualizr::SetSecondaryData(const Uptane::EcuSerial &ecu, const std::string &data) {
  storage_->saveSecondaryData(ecu, data);
}

std::vector<SecondaryInfo> Aktualizr::GetSecondaries() const {
  std::vector<SecondaryInfo> info;
  storage_->loadSecondariesInfo(&info);

  return info;
}

std::future<result::CampaignCheck> Aktualizr::CampaignCheck() {
  std::function<result::CampaignCheck()> task([this] { return uptane_client_->campaignCheck(); });
  return api_queue_->enqueue(move(task));
}

std::future<void> Aktualizr::CampaignControl(const std::string &campaign_id, campaign::Cmd cmd) {
  std::function<void()> task([this, campaign_id, cmd] {
    switch (cmd) {
      case campaign::Cmd::Accept:
        uptane_client_->campaignAccept(campaign_id);
        break;
      case campaign::Cmd::Decline:
        uptane_client_->campaignDecline(campaign_id);
        break;
      case campaign::Cmd::Postpone:
        uptane_client_->campaignPostpone(campaign_id);
        break;
      default:
        break;
    }
  });
  return api_queue_->enqueue(move(task));
}

void Aktualizr::SetCustomHardwareInfo(Json::Value hwinfo) { uptane_client_->setCustomHardwareInfo(move(hwinfo)); }
std::future<void> Aktualizr::SendDeviceData() {
  std::function<void()> task([this] { uptane_client_->sendDeviceData(); });
  return api_queue_->enqueue(move(task));
}

// FIXME: [TDX] This solution must be reviewed (we should probably have a method to be used just for the data proxy).
std::future<void> Aktualizr::SendDeviceData(const Json::Value &hwinfo) {
  std::function<void()> task([this, hwinfo] {
    uptane_client_->setCustomHardwareInfo(hwinfo);
    uptane_client_->sendDeviceData();
  });
  return api_queue_->enqueue(move(task));
}

std::future<void> Aktualizr::CompleteSecondaryUpdates() {
  std::function<void()> task([this] { return uptane_client_->completePreviousSecondaryUpdates(); });
  return api_queue_->enqueue(move(task));
}

std::future<result::UpdateCheck> Aktualizr::CheckUpdates() {
  std::function<result::UpdateCheck()> task([this] { return uptane_client_->fetchMeta(); });
  return api_queue_->enqueue(move(task));
}

std::future<result::Download> Aktualizr::Download(const std::vector<Uptane::Target> &updates) {
  std::function<result::Download(const api::FlowControlToken *)> task(
      [this, updates](const api::FlowControlToken *token) { return uptane_client_->downloadImages(updates, token); });
  return api_queue_->enqueue(move(task));
}

std::future<result::Install> Aktualizr::Install(const std::vector<Uptane::Target> &updates) {
  std::function<result::Install()> task([this, updates] { return uptane_client_->uptaneInstall(updates); });
  return api_queue_->enqueue(move(task));
}

bool Aktualizr::SetInstallationRawReport(const std::string &custom_raw_report) {
  return storage_->storeDeviceInstallationRawReport(custom_raw_report);
}

std::future<bool> Aktualizr::SendManifest(const Json::Value &custom) {
  std::function<bool()> task([this, custom]() { return uptane_client_->putManifest(custom); });
  return api_queue_->enqueue(move(task));
}

result::Pause Aktualizr::Pause() {
  if (api_queue_->pause(true)) {
    uptane_client_->reportPause();
    return result::PauseStatus::kSuccess;
  } else {
    return result::PauseStatus::kAlreadyPaused;
  }
}

result::Pause Aktualizr::Resume() {
  if (api_queue_->pause(false)) {
    uptane_client_->reportResume();
    return result::PauseStatus::kSuccess;
  } else {
    return result::PauseStatus::kAlreadyRunning;
  }
}

void Aktualizr::Abort() { api_queue_->abort(); }

boost::signals2::connection Aktualizr::SetSignalHandler(
    const std::function<void(shared_ptr<event::BaseEvent>)> &handler) {
  return sig_->connect(handler);
}

Aktualizr::InstallationLog Aktualizr::GetInstallationLog() {
  std::vector<Aktualizr::InstallationLogEntry> ilog;

  EcuSerials serials;
  if (!uptane_client_->getEcuSerials(&serials)) {
    throw std::runtime_error("Could not load ECU serials");
  }

  ilog.reserve(serials.size());
  for (const auto &s : serials) {
    Uptane::EcuSerial serial = s.first;
    std::vector<Uptane::Target> installs;

    std::vector<Uptane::Target> log;
    storage_->loadInstallationLog(serial.ToString(), &log, true);

    ilog.emplace_back(Aktualizr::InstallationLogEntry{serial, move(log)});
  }

  return ilog;
}

std::vector<Uptane::Target> Aktualizr::GetStoredTargets() { return uptane_client_->getStoredTargets(); }

void Aktualizr::DeleteStoredTarget(const Uptane::Target &target) { uptane_client_->deleteStoredTarget(target); }

std::ifstream Aktualizr::OpenStoredTarget(const Uptane::Target &target) {
  return uptane_client_->openStoredTarget(target);
}

#ifdef BUILD_OFFLINE_UPDATES
bool Aktualizr::OfflineUpdateAvailable() {
  static const std::string update_subdir{"metadata"};

  OffUpdSourceState old_state = offupd_source_state_;
  OffUpdSourceState cur_state = OffUpdSourceState::Unknown;

  if (bf::exists(config_.uptane.offline_updates_source)) {
    if (bf::is_directory(config_.uptane.offline_updates_source / update_subdir)) {
      cur_state = OffUpdSourceState::SourceExists;
    } else {
      cur_state = OffUpdSourceState::SourceExistsNoContent;
    }
  } else {
    cur_state = OffUpdSourceState::SourceDoesNotExist;
  }

  offupd_source_state_ = cur_state;
  // LOG_INFO << "OfflineUpdateAvailable: " << int(old_state) << " -> " << int(cur_state);

  return (old_state == OffUpdSourceState::SourceDoesNotExist && cur_state == OffUpdSourceState::SourceExists);
}

std::future<result::UpdateCheck> Aktualizr::CheckUpdatesOffline(const boost::filesystem::path &source_path) {
  std::function<result::UpdateCheck()> task(
      [this, source_path] { return uptane_client_->fetchMetaOffUpd(source_path); });
  return api_queue_->enqueue(move(task));
}

std::future<result::Download> Aktualizr::FetchImagesOffline(const std::vector<Uptane::Target> &updates) {
  std::function<result::Download(const api::FlowControlToken *)> task(
      [this, updates](const api::FlowControlToken *token) {
        return uptane_client_->fetchImagesOffUpd(updates, token);
      });
  return api_queue_->enqueue(move(task));
}

std::future<result::Install> Aktualizr::InstallOffline(const std::vector<Uptane::Target> &updates) {
  std::function<result::Install()> task([this, updates] { return uptane_client_->uptaneInstallOffUpd(updates); });
  return api_queue_->enqueue(move(task));
}

bool Aktualizr::CheckAndInstallOffline(const boost::filesystem::path &source_path) {
  // TODO: [OFFUPD] Handle interaction between offline and online modes.

  LOG_TRACE << "CheckAndInstallOffline: call CheckUpdatesOffline";
  result::UpdateCheck update_result = CheckUpdatesOffline(source_path).get();
  if (update_result.updates.empty() || updates_disabled_) {
    // TODO: [OFFUPD] Do we need this?
    // if (update_result.status == result::UpdateStatus::kError) {
    //   // If the metadata verification failed, inform the backend immediately.
    //   SendManifest().get();
    // }
    return true;
  }

  LOG_TRACE << "CheckAndInstallOffline: call FetchImagesOffline";
  result::Download download_result = FetchImagesOffline(update_result.updates).get();
  if (download_result.status != result::DownloadStatus::kSuccess || download_result.updates.empty()) {
    // TODO: [OFFUPD] Do we need this?
    // if (download_result.status != result::DownloadStatus::kNothingToDownload) {
    //   // If the download failed, inform the backend immediately.
    //   SendManifest().get();
    // }
    return true;
  }

  LOG_TRACE << "CheckAndInstallOffline: call InstallOffline";
  InstallOffline(download_result.updates).get();

  // TODO: [OFFUPD] Do we need this?
  if (uptane_client_->isInstallCompletionRequired()) {
    // If there are some pending updates then effectively either reboot (OSTree) or restart
    // aktualizr (fake pack mngr) to apply the update(s)
    LOG_INFO << "Exiting aktualizr so that pending updates can be applied after reboot";
    return false;
  }

  // TODO: [OFFUPD] Do we need this?
  // if (!uptane_client_->hasPendingUpdates()) {
  //   // If updates were applied and no any reboot/finalization is required then send/put
  //   // manifestas soon as possible
  //   SendManifest().get();
  // }

  return true;
}
#endif
