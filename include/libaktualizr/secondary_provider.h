#ifndef UPTANE_SECONDARY_PROVIDER_H
#define UPTANE_SECONDARY_PROVIDER_H

#include <string>

#include "libaktualizr/config.h"
#include "libaktualizr/packagemanagerinterface.h"
#include "libaktualizr/types.h"
#include "storage/invstorage.h"

class SecondaryProviderBuilder;

class SecondaryProvider {
 public:
  friend class SecondaryProviderBuilder;

  bool getMetadata(Uptane::MetaBundle* meta_bundle, const Uptane::Target& target) const;
  bool getDirectorMetadata(Uptane::MetaBundle* meta_bundle) const;
  bool getImageRepoMetadata(Uptane::MetaBundle* meta_bundle, const Uptane::Target& target) const;
  bool getEcuSerialsForHwId(EcuSerials* serials) const;
  bool pendingPrimaryUpdate();
  std::string getTreehubCredentials() const;
  std::ifstream getTargetFileHandle(const Uptane::Target& target) const;

 private:
  SecondaryProvider(Config& config_in, std::shared_ptr<const INvStorage> storage_in,
                    std::shared_ptr<const PackageManagerInterface> package_manager_in)
      : config_(config_in), storage_(std::move(storage_in)), package_manager_(std::move(package_manager_in)) {}

  Config& config_;
  const std::shared_ptr<const INvStorage> storage_;
  const std::shared_ptr<const PackageManagerInterface> package_manager_;
};

#endif  // UPTANE_SECONDARY_PROVIDER_H
