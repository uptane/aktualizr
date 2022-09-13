#ifndef UPTANE_SECONDARYINTERFACE_H
#define UPTANE_SECONDARYINTERFACE_H

#include <string>

#include "libaktualizr/secondary_provider.h"
#include "libaktualizr/types.h"

namespace Uptane {
class OfflineUpdateFetcher;
}  // namespace Uptane

class InstallInfo {
 public:
  explicit InstallInfo(UpdateType update_type = UpdateType::kOnline) : update_type_(update_type) {}
  virtual ~InstallInfo() = default;
  InstallInfo(const InstallInfo&) = default;
  InstallInfo(InstallInfo&&) = default;
  InstallInfo& operator=(const InstallInfo&) = default;
  InstallInfo& operator=(InstallInfo&&) = default;

  void initOffline(const boost::filesystem::path& images_path_offline,
                   const boost::filesystem::path& metadata_path_offline) {
    assert(update_type_ == UpdateType::kOffline);
    images_path_offline_ = images_path_offline;
    metadata_path_offline_ = metadata_path_offline;
  }

  UpdateType getUpdateType() const { return update_type_; }
  const boost::filesystem::path& getImagesPathOffline() const { return images_path_offline_; }
  const boost::filesystem::path& getMetadataPathOffline() const { return metadata_path_offline_; }

 protected:
  UpdateType update_type_;
  boost::filesystem::path images_path_offline_;
  boost::filesystem::path metadata_path_offline_;
};

class SecondaryInterface {
 public:
  SecondaryInterface() = default;
  virtual ~SecondaryInterface() = default;

  using Ptr = std::shared_ptr<SecondaryInterface>;

  virtual void init(std::shared_ptr<SecondaryProvider> secondary_provider_in) = 0;
  virtual std::string Type() const = 0;
  virtual Uptane::EcuSerial getSerial() const = 0;
  virtual Uptane::HardwareIdentifier getHwId() const = 0;
  virtual PublicKey getPublicKey() const = 0;

  virtual Uptane::Manifest getManifest() const = 0;
  virtual data::InstallationResult putMetadata(const Uptane::Target& target) = 0;
  virtual bool ping() const = 0;

  // return 0 during initialization and -1 for error.
  virtual int32_t getRootVersion(bool director) const = 0;
  virtual data::InstallationResult putRoot(const std::string& root, bool director) = 0;

  virtual data::InstallationResult sendFirmware(const Uptane::Target& target) = 0;
  virtual data::InstallationResult install(const Uptane::Target& target, const InstallInfo& info) = 0;
  virtual data::InstallationResult install(const Uptane::Target& target) { return install(target, InstallInfo()); }
  virtual boost::optional<data::InstallationResult> completePendingInstall(const Uptane::Target& target) {
    (void)target;
    return boost::none;
  }
  virtual void rollbackPendingInstall() {}

#ifdef BUILD_OFFLINE_UPDATES
  virtual data::InstallationResult putMetadataOffUpd(const Uptane::Target& target,
                                                     const Uptane::OfflineUpdateFetcher& fetcher) = 0;
#endif

 protected:
  SecondaryInterface(const SecondaryInterface&) = default;
  SecondaryInterface(SecondaryInterface&&) = default;
  SecondaryInterface& operator=(const SecondaryInterface&) = default;
  SecondaryInterface& operator=(SecondaryInterface&&) = default;
};

#endif  // UPTANE_SECONDARYINTERFACE_H
