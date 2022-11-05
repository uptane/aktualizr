#ifndef IMAGE_REPOSITORY_H_
#define IMAGE_REPOSITORY_H_

#include <memory>
#include <string>

#include "uptanerepository.h"

namespace Uptane {

constexpr int kDelegationsMaxDepth = 5;

class ImageRepository : public RepositoryCommon {
 public:
  ImageRepository() : RepositoryCommon(RepositoryType::Image()) {}

  void resetMeta();

  void verifyTargets(const std::string& targets_raw, bool prefetch);

  void verifyTimestamp(const std::string& timestamp_raw);

  void verifySnapshot(const std::string& snapshot_raw, bool prefetch);

  static std::shared_ptr<Uptane::Targets> verifyDelegation(const std::string& delegation_raw, const Uptane::Role& role,
                                                           const Targets& parent_target);
  std::shared_ptr<const Uptane::Targets> getTargets() const { return targets; }

  void verifyRoleHashes(const std::string& role_data, const Uptane::Role& role, bool prefetch) const;
  int getRoleVersion(const Uptane::Role& role) const;
  int64_t getRoleSize(const Uptane::Role& role) const;

  void checkMetaOffline(INvStorage& storage);
  void updateMeta(INvStorage& storage, const IMetadataFetcher& fetcher,
                  const api::FlowControlToken* flow_control) override;

#ifdef BUILD_OFFLINE_UPDATES
  void checkMetaOfflineOffUpd(INvStorage& storage);
  void updateMetaOffUpd(INvStorage& storage, const OfflineUpdateFetcher& fetcher) override;
  void verifySnapshotOffline(const std::string& snapshot_raw);
#endif

 private:
  void checkTimestampExpired();
  void checkSnapshotExpired();
  int64_t snapshotSize() const { return timestamp.snapshot_size(); }
  void fetchSnapshot(INvStorage& storage, const IMetadataFetcher& fetcher, int local_version,
                     const api::FlowControlToken* flow_control);
  void fetchTargets(INvStorage& storage, const IMetadataFetcher& fetcher, int local_version,
                    const api::FlowControlToken* flow_control);
  void checkTargetsExpired();

  std::shared_ptr<Uptane::Targets> targets;
  Uptane::TimestampMeta timestamp;
  Uptane::Snapshot snapshot;
};

}  // namespace Uptane

#endif  // IMAGE_REPOSITORY_H_
