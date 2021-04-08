#include <boost/process.hpp>

#include "command_runner.h"
#include "logging/logging.h"

bool CommandRunner::run(const std::string& cmd) {
  LOG_INFO << "Running command: " << cmd;
  return boost::process::system(cmd) == 0;
}
