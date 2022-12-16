#include "fetcher.h"

#include "uptane/exceptions.h"

#include <boost/filesystem.hpp>
#include <fstream>

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

void OfflineUpdateFetcher::fetchRole(std::string* result, int64_t maxsize, RepositoryType repo,
                                     const Uptane::Role& role, Version version,
                                     const api::FlowControlToken* flow_control) const {
  (void)flow_control;  // Safe, we are only looking at the local file system
  boost::filesystem::path path;
  if (repo == RepositoryType::Director()) {
    path = getMetadataPath() / "director" / version.RoleFileName(role);
  } else {
    path = getMetadataPath() / "image-repo" / version.RoleFileName(role);
  }

  boost::system::error_code ec;
  if (!boost::filesystem::exists(path, ec)) {
    throw Uptane::MetadataFetchFailure(repo.ToString(), path.string());
  }

  std::ifstream file_input(path.c_str());
  file_input.seekg(0, std::ifstream::end);
  int64_t file_size = file_input.tellg();
  // [OFFUPD] We may need improvements here especially for unreliable media like NFS:
  // - Maybe throw a better error;
  // - Handle the case where tellg() returns -1;
  // - Handle the case where an error happens during read() (if (!file_input)).
  if (file_size > maxsize) {
    throw Uptane::MetadataFetchFailure(repo.ToString(), path.string());
  }

  file_input.seekg(0, std::ifstream::beg);
  std::vector<char> buffer(static_cast<size_t>(file_size));

  file_input.read(buffer.data(), file_size);
  *result = std::string(buffer.begin(), buffer.end());
}
}  // namespace Uptane
