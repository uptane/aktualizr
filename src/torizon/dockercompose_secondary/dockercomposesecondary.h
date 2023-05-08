#ifndef PRIMARY_DOCKERCOMPOSESECONDARY_H_
#define PRIMARY_DOCKERCOMPOSESECONDARY_H_

#include <boost/filesystem.hpp>
#include <string>

#include "compose_manager.h"
#include "libaktualizr/types.h"
#include "managedsecondary.h"

namespace Primary {

class DockerComposeSecondaryConfig : public ManagedSecondaryConfig {
 public:
  static constexpr const char* const Type{"docker-compose"};

  DockerComposeSecondaryConfig() : ManagedSecondaryConfig(Type) {}
  explicit DockerComposeSecondaryConfig(const Json::Value& json_config);

  static std::vector<DockerComposeSecondaryConfig> create_from_file(const boost::filesystem::path& file_full_path);
  void dump(const boost::filesystem::path& file_full_path) const;
};

/**
 * An primary secondary that runs on the same device but treats
 * the firmware that it is pushed as a docker-compose yaml file
 */
class DockerComposeSecondary : public ManagedSecondary {
 public:
  explicit DockerComposeSecondary(Primary::DockerComposeSecondaryConfig sconfig_in);
  DockerComposeSecondary(const DockerComposeSecondary&) = delete;
  DockerComposeSecondary(DockerComposeSecondary&&) = delete;
  DockerComposeSecondary& operator=(const DockerComposeSecondary&) = delete;
  DockerComposeSecondary& operator=(DockerComposeSecondary&&) = delete;

  ~DockerComposeSecondary() override = default;

  // SecondaryInterface implementation
  std::string Type() const override { return DockerComposeSecondaryConfig::Type; }
  bool ping() const override { return true; }
  data::InstallationResult sendFirmware(const Uptane::Target& target, const InstallInfo& install_info,
                                        const api::FlowControlToken* flow_control) override;
  data::InstallationResult install(const Uptane::Target& target, const InstallInfo& info,
                                   const api::FlowControlToken* flow_control) override;
  boost::optional<data::InstallationResult> completePendingInstall(const Uptane::Target& target) override;
  void rollbackPendingInstall() override;
  void cleanStartup() override;

 protected:
  bool getFirmwareInfo(Uptane::InstalledImageInfo& firmware_info) const override;

 private:
  /**
   * Load Docker images from an offline-update image.
   */
  static bool loadDockerImages(const boost::filesystem::path& compose_in, const std::string& compose_sha256,
                               const boost::filesystem::path& images_path,
                               const boost::filesystem::path& manifests_path,
                               boost::filesystem::path* compose_out = nullptr);

  /**
   * The name of the docker-compose file that we are managing
   */
  boost::filesystem::path composeFile() const { return sconfig.firmware_path; }

  /**
   * If this file exists, then docker-compose.service in meta-toradex-torizon won't call docker-compose up at all during
   * boot.
   *
   * It is created to signify that Aktualizr is responsible for running docker-compose up, because it is part of a
   * synchronous update, and Aktualizr needs to check whether we are booted into the new image or got rolled back.
   */
  boost::filesystem::path composeFileNew() const {
    auto res = composeFile();
    res += ".tmp";
    return res;
  }

  ComposeManager compose_manager_{};
};

}  // namespace Primary

#endif  // PRIMARY_DOCKERCOMPOSESECONDARY_H_
