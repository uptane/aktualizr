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
  int rootVersion() const { return root.version(); }
  bool rootExpired() const { return root.isExpired(TimeStamp::Now()); }

  /**
   * Load the initial state of the repository from storage.
   * Note that this _required_ for correct initialization. The API here is
   * particularly error prone and should be improved:
   *   - Until this is called the root will have a policy kRejectAll (i.e. no
   *     updates are possible). It is therefore part of the initialization
   *     process and so should be moved to the ctor.
   *   - It throws an exception in a non-exceptional case: where the metadata
   *     in the storage has expired.
   *   - When it throws an exception, it changes the state and actually does
   *     perform initialization, therefore violating the Strong Exception
   *     Guarantee.
   * @throws UptaneException if the local metadata is stale (this is not a failure)
   */
  virtual void checkMetaOffline(INvStorage &storage) = 0;
  virtual void updateMeta(INvStorage &storage, const IMetadataFetcher &fetcher,
                          const api::FlowControlToken *flow_control) = 0;

 protected:
  void resetRoot();
  void updateRoot(INvStorage &storage, const IMetadataFetcher &fetcher, RepositoryType repo_type);

  Root root{Root::Policy::kRejectAll};
  RepositoryType type;
};
}  // namespace Uptane

#endif
