#include <boost/filesystem/path.hpp>

#include "compose_manager.h"
#include "libaktualizr/config.h"
#include "logging/logging.h"

ComposeManager::ComposeManager(const std::string &compose_file_current, const std::string &compose_file_new) {
  compose_file_current_ = compose_file_current;
  compose_file_new_ = compose_file_new;
  compose_cmd_ = compose_program_ + " --file ";
  containers_stopped = false;
  reboot = false;
  sync_update = false;
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

bool ComposeManager::completeUpdate() {
  if (access(compose_file_current_.c_str(), F_OK) == 0) {
    if (!down(compose_file_current_)) {
      LOG_ERROR << "Error running docker-compose down";
      return false;
    }
    containers_stopped = true;
  }

  if (!up(compose_file_new_)) {
    LOG_ERROR << "Error running docker-compose up";
    return false;
  }

  rename(compose_file_new_.c_str(), compose_file_current_.c_str());

  cleanup();

  return true;
}

bool ComposeManager::checkRollback() {
  LOG_INFO << "Checking rollback status";
  std::vector<std::string> output = cmd.runResult(printenv_program_);
  auto found_it = std::find_if(output.begin(), output.end(),
                               [](const std::string &str) { return str.find("rollback=1") != std::string::npos; });
  return found_it != output.end();
}

bool ComposeManager::update(bool offline, bool sync) {
  LOG_INFO << "Updating containers via docker-compose";

  sync_update = sync;
  reboot = false;

  if (sync_update) {
    LOG_INFO << "OSTree update pending. This is a synchronous update transaction.";
  }

  containers_stopped = false;

  if (!offline) {
    // Only try to pull images upon an online update.
    if (!pull(compose_file_new_)) {
      LOG_ERROR << "Error running docker-compose pull";
      return false;
    }
  }

  if (!sync_update) {
    if (!completeUpdate()) {
      return false;
    }
  }

  return true;
}

bool ComposeManager::pendingUpdate() {
  if (access(compose_file_new_.c_str(), F_OK) == 0) {
    LOG_INFO << "Finishing pending container updates via docker-compose";
  } else {
    // Should never reach here in normal operation.
    return false;
  }

  if (checkRollback()) {
    return false;
  }

  return completeUpdate();
}

bool ComposeManager::rollback() {
  LOG_INFO << "Rolling back container update";

  if (containers_stopped) {
    up(compose_file_current_);
    cleanup();
    containers_stopped = false;
  }

  remove(compose_file_new_.c_str());

  if (sync_update) {
    cmd.run("fw_setenv rollback 1");
  }

  if (reboot) {
    cmd.run("reboot");
  }

  return true;
}
