#ifndef UPTANE_FETCHER_H_
#define UPTANE_FETCHER_H_

#include <boost/filesystem/path.hpp>

#include "http/httpinterface.h"
#include "libaktualizr/config.h"
#include "tuf.h"
#include "utilities/flow_control.h"

namespace Uptane {

constexpr int64_t kMaxRootSize = 64 * 1024;
constexpr int64_t kMaxDirectorTargetsSize = 64 * 1024;
constexpr int64_t kMaxTimestampSize = 64 * 1024;
constexpr int64_t kMaxSnapshotSize = 64 * 1024;
constexpr int64_t kMaxImageTargetsSize = 8 * 1024 * 1024;

class IMetadataFetcher {
 public:
  IMetadataFetcher(const IMetadataFetcher&) = delete;
  IMetadataFetcher& operator=(const IMetadataFetcher&) = delete;
  IMetadataFetcher& operator=(IMetadataFetcher&&) = delete;
  virtual ~IMetadataFetcher() = default;

  /**
   * Fetch a role at a version (which might be 'latest').
   *
   * If the fetch fails, throw something derived Uptane::Exception
   * @param result
   * @param maxsize
   * @param repo
   * @param role
   * @param version
   * @param flow_control
   * @throws Uptane::MetadataFetchFailure If fetching metadata fails (e.g. network error)
   * @throws Uptane::LocallyAborted If the caller aborts with flow_control->hasAborted()
   */
  virtual void fetchRole(std::string* result, int64_t maxsize, RepositoryType repo, const Uptane::Role& role,
                         Version version, const api::FlowControlToken* flow_control) const = 0;

  void fetchRole(std::string* result, int64_t maxsize, RepositoryType repo, const Uptane::Role& role,
                 Version version) const {
    return fetchRole(result, maxsize, repo, role, version, nullptr);
  }

  void fetchLatestRole(std::string* result, int64_t maxsize, RepositoryType repo, const Uptane::Role& role,
                       const api::FlowControlToken* flow_control = nullptr) const {
    fetchRole(result, maxsize, repo, role, Version(), flow_control);
  }

 protected:
  IMetadataFetcher() = default;
  IMetadataFetcher(IMetadataFetcher&&) = default;
};

class Fetcher : public IMetadataFetcher {
 public:
  Fetcher(const Config& config_in, std::shared_ptr<HttpInterface> http_in)
      : Fetcher(config_in.uptane.repo_server, config_in.uptane.director_server, std::move(http_in)) {}
  Fetcher(std::string repo_server_in, std::string director_server_in, std::shared_ptr<HttpInterface> http_in)
      : http(std::move(http_in)),
        repo_server(std::move(repo_server_in)),
        director_server(std::move(director_server_in)) {}
  void fetchRole(std::string* result, int64_t maxsize, RepositoryType repo, const Uptane::Role& role, Version version,
                 const api::FlowControlToken* flow_control) const override;

  std::string getRepoServer() const { return repo_server; }

 private:
  std::shared_ptr<HttpInterface> http;
  std::string repo_server;
  std::string director_server;
};

class OfflineUpdateFetcher : public IMetadataFetcher {
 public:
  explicit OfflineUpdateFetcher(boost::filesystem::path source_path) : source_path_(std::move(source_path)) {
    if (source_path_.empty()) {
      throw std::runtime_error("Source path for offline-updates is not defined");
    }
  }
  void fetchRole(std::string* result, int64_t maxsize, RepositoryType repo, const Uptane::Role& role, Version version,
                 const api::FlowControlToken* flow_control) const override;

  boost::filesystem::path getBasePath() const { return source_path_; }
  boost::filesystem::path getImagesPath() const { return source_path_ / "images"; }
  boost::filesystem::path getMetadataPath() const { return source_path_ / "metadata"; }

 private:
  boost::filesystem::path source_path_;
};

}  // namespace Uptane

#endif
