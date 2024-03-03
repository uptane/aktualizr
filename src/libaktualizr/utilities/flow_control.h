#ifndef AKTUALIZR_FLOW_CONTROL_H
#define AKTUALIZR_FLOW_CONTROL_H

#include <condition_variable>
#include <mutex>

namespace api {

///
/// Provides a thread-safe way to pause and terminate task execution.
/// A task must call canContinue() method to check the current state.
///
class FlowControlToken {
 public:
  FlowControlToken() = default;
  ~FlowControlToken() = default;

  // Non-copyable, non,moveable
  FlowControlToken(const FlowControlToken&) = delete;
  FlowControlToken& operator=(const FlowControlToken&) = delete;
  FlowControlToken(FlowControlToken&&) = delete;
  FlowControlToken& operator=(FlowControlToken&&) = delete;

  /// After casting from a void pointer check this is true before continuing
  /// \return `true` if this really does point to a FlowControlToken
  bool IsValid() const;

  ///
  /// Called by the controlling thread to request the task to pause or resume.
  /// Has no effect if the task was aborted.
  /// @return `true` if the state was changed, `false` otherwise.
  ///
  bool setPause(bool set_paused);

  ///
  /// Called by the controlling thread to request the task to abort.
  /// @return `false` if the task was already aborted, `true` otherwise.
  ///
  bool setAbort();

  ///
  /// Called by the controlled thread to query the currently requested state.
  /// Sleeps if the state is `Paused` and `blocking == true`.
  /// @return `true` for `Running` state, `false` for `Aborted`,
  /// and also `false` for the `Paused` state, if the call is non-blocking.
  ///
  bool canContinue(bool blocking = true) const;

  ///
  /// \return true if the operation has aborted and we should stop trying to make progress and start aborting
  bool hasAborted() const;

  ////
  //// Sets token to the initial state
  ////
  void reset();

 private:
  static const uint32_t SENTINEL = 0xced53470;
  const uint32_t sentinel_{SENTINEL};
  enum class State {
    kRunning,  // transitions: ->Paused, ->Aborted
    kPaused,   // transitions: ->Running, ->Aborted
    kAborted   // transitions: none
  } state_{State::kRunning};
  mutable std::mutex m_;
  mutable std::condition_variable cv_;
};

}  // namespace api
#endif  // AKTUALIZR_FLOW_CONTROL_H
