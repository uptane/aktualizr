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

#include "utilities/flow_control.h"

namespace api {

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

  const api::FlowControlToken* FlowControlToken() const { return &token_; }

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
  class api::FlowControlToken token_;
};

}  // namespace api
#endif  // AKTUALIZR_APIQUEUE_H
