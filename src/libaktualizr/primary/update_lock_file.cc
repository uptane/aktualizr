#include "primary/update_lock_file.h"

#include <asm-generic/errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>

#include "logging/logging.h"

// NOLINTNEXTLINE(modernize-pass-by-value) The source of this is a long-lived config object
UpdateLockFile::UpdateLockFile(const boost::filesystem::path &lock_file) : lock_file_{lock_file} {}

UpdateLockFile::~UpdateLockFile() {
  if (lock_file_descriptor_ >= 0) {
    close(lock_file_descriptor_);
  }
}

UpdateLockFile::LockResult UpdateLockFile::ShouldUpdate() {
  if (lock_file_.empty()) {
    return kGoAhead;
  }
  if (is_locked_) {
    return kGoAhead;
  }
  /* Open the lock file. If for some reason the lock file cannot be opened,
   * we won't lock updates to prevent situations that could make the device
   * no longer updateable, for example if the lock directory does not exist
   * or is not writeable.
   * Do this here (late initialization) in case the file is created after Aktualizr starts.
   */
  if (lock_file_descriptor_ == -1) {
    int res = open(lock_file_.c_str(), O_RDWR | O_CREAT | O_APPEND, 0666);
    if (res < 0) {
      if (errno == ENOENT) {
        LOG_DEBUG << "Lock file " << lock_file_ << " not present, continuing installation";
      } else {
        LOG_WARNING << "Opening lock file " << lock_file_ << " failed " << strerror(errno);
      }
      is_locked_ = true;
      return kGoAhead;
    }
    lock_file_descriptor_ = res;
  }

  int flags = LOCK_EX | LOCK_NB;
  if (flock(lock_file_descriptor_, flags) < 0) {
    if (errno == EWOULDBLOCK) {
      LOG_INFO << "Skipping update because lock on " << lock_file_ << " is held";
    } else {
      LOG_ERROR << "flock on " << lock_file_ << " failed:" << strerror(errno);
    }
    return kNoUpdate;
  }
  is_locked_ = true;
  return kGoAhead;
}

void UpdateLockFile::UpdateComplete() {
  // We call Release() any time Aktualizr is in the idle state. This is a fast
  // path to avoid system calls in that case.
  if (!is_locked_) {
    return;
  }
  is_locked_ = false;
  if (lock_file_descriptor_ < 0) {
    return;
  }
  if (flock(lock_file_descriptor_, LOCK_UN) < 0) {
    LOG_INFO << "Unable to release lock: " << lock_file_;
  }
}
