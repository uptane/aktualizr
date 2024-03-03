#include "fetcher.h"

#include "uptane/exceptions.h"

namespace Uptane {

void Fetcher::fetchRole(std::string* result, int64_t maxsize, RepositoryType repo, const Uptane::Role& role,
                        Version version, const api::FlowControlToken* flow_control) const {
  std::string url = (repo == RepositoryType::Director()) ? director_server : repo_server;
  if (role.IsDelegation()) {
    url += "/delegations";
  }
  url += "/" + version.RoleFileName(role);
  HttpResponse response = http->get(url, maxsize, flow_control);
  if (flow_control != nullptr && flow_control->hasAborted()) {
    throw Uptane::LocallyAborted(repo);
  }
  if (!response.isOk()) {
    throw Uptane::MetadataFetchFailure(repo.ToString(), role.ToString());
  }
  *result = response.body;
}

}  // namespace Uptane
