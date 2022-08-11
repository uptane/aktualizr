#ifndef UPDATE_LOCK_H_
#define UPDATE_LOCK_H_

#include "libaktualizr/aktualizr.h"

class UpdateLock {
  boost::filesystem::path lockfile;
  int lockdesc;

 public:
  explicit UpdateLock(boost::filesystem::path lock) : lockfile(std::move(lock)), lockdesc(0) {}
  UpdateLock(const UpdateLock&) = delete;
  UpdateLock(UpdateLock&&) = delete;
  UpdateLock& operator=(const UpdateLock&) = delete;
  UpdateLock& operator=(UpdateLock&&) = delete;
  ~UpdateLock();

  bool get(bool block = true);
  bool try_get();
  bool free();
};

#endif  // UPDATE_EVENTS_H_
