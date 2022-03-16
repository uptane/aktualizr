// TODO: Review: Maybe this module could be absorbed by compose_manager or dockercomposesecondary.
// TODO: Review: This module is used by the secondary but is in the primary directory.
#include <boost/process.hpp>

#include "command_runner.h"
#include "logging/logging.h"

bool CommandRunner::run(const std::string& cmd) {
  LOG_INFO << "Running command: " << cmd;
  return boost::process::system(cmd) == 0;
}

std::vector<std::string> CommandRunner::runResult(const std::string& cmd) {
  LOG_INFO << "Running command: " << cmd;
  boost::process::ipstream pipe;
  boost::process::child c(cmd, boost::process::std_out > pipe);

  std::vector<std::string> result;
  std::string line;

  // TODO: Review: log sometimes seems to be lost. E.g.
  // ...aktualizr-torizon[817]: emoving not used containers, networks an
  // ...aktualizr-torizon[1396]: emoving not used containers, networks an
  while (c.running() && std::getline(pipe, line) && !line.empty())
      result.push_back(line);

  c.wait();

  return result;
}
