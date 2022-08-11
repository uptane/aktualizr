#ifndef COMMAND_RUNNER_H_
#define COMMAND_RUNNER_H_

#include <string>
#include <vector>

class CommandRunner {
 public:
  CommandRunner() = default;

  bool run(const std::string& cmd);
  std::vector<std::string> runResult(const std::string& cmd);
};

#endif  // COMMAND_RUNNER_H_
