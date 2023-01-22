#include <boost/filesystem.hpp>

#include "compose_manager.h"
#include "libaktualizr/config.h"
#include "logging/logging.h"

static const char *const compose_cmd_prefix = "/usr/bin/docker-compose --file ";
static const char *const docker_cmd_prefix = "/usr/bin/docker ";
static const char *const check_rollback_cmd = "/usr/bin/fw_printenv rollback";

// In the future we may want to override the commands for testing.
ComposeManager::ComposeManager()
    : compose_cmd_{compose_cmd_prefix}, docker_cmd_{docker_cmd_prefix}, check_rollback_cmd_{check_rollback_cmd} {}

bool ComposeManager::pull(const boost::filesystem::path &compose_file, const api::FlowControlToken *flow_control) {
  LOG_INFO << "Running docker-compose pull";
  return CommandRunner::run(compose_cmd_ + compose_file.string() + " pull --no-parallel", flow_control);
}

bool ComposeManager::up(const boost::filesystem::path &compose_file) {
  LOG_INFO << "Running docker-compose up";
  return CommandRunner::run(compose_cmd_ + compose_file.string() + " -p torizon up --detach --remove-orphans");
}

bool ComposeManager::down(const boost::filesystem::path &compose_file) {
  LOG_INFO << "Running docker-compose down";
  return CommandRunner::run(compose_cmd_ + compose_file.string() + " -p torizon down");
}

bool ComposeManager::cleanup() {
  LOG_INFO << "Removing not used containers, networks and images";
  return CommandRunner::run(docker_cmd_ + "system prune -a --force");
}

bool ComposeManager::checkRollback() {
  LOG_INFO << "Checking rollback status";
  std::vector<std::string> output = CommandRunner::runResult(check_rollback_cmd_);
  auto found_it = std::find_if(output.begin(), output.end(),
                               [](const std::string &str) { return str.find("rollback=1") != std::string::npos; });
  return found_it != output.end();
}
