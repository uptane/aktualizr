// TODO: Review: Maybe this module could be absorbed by compose_manager or dockercomposesecondary.
#include <boost/process.hpp>

#include "command_runner.h"
#include "logging/logging.h"

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool CommandRunner::run(const std::string& cmd, const api::FlowControlToken* flow_control) {
  LOG_INFO << "Running command: " << cmd;
  std::error_code err;
  boost::process::group process_group;
  boost::process::child child_process(cmd, process_group, err);

  if (err) {
    LOG_WARNING << "Failed to start " << cmd;
    return false;
  }

  while (!child_process.wait_for(std::chrono::milliseconds(100))) {
    if (flow_control != nullptr && flow_control->hasAborted()) {
      LOG_INFO << "Killing child process due to flow_control abort";
      auto pid = child_process.id();
      if (kill(-pid, SIGINT) != 0) {
        LOG_WARNING << "Attempt to send SIGINT to pid " << pid << " failed with " << strerror(errno);
      }
      // Give it 5s to exit
      if (!child_process.wait_for(std::chrono::seconds(5))) {
        LOG_WARNING << "Process didn't respond to SIGINT, sending SIGTERM";
        if (kill(-pid, SIGTERM) != 0) {
          LOG_WARNING << "Attempt to send SIGINT to pid " << pid << " failed with " << strerror(errno);
        }
      }
      // Give it another 25s to exit cleanly
      if (!child_process.wait_for(std::chrono::seconds(25))) {
        LOG_WARNING << "Process didn't respond to SIGTERM, sending SIGKILL";
        child_process.terminate();
      }
      child_process.terminate();
      return false;
    }
  }
  child_process.wait();
  return child_process.exit_code() == 0;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::vector<std::string> CommandRunner::runResult(const std::string& cmd) {
  // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
  LOG_INFO << "Running command: " << cmd;
  boost::process::ipstream pipe;
  // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
  boost::process::child c(cmd, boost::process::std_out > pipe);

  std::vector<std::string> result;
  std::string line;

  // TODO: Review: log sometimes seems to be lost. E.g.
  // ...aktualizr-torizon[817]: emoving not used containers, networks an
  // ...aktualizr-torizon[1396]: emoving not used containers, networks an
  while (c.running() && std::getline(pipe, line) && !line.empty()) {
    result.push_back(line);
  }

  c.wait();

  return result;
}
