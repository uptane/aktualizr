#ifndef UPDATE_EVENTS_H_
#define UPDATE_EVENTS_H_

#include "libaktualizr/aktualizr.h"
#include "update_lock.h"

class UpdateEvents {
  boost::filesystem::path update_lock_file = "/run/lock/aktualizr-lock";

  UpdateEvents(Aktualizr *a) : aktualizr(a), lock(update_lock_file) {}

  static UpdateEvents *instance;
  Aktualizr *aktualizr;
  UpdateLock lock;

  void processUpdateCheckComplete(const result::UpdateStatus status);
  void processAllInstallsComplete();

 public:
  static UpdateEvents *getInstance(Aktualizr *a);
  static void processEvent(const std::shared_ptr<event::BaseEvent> &event);
};

#endif  // UPDATE_EVENTS_H_
