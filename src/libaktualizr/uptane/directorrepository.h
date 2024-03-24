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

 private:
  FRIEND_TEST(Director, EmptyTargets);

  void resetMeta();
  void checkTargetsExpired();
  void targetsSanityCheck();
  bool usePreviousTargets() const;

  // Since the Director can send us an empty targets list to mean "no new
  // updates", we have to persist the previous targets list. Use the latest for
  // checking expiration but the most recent non-empty list for everything else.
  Uptane::Targets targets;         // Only empty if we've never received non-empty targets.
  Uptane::Targets latest_targets;  // Can be an empty list.
  /**
   * The correlation id of the currently running update.
   * This is set when the targets are first downloaded from the server, and
   * kept until we've sent a manifest containing a terminating state.
   */
  Uptane::CorrelationId correlation_id_;
};

}  // namespace Uptane

#endif  // DIRECTOR_REPOSITORY_H
