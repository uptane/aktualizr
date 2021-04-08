#ifndef COMMAND_RUNNER_H_
#define COMMAND_RUNNER_H_

#include <string>

class CommandRunner {

 public:
  CommandRunner() {}

  bool run(const std::string& cmd);
};

#endif  // COMMAND_RUNNER_H_
