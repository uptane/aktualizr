#ifndef UPTANE_REPOSITORY_H_
#define UPTANE_REPOSITORY_H_

#include <cstdint>  // for int64_t
#include <string>   // for string
#include "fetcher.h"
#include "libaktualizr/types.h"  // for TimeStamp
#include "uptane/tuf.h"          // for Root, RepositoryType
#include "utilities/flow_control.h"

class INvStorage;

namespace Uptane {
class IMetadataFetcher;
class OfflineUpdateFetcher;

class RepositoryCommon {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
  RepositoryCommon(RepositoryType type_in) : type{type_in} {}
  virtual ~RepositoryCommon() = default;
  RepositoryCommon(const RepositoryCommon &guard) = default;
  RepositoryCommon(RepositoryCommon &&) = default;
  RepositoryCommon &operator=(const RepositoryCommon &guard) = default;
  RepositoryCommon &operator=(RepositoryCommon &&) = default;
  void initRoot(RepositoryType repo_type, const std::string &root_raw);
  void verifyRoot(const std::string &root_raw);
  int rootVersion() const { return root.version(); }
  bool rootExpired() const { return root.isExpired(TimeStamp::Now()); }
  virtual void updateMeta(INvStorage &storage, const IMetadataFetcher &fetcher,
                          const api::FlowControlToken *flow_control) = 0;
#ifdef BUILD_OFFLINE_UPDATES
  virtual void updateMetaOffUpd(INvStorage &storage, const OfflineUpdateFetcher &fetcher) = 0;
#endif

 protected:
  void resetRoot();
  void updateRoot(INvStorage &storage, const IMetadataFetcher &fetcher, RepositoryType repo_type);

  static const int64_t kMaxRotations = 1000;

  Root root;
  RepositoryType type;
};
}  // namespace Uptane

#endif
