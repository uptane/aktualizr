// TODO: Review: Maybe this module could be absorbed by compose_manager or dockercomposesecondary.
#include <boost/process.hpp>

#include "command_runner.h"
#include "logging/logging.h"

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool CommandRunner::run(const std::string& cmd, const api::FlowControlToken* flow_control) {
  LOG_INFO << "Running command: " << cmd;
  boost::process::child c(cmd);

  while (!c.wait_for(std::chrono::milliseconds(100))) {
    if (flow_control != nullptr && flow_control->hasAborted()) {
      LOG_INFO << "Killing child process due to flow_control abort";
      auto pid = c.id();
      kill(pid, SIGTERM);
      // Give it 30s to exit cleanly
      if (!c.wait_for(std::chrono::seconds(30))) {
        LOG_WARNING << "Process didn't respond to SIGTERM, sending SIGKILL";
        c.terminate();
      }
      return false;
    }
  }

  return c.exit_code() == 0;
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
