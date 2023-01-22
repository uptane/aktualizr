#ifndef COMPOSE_MANAGER_H_
#define COMPOSE_MANAGER_H_

#include <boost/filesystem/path.hpp>
#include <string>
#include "command_runner.h"
#include "libaktualizr/types.h"
#include "utilities/flow_control.h"

class ComposeManager {
 public:
  ComposeManager();

  bool pull(const boost::filesystem::path &compose_file, const api::FlowControlToken *flow_control);
  bool up(const boost::filesystem::path &compose_file);
  bool down(const boost::filesystem::path &compose_file);
  bool cleanup();
  bool checkRollback();

 private:
  const std::string compose_cmd_;
  const std::string docker_cmd_;
  const std::string check_rollback_cmd_;
};

#endif  // COMPOSE_MANAGER_H_
