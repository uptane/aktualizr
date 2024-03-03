#include "utilities/flow_control.h"

#include <cassert>

namespace api {

bool FlowControlToken::IsValid() const { return sentinel_ == SENTINEL; }

bool FlowControlToken::setPause(bool set_paused) {
  assert(IsValid());
  {
    std::lock_guard<std::mutex> lock(m_);
    if (set_paused && state_ == State::kRunning) {
      state_ = State::kPaused;
    } else if (!set_paused && state_ == State::kPaused) {
      state_ = State::kRunning;
    } else {
      return false;
    }
  }
  cv_.notify_all();
  return true;
}

bool FlowControlToken::setAbort() {
  assert(IsValid());

  {
    std::lock_guard<std::mutex> g(m_);
    if (state_ == State::kAborted) {
      return false;
    }
    state_ = State::kAborted;
  }
  cv_.notify_all();
  return true;
}

bool FlowControlToken::canContinue(bool blocking) const {
  assert(IsValid());
  std::unique_lock<std::mutex> lk(m_);
  if (blocking) {
    cv_.wait(lk, [this] { return state_ != State::kPaused; });
  }
  return state_ == State::kRunning;
}

bool FlowControlToken::hasAborted() const {
  assert(IsValid());
  return !canContinue(false);
}

void FlowControlToken::reset() {
  assert(IsValid());

  std::lock_guard<std::mutex> g(m_);
  state_ = State::kRunning;
}
}  // namespace api