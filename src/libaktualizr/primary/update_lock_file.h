#ifndef UPDATE_LOCK_FILE_H_
#define UPDATE_LOCK_FILE_H_

#include <boost/filesystem/path.hpp>

class UpdateLockFile {
 public:
  enum LockResult { kGoAhead, kNoUpdate };
  explicit UpdateLockFile(const boost::filesystem::path &lock_file);
  ~UpdateLockFile();
  // Non-copyable
  UpdateLockFile(const UpdateLockFile &) = delete;
  UpdateLockFile operator=(const UpdateLockFile &) = delete;
  // It would be possible to implement move, but we don't need it
  UpdateLockFile(UpdateLockFile &&) = delete;
  UpdateLockFile operator=(UpdateLockFile &&) = delete;

  /**
   * Called when we are about to start an update.
   */
  LockResult ShouldUpdate();
  void UpdateComplete();

 private:
  boost::filesystem::path lock_file_;
  int lock_file_descriptor_{-1};
  bool is_locked_{false};
};

#endif  // UPDATE_LOCK_FILE_H_
