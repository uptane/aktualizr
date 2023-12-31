#ifndef DIRECTOR_REPOSITORY_H_
#define DIRECTOR_REPOSITORY_H_

#include "gtest/gtest_prod.h"

#include "uptanerepository.h"

namespace Uptane {

/* Director repository encapsulates state of metadata verification process. Subsequent verification steps rely on
 * previous ones.
 */
class DirectorRepository : public RepositoryCommon {
 public:
  DirectorRepository() : RepositoryCommon(RepositoryType::Director()) {}

  void verifyTargets(const std::string& targets_raw);
  const Targets& getTargets() const { return targets; }
  std::vector<Uptane::Target> getTargets(const Uptane::EcuSerial& ecu_id,
                                         const Uptane::HardwareIdentifier& hw_id) const {
    return targets.getTargets(ecu_id, hw_id);
  }
  Uptane::CorrelationId getCorrelationId() const { return correlation_id_; }
  void checkMetaOffline(INvStorage& storage);
  void dropTargets(INvStorage& storage);

  void updateMeta(INvStorage& storage, const IMetadataFetcher& fetcher,
                  const api::FlowControlToken* flow_control) override;
  bool matchTargetsWithImageTargets(const std::shared_ptr<const Uptane::Targets>& image_targets) const;

#ifdef BUILD_OFFLINE_UPDATES
  void checkMetaOfflineOffUpd(INvStorage& storage);
  void updateMetaOffUpd(INvStorage& storage, const OfflineUpdateFetcher& fetcher) override;
  void verifyOfflineSnapshot(const std::string& snapshot_raw_new, const std::string& snapshot_raw_old);
  void verifyOfflineSnapshot(const std::string& snapshot_raw_new) {
    std::string empty;
    verifyOfflineSnapshot(snapshot_raw_new, empty);
  }
  void verifyOfflineTargets(const std::string& targets_raw, INvStorage& storage);
#endif

 private:
  FRIEND_TEST(Director, EmptyTargets);

  void resetMeta();
  void checkTargetsExpired(UpdateType utype);
  void targetsSanityCheck(UpdateType utype);

  Uptane::Targets targets;
  /**
   * The correlation id of the currently running update.
   * This is set when the targets are first downloaded from the server, and
   * kept until we've sent a manifest containing a terminating state.
   */
  Uptane::CorrelationId correlation_id_;

#ifdef BUILD_OFFLINE_UPDATES
  void checkOfflineSnapshotExpired();
  void transformOfflineTargets(INvStorage& storage);
  Uptane::Snapshot offline_snapshot_;
#endif
};

}  // namespace Uptane

#endif  // DIRECTOR_REPOSITORY_H
