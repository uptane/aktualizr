#include "compose_manager.h"
#include "logging/logging.h"

ComposeManager::ComposeManager(const std::string &compose_file_current, const std::string &compose_file_new) {
  compose_file_current_ = compose_file_current;
  compose_file_new_ = compose_file_new;
  compose_cmd_  = compose_program_ + " --file ";
}

bool ComposeManager::pull(const std::string &compose_file) {
  LOG_INFO << "Running docker-compose pull";
  return cmd.run(compose_cmd_ + compose_file + " pull --no-parallel");
}

bool ComposeManager::up(const std::string &compose_file) {
  LOG_INFO << "Running docker-compose up";
  return cmd.run(compose_cmd_ + compose_file + " -p torizon up --detach --remove-orphans");
}

bool ComposeManager::down(const std::string &compose_file) {
  LOG_INFO << "Running docker-compose down";
  return cmd.run(compose_cmd_ + compose_file + " -p torizon down");
}

bool ComposeManager::cleanup() {
  LOG_INFO << "Removing not used containers, networks and images";
  return cmd.run("docker system prune -a --force");
}

bool ComposeManager::update() {

  LOG_INFO << "Updating containers via docker-compose";

  containers_stopped = false;

  if (pull(compose_file_new_) == false) {
    LOG_ERROR << "Error running docker-compose pull";
    return false;
  }

  if (!access(compose_file_current_.c_str(), F_OK)) {
    if (down(compose_file_current_) == false) {
      LOG_ERROR << "Error running docker-compose down";
      return false;
    }
    containers_stopped = true;
  }

  if (up(compose_file_new_) == false) {
    LOG_ERROR << "Error running docker-compose up";
    return false;
 }

  rename(compose_file_new_.c_str(), compose_file_current_.c_str());

  cleanup();

  return true;
}

bool ComposeManager::roolback() {

  LOG_INFO << "Rolling back container update";

  if (containers_stopped == true) {
    up(compose_file_current_);
    containers_stopped = false;
  }

  remove(compose_file_new_.c_str());

  cleanup();

  return true;
}
