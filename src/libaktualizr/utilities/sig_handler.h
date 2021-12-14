#ifndef SIG_HANDLER_H
#define SIG_HANDLER_H

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <functional>
#include <mutex>

#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>

class SigHandler {
 public:
  static SigHandler& get();

  SigHandler(const SigHandler&) = delete;
  SigHandler(SigHandler&&) = delete;
  SigHandler& operator=(const SigHandler&) = delete;
  SigHandler& operator=(SigHandler&&) = delete;

  // set an handler for signals and start the handling thread
  void start(const std::function<void()>& on_signal);
  // add hook on signal `sig`
  static void signal(int sig);

  bool masked();
  void mask(int secs);  // send 0 to unmask

 private:
  SigHandler() = default;
  ~SigHandler();
  static void signal_handler(int sig);

  boost::thread polling_thread_;
  static std::atomic_uint signal_marker_;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

  static std::mutex exit_m_;                // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
  static std::condition_variable exit_cv_;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
  static bool exit_flag_;                   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
};

void signal_handler(int sig);

#endif /* SIG_HANDLER_H */
