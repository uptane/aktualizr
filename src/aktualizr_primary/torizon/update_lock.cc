#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "logging/logging.h"
#include "update_lock.h"

bool UpdateLock::get(bool block) {
  int flags = LOCK_EX;

  /* Open the lock file. If for some reason the lock file cannot be opened,
   * we won't lock updates to prevent situations that could make the device
   * no longer updateable, for example if the lock directory does not exist
   * or is not writeable.
   */
  if (lockdesc == 0) {
    lockdesc = open(lockfile.c_str(), O_RDWR | O_CREAT | O_APPEND, 0666);
    if (lockdesc < 0) {
      LOG_ERROR << "Unable to open lock file: " << lockfile;
      lockdesc = 0;
      return true;
    }
  }

  if (!block) {
    flags |= LOCK_NB;
  }

  if (flock(lockdesc, flags) < 0) {
    LOG_ERROR << "Unable to acquire lock: " << lockfile;
    return false;
  }

  return true;
}

bool UpdateLock::try_get() { return get(false); }

bool UpdateLock::free() {
  if (lockdesc == 0) {
    return false;
  }

  if (flock(lockdesc, LOCK_UN) < 0) {
    LOG_INFO << "Unable to release lock: " << lockfile;
    return false;
  }

  return true;
}

UpdateLock::~UpdateLock() {
  if (lockdesc != 0) {
    close(lockdesc);
  }
}
