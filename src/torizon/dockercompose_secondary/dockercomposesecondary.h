#ifndef PRIMARY_DOCKERCOMPOSESECONDARY_H_
#define PRIMARY_DOCKERCOMPOSESECONDARY_H_

#include <string>
#include "managedsecondary.h"
#include "libaktualizr/types.h"

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
  data::InstallationResult install(const Uptane::Target &target) override;
  void validateInstall();
};

}  // namespace Primary

#endif  // PRIMARY_DOCKERCOMPOSESECONDARY_H_
