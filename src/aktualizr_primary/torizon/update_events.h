#ifndef UPDATE_EVENTS_H_
#define UPDATE_EVENTS_H_

#include "libaktualizr/aktualizr.h"
#include "update_lock.h"

class UpdateEvents {
 private:
  boost::filesystem::path update_lock_file = "/run/lock/aktualizr-lock";

  explicit UpdateEvents(Aktualizr *a) : aktualizr(a), lock(update_lock_file) {}

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static UpdateEvents *instance;
  Aktualizr *aktualizr;
  UpdateLock lock;

  void processUpdateCheckComplete(result::UpdateStatus status);
  void processAllInstallsComplete();

 public:
  static UpdateEvents *getInstance(Aktualizr *a);
  static void processEvent(const std::shared_ptr<event::BaseEvent> &event);
};

#endif  // UPDATE_EVENTS_H_
