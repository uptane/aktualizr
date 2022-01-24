#ifndef AKTUALIZR_APIQUEUE_H
#define AKTUALIZR_APIQUEUE_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

namespace api {

///
/// Provides a thread-safe way to pause and terminate task execution.
/// A task must call canContinue() method to check the current state.
///
class FlowControlToken {
 public:
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

  ////
  //// Sets token to the initial state
  ////
  void reset();

 private:
  enum class State {
    kRunning,  // transitions: ->Paused, ->Aborted
    kPaused,   // transitions: ->Running, ->Aborted
    kAborted   // transitions: none
  } state_{State::kRunning};
  mutable std::mutex m_;
  mutable std::condition_variable cv_;
};

struct Context {
  api::FlowControlToken* flow_control;
};

class ICommand {
 public:
  using Ptr = std::shared_ptr<ICommand>;
  ICommand() = default;
  virtual ~ICommand() = default;
  // Non-movable-non-copyable
  ICommand(const ICommand&) = delete;
  ICommand(ICommand&&) = delete;
  ICommand& operator=(const ICommand&) = delete;
  ICommand& operator=(ICommand&&) = delete;

  virtual void PerformTask(Context* ctx) = 0;
};

template <class T>
class CommandBase : public ICommand {
 public:
  void PerformTask(Context* ctx) override {
    try {
      result_.set_value(TaskImplementation(ctx));
    } catch (...) {
      result_.set_exception(std::current_exception());
    }
  }

  std::future<T> GetFuture() { return result_.get_future(); }

 protected:
  virtual T TaskImplementation(Context*) = 0;

 private:
  std::promise<T> result_;
};

template <>
class CommandBase<void> : public ICommand {
 public:
  void PerformTask(Context* ctx) override {
    try {
      TaskImplementation(ctx);
      result_.set_value();
    } catch (...) {
      result_.set_exception(std::current_exception());
    }
  }

  std::future<void> GetFuture() { return result_.get_future(); }

 protected:
  virtual void TaskImplementation(Context*) = 0;

 private:
  std::promise<void> result_;
};

template <class T>
class Command : public CommandBase<T> {
 public:
  explicit Command(std::function<T()>&& func) : f_{move(func)} {}
  T TaskImplementation(Context* ctx) override {
    (void)ctx;
    return f_();
  }

 private:
  std::function<T()> f_;
};

template <class T>
class CommandFlowControl : public CommandBase<T> {
 public:
  explicit CommandFlowControl(std::function<T(const api::FlowControlToken*)>&& func) : f_{move(func)} {}
  T TaskImplementation(Context* ctx) override { return f_(ctx->flow_control); }

 private:
  std::function<T(const api::FlowControlToken*)> f_;
};

class CommandQueue {
 public:
  CommandQueue() = default;
  ~CommandQueue();
  // Non-copyable Non-movable
  CommandQueue(const CommandQueue&) = delete;
  CommandQueue(CommandQueue&&) = delete;
  CommandQueue& operator=(const CommandQueue&) = delete;
  CommandQueue& operator=(CommandQueue&&) = delete;
  void run();
  bool pause(bool do_pause);  // returns true iff pause→resume or resume→pause
  void abort(bool restart_thread = true);

  template <class R>
  std::future<R> enqueue(std::function<R()>&& function) {
    auto task = std::make_shared<Command<R>>(std::move(function));
    enqueue(task);
    return task->GetFuture();
  }

  template <class R>
  std::future<R> enqueue(std::function<R(const api::FlowControlToken*)>&& function) {
    auto task = std::make_shared<CommandFlowControl<R>>(std::move(function));
    enqueue(task);
    return task->GetFuture();
  }

  void enqueue(ICommand::Ptr&& task);

 private:
  std::atomic_bool shutdown_{false};
  std::atomic_bool paused_{false};

  std::thread thread_;
  std::mutex thread_m_;

  std::queue<ICommand::Ptr> queue_;
  std::mutex m_;
  std::condition_variable cv_;
  FlowControlToken token_;
};

}  // namespace api
#endif  // AKTUALIZR_APIQUEUE_H
