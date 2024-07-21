#ifndef UPTANE_REPOSITORY_H_
#define UPTANE_REPOSITORY_H_

#include <cstdint>               // for int64_t
#include <string>                // for string
#include "libaktualizr/types.h"  // for TimeStamp
#include "uptane/tuf.h"          // for Root, RepositoryType
#include "utilities/flow_control.h"

class INvStorage;

namespace Uptane {
class IMetadataFetcher;
class OfflineUpdateFetcher;

class RepositoryCommon {
 public:
  explicit RepositoryCommon(RepositoryType type_in) : type{type_in} {}
  virtual ~RepositoryCommon() = default;
  RepositoryCommon(const RepositoryCommon &guard) = default;
  RepositoryCommon(RepositoryCommon &&) = default;
  RepositoryCommon &operator=(const RepositoryCommon &guard) = default;
  RepositoryCommon &operator=(RepositoryCommon &&) = default;
  void initRoot(RepositoryType repo_type, const std::string &root_raw);
  void verifyRoot(const std::string &root_raw);
  [[nodiscard]] int rootVersion() const { return root.version(); }
  [[nodiscard]] bool rootExpired() const { return root.isExpired(Now()); }

  /**
   * Load the initial state of the repository from storage.
   * Note that this _required_ for correct initialization.
   * @throws UptaneException if the local metadata is stale (this is not a failure)
   */
  virtual void checkMetaOffline(INvStorage &storage) = 0;
  virtual void updateMeta(INvStorage &storage, const IMetadataFetcher &fetcher,
                          const api::FlowControlToken *flow_control) = 0;
#ifdef BUILD_OFFLINE_UPDATES
  virtual void updateMetaOffUpd(INvStorage &storage, const OfflineUpdateFetcher &fetcher) = 0;
#endif

  void ForceNowForTesting(TimeStamp &&fake_now);

 protected:
  void resetRoot();
  void updateRoot(INvStorage &storage, const IMetadataFetcher &fetcher, RepositoryType repo_type);
  [[nodiscard]] TimeStamp Now() const;

  static const int64_t kMaxRotations = 1000;

  Root root{Root::Policy::kRejectAll};
  RepositoryType type;

 private:
  TimeStamp overriden_now_{};
};
}  // namespace Uptane

#endif
