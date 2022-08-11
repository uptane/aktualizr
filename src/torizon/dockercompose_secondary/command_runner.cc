// TODO: Review: Maybe this module could be absorbed by compose_manager or dockercomposesecondary.
#include <boost/process.hpp>

#include "command_runner.h"
#include "logging/logging.h"

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool CommandRunner::run(const std::string& cmd) {
  LOG_INFO << "Running command: " << cmd;
  return boost::process::system(cmd) == 0;
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
