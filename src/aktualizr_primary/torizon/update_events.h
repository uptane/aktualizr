#ifndef UPDATE_EVENTS_H_
#define UPDATE_EVENTS_H_

#include "libaktualizr/aktualizr.h"

class UpdateEvents {
 public:
  static void processEvent(const std::shared_ptr<event::BaseEvent> &event);
};

#endif  // UPDATE_EVENTS_H_
