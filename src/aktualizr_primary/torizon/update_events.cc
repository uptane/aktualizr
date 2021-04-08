/*
* Aktualizr event handlers
* Complete list of events available in:
* https://github.com/advancedtelematic/aktualizr/blob/master/include/libaktualizr/events.h
*
*/

#include <iostream>
#include "update_events.h"
#include "logging/logging.h"

UpdateEvents* UpdateEvents::instance = nullptr;

UpdateEvents *UpdateEvents::getInstance(Aktualizr *a) {
  if (instance == nullptr)
    instance = new UpdateEvents(a);
  return instance;
}

void UpdateEvents::processAllInstallsComplete() {
  LOG_INFO << "Update install completed. Releasing the update lock...";
  lock.free();
}

void UpdateEvents::processUpdateCheckComplete(const result::UpdateStatus status) {
  lock.free();
  if (status == result::UpdateStatus::kUpdatesAvailable) {
    LOG_INFO << "Update available. Acquiring the update lock...";
    if (lock.try_get() == false)
      aktualizr->DisableUpdates(true);
    else
      aktualizr->DisableUpdates(false);
  }
}

void UpdateEvents::processEvent(const std::shared_ptr<event::BaseEvent> &event) {

  LOG_INFO << "Event: " << event->variant;

  UpdateEvents *e = getInstance(0);

  if (event->variant == "UpdateCheckComplete") {
    const auto *update_available = dynamic_cast<event::UpdateCheckComplete *>(event.get());
    e->processUpdateCheckComplete(update_available->result.status);
  } else if (event->variant == "AllInstallsComplete") {
    e->processAllInstallsComplete();
  }
}
