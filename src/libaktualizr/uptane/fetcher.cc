#include "fetcher.h"

#include "uptane/exceptions.h"

#include <fstream>

namespace Uptane {

void Fetcher::fetchRole(std::string* result, int64_t maxsize, RepositoryType repo, const Uptane::Role& role,
                        Version version) const {
  std::string url = (repo == RepositoryType::Director()) ? director_server : repo_server;
  if (role.IsDelegation()) {
    url += "/delegations";
  }
  url += "/" + version.RoleFileName(role);
  HttpResponse response = http->get(url, maxsize);
  if (!response.isOk()) {
    throw Uptane::MetadataFetchFailure(repo.ToString(), role.ToString());
  }
  *result = response.body;
}

void OfflineUpdateFetcher::fetchRole(std::string* result, int64_t maxsize, RepositoryType repo,
                                     const Uptane::Role& role, Version version) const {
  boost::filesystem::path path;
  if (repo == RepositoryType::Director()) {
    path = getMetadataPath() / "director" / version.RoleFileName(role);
  } else {
    path = getMetadataPath() / "image-repo" / version.RoleFileName(role);
  }

  if (!boost::filesystem::exists(path)) {
    throw Uptane::MetadataFetchFailure(repo.ToString(), path.string());
  }

  std::ifstream file_input(path.c_str());
  file_input.seekg(0, std::ifstream::end);
  int64_t file_size = file_input.tellg();
  // [OFFUPD] Maybe throw a better error here?
  if (file_size > maxsize) {
    throw Uptane::MetadataFetchFailure(repo.ToString(), path.string());
  }

  file_input.seekg(0, std::ifstream::beg);
  std::vector<char> buffer(static_cast<size_t>(file_size));

  file_input.read(buffer.data(), file_size);
  *result = std::string(buffer.begin(), buffer.end());
}
}  // namespace Uptane
