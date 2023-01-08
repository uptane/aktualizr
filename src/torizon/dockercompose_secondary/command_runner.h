#ifndef COMMAND_RUNNER_H_
#define COMMAND_RUNNER_H_

#include <string>
#include <vector>
#include "utilities/flow_control.h"

class CommandRunner {
 public:
  static bool run(const std::string& cmd, const api::FlowControlToken* flow_control = nullptr);
  static std::vector<std::string> runResult(const std::string& cmd);
};

#endif  // COMMAND_RUNNER_H_
