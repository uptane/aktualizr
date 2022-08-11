/*
 * Aktualizr event handlers
 * Complete list of events available in:
 * https://github.com/advancedtelematic/aktualizr/blob/master/include/libaktualizr/events.h
 *
 */

#include "update_events.h"
#include <iostream>
#include <string>
#include "logging/logging.h"

// Map event into lambda returning extra information to log as part of processEvent():
static const std::map<std::string, std::function<std::string(const event::BaseEvent *)> > extra_logs_map = {
    {"PutManifestComplete",
     [](const event::BaseEvent *base) -> std::string {
       const auto *event_ptr = dynamic_cast<const event::PutManifestComplete *>(base);
       return event_ptr->success ? "Result - Success" : "Result - Error";
     }},
    {"UpdateCheckComplete",
     [](const event::BaseEvent *base) -> std::string {
       const auto *event_ptr = dynamic_cast<const event::UpdateCheckComplete *>(base);
       if (event_ptr->result.status == result::UpdateStatus::kNoUpdatesAvailable) {
         return "Result - No updates available";
       } else if (event_ptr->result.status == result::UpdateStatus::kUpdatesAvailable) {
         return "Result - Updates available";
       } else if (event_ptr->result.status == result::UpdateStatus::kError) {
         return "Result - Error";
       }
       return "Result - Unknown";
     }},
    {"DownloadProgressReport",
     [](const event::BaseEvent *base) -> std::string {
       const auto *event_ptr = dynamic_cast<const event::DownloadProgressReport *>(base);
       return "Progress at " + std::to_string(event_ptr->progress) + "%";
     }},
    {"DownloadTargetComplete",
     [](const event::BaseEvent *base) -> std::string {
       const auto *event_ptr = dynamic_cast<const event::DownloadTargetComplete *>(base);
       return event_ptr->success ? "Result - Success" : "Result - Error";
     }},
    {"AllDownloadsComplete",
     [](const event::BaseEvent *base) -> std::string {
       const auto *event_ptr = dynamic_cast<const event::AllDownloadsComplete *>(base);
       if (event_ptr->result.status == result::DownloadStatus::kSuccess) {
         return "Result - Success";
       } else if (event_ptr->result.status == result::DownloadStatus::kPartialSuccess) {
         return "Result - Partial success";
       } else if (event_ptr->result.status == result::DownloadStatus::kNothingToDownload) {
         return "Result - Nothing to download";
       } else if (event_ptr->result.status == result::DownloadStatus::kError) {
         return "Result - Error";
       }
       return "Result - Unknown";
     }},
    {"InstallTargetComplete",
     [](const event::BaseEvent *base) -> std::string {
       const auto *event_ptr = dynamic_cast<const event::InstallTargetComplete *>(base);
       return event_ptr->success ? "Result - Success" : "Result - Error";
     }},
    {"AllInstallsComplete",
     [](const event::BaseEvent *base) -> std::string {
       const auto *event_ptr = dynamic_cast<const event::AllInstallsComplete *>(base);
       return "Result - " + event_ptr->result.dev_report.result_code.ToString();
     }},
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
UpdateEvents *UpdateEvents::instance = nullptr;

UpdateEvents *UpdateEvents::getInstance(Aktualizr *a) {
  if (instance == nullptr) {
    instance = new UpdateEvents(a);
  }
  return instance;
}

void UpdateEvents::processAllInstallsComplete() {
  LOG_INFO << "Update install completed. Releasing the update lock...";
  lock.free();
}

void UpdateEvents::processUpdateCheckComplete(result::UpdateStatus status) {
  lock.free();
  if (status == result::UpdateStatus::kUpdatesAvailable) {
    LOG_INFO << "Update available. Acquiring the update lock...";
    if (!lock.try_get()) {
      aktualizr->DisableUpdates(true);
    } else {
      aktualizr->DisableUpdates(false);
    }
  }
}

void UpdateEvents::processEvent(const std::shared_ptr<event::BaseEvent> &event) {
  auto extra = extra_logs_map.find(event->variant);
  if (extra == extra_logs_map.end()) {
    LOG_INFO << "Event: " << event->variant;
  } else {
    // Display extra information for some events.
    LOG_INFO << "Event: " << event->variant << ", " << extra->second(event.get());
  }

  UpdateEvents *e = getInstance(nullptr);

  if (event->variant == event::UpdateCheckComplete::TypeName) {
    const auto *update_event = dynamic_cast<event::UpdateCheckComplete *>(event.get());
    e->processUpdateCheckComplete(update_event->result.status);
  } else if (event->variant == event::AllInstallsComplete::TypeName) {
    e->processAllInstallsComplete();
  }
}
