#ifndef COMPOSE_MANAGER_H_
#define COMPOSE_MANAGER_H_

#include <string>
#include "command_runner.h"

class ComposeManager {

  const std::string compose_program_ = "/usr/bin/docker-compose";
  std::string compose_file_current_;
  std::string compose_file_new_;
  std::string compose_cmd_;
  bool containers_stopped;

  CommandRunner cmd;

  bool pull(const std::string &compose_file);
  bool up(const std::string &compose_file);
  bool down(const std::string &compose_file);

  bool cleanup();

 public:
  ComposeManager(const std::string &compose_file_current, const std::string &compose_file_new);

  bool update();
  bool roolback();
};

#endif  // COMPOSE_MANAGER_H_
