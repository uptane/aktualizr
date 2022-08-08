#ifndef UPDATE_LOCK_H_
#define UPDATE_LOCK_H_

#include "libaktualizr/aktualizr.h"

class UpdateLock {
  boost::filesystem::path lockfile;
  int lockdesc;

 public:
  UpdateLock(boost::filesystem::path lock) : lockfile(lock), lockdesc(0) {}
  ~UpdateLock();

  bool get(bool block = true);
  bool try_get();
  bool free();
};

#endif  // UPDATE_EVENTS_H_
