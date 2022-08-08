#ifndef PRIMARY_DOCKERCOMPOSESECONDARY_H_
#define PRIMARY_DOCKERCOMPOSESECONDARY_H_

#include <boost/filesystem.hpp>
#include <string>

#include "libaktualizr/types.h"
#include "managedsecondary.h"

namespace Primary {

class DockerComposeSecondaryConfig : public ManagedSecondaryConfig {
 public:
  DockerComposeSecondaryConfig() : ManagedSecondaryConfig(Type) {}
  DockerComposeSecondaryConfig(const Json::Value& json_config);

  static std::vector<DockerComposeSecondaryConfig> create_from_file(const boost::filesystem::path& file_full_path);
  void dump(const boost::filesystem::path& file_full_path) const;

 public:
  static const char* const Type;
};

/**
 * An primary secondary that runs on the same device but treats
 * the firmware that it is pushed as a docker-compose yaml file
 */
class DockerComposeSecondary : public ManagedSecondary {
 public:
  explicit DockerComposeSecondary(Primary::DockerComposeSecondaryConfig sconfig_in);
  ~DockerComposeSecondary() override = default;

  std::string Type() const override { return DockerComposeSecondaryConfig::Type; }

  bool ping() const override { return true; }

 private:
  bool getFirmwareInfo(Uptane::InstalledImageInfo& firmware_info) const override;
  data::InstallationResult install(const Uptane::Target& target, const InstallInfo& info) override;
  void validateInstall();

  /**
   * Load Docker images from an offline-update image.
   */
  bool loadDockerImages(const boost::filesystem::path& compose_in, const std::string& compose_sha256,
                        const boost::filesystem::path& images_path, const boost::filesystem::path& manifests_path,
                        boost::filesystem::path* compose_out = nullptr);
  bool pendingPrimaryUpdate();
};

}  // namespace Primary

#endif  // PRIMARY_DOCKERCOMPOSESECONDARY_H_
