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

  while (c.running() && std::getline(pipe, line) && !line.empty())
      result.push_back(line);

  c.wait();

  return result;
}
