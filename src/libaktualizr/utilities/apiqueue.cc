#include "apiqueue.h"
#include "logging/logging.h"

namespace api {

CommandQueue::~CommandQueue() {
  try {
    abort(false);
  } catch (std::exception& ex) {
    LOG_ERROR << "~CommandQueue() exception: " << ex.what() << std::endl;
  } catch (...) {
    LOG_ERROR << "~CommandQueue() unknown exception" << std::endl;
  }
}

void CommandQueue::run() {
  std::lock_guard<std::mutex> g(thread_m_);
  if (!thread_.joinable()) {
    thread_ = std::thread([this] {
      Context ctx{.flow_control = &token_};
      std::unique_lock<std::mutex> lock(m_);
      for (;;) {
        cv_.wait(lock, [this] { return (!queue_.empty() && !paused_) || shutdown_; });
        if (shutdown_) {
          break;
        }
        auto task = std::move(queue_.front());
        queue_.pop();
        lock.unlock();
        task->PerformTask(&ctx);
        lock.lock();
      }
    });
  }
}

bool CommandQueue::pause(bool do_pause) {
  bool has_effect;
  {
    std::lock_guard<std::mutex> lock(m_);
    has_effect = paused_ != do_pause;
    paused_ = do_pause;
    token_.setPause(do_pause);
  }
  cv_.notify_all();

  return has_effect;
}

void CommandQueue::abort(bool restart_thread) {
  {
    std::lock_guard<std::mutex> thread_g(thread_m_);
    {
      std::lock_guard<std::mutex> g(m_);
      token_.setAbort();
      shutdown_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
    {
      // Flush the queue and reset to initial state
      std::lock_guard<std::mutex> g(m_);
      std::queue<ICommand::Ptr>().swap(queue_);
      token_.reset();
      shutdown_ = false;
    }
  }
  if (restart_thread) {
    run();
  }
}

void CommandQueue::enqueue(ICommand::Ptr&& task) {
  {
    std::lock_guard<std::mutex> lock(m_);
    queue_.push(std::move(task));
  }
  cv_.notify_all();
}

}  // namespace api
