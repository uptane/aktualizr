#include "swupdatemanager.h"

#include <unistd.h>
#include <cstdio>
#include <fstream>

#include <json/json.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem.hpp>
#include <utility>

#include "libaktualizr/packagemanagerfactory.h"

#include "bootloader/bootloader.h"
#include "logging/logging.h"
#include "storage/invstorage.h"
#include "utilities/utils.h"
#include "http/httpclient.h"
#include "utilities/fault_injection.h"

#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <pthread.h>
#include <fcntl.h>

extern "C" {
#include "network_ipc.h"
}

#include <sys/statvfs.h>
#include <chrono>
#include "crypto/crypto.h"
#include "crypto/keymanager.h"
#include "uptane/exceptions.h"
#include "uptane/fetcher.h"
#include "utilities/apiqueue.h"

#include <queue>
#include <atomic>

// duplicate declartion in packagemanager.cc
struct DownloadMetaStruct {
public:
    DownloadMetaStruct(Uptane::Target target_in, FetcherProgressCb progress_cb_in, const api::FlowControlToken* token_in)
        : hash_type{target_in.hashes()[0].type()},
          target{std::move(target_in)},
          token{token_in},
          progress_cb{std::move(progress_cb_in)},
          time_lastreport{std::chrono::steady_clock::now()} {}
    uintmax_t downloaded_length{0};
    unsigned int last_progress{0};
    std::ofstream fhandle;
    const Hash::Type hash_type;
    MultiPartHasher& hasher() {
        switch (hash_type) {
            case Hash::Type::kSha256:
                return sha256_hasher;
            case Hash::Type::kSha512:
                return sha512_hasher;
            default:
                throw std::runtime_error("Unknown hash algorithm");
        }
    }
    Uptane::Target target;
    const api::FlowControlToken* token;
    FetcherProgressCb progress_cb;
    // each LogProgressInterval msec log dowload progress for big files
    std::chrono::time_point<std::chrono::steady_clock> time_lastreport;

private:
    MultiPartSHA256Hasher sha256_hasher;
    MultiPartSHA512Hasher sha512_hasher;
};

std::mutex buffer_mutex;
std::condition_variable buffer_cv;
std::vector<char> data_buffer;
std::unique_ptr<DownloadMetaStruct> ds;

std::atomic<bool> data_ready(false);
std::atomic<bool> data_read(false);
std::atomic<bool> unrecoverable_error(false);

pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv_end = PTHREAD_COND_INITIALIZER;

int verbose = 1;

AUTO_REGISTER_PACKAGE_MANAGER(PACKAGE_MANAGER_SWUPDATE, SwupdateManager);

Json::Value SwupdateManager::getInstalledPackages() const {
  std::string packages_str = Utils::readFile(config.packages_file);
  std::vector<std::string> package_lines;
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  boost::split(package_lines, packages_str, boost::is_any_of("\n"));
  Json::Value packages(Json::arrayValue);
  for (auto it = package_lines.begin(); it != package_lines.end(); ++it) {
    if (it->empty()) {
      continue;
    }
    size_t pos = it->find(" ");
    if (pos == std::string::npos) {
      throw std::runtime_error("Wrong packages file format");
    }
    Json::Value package;
    package["name"] = it->substr(0, pos);
    package["version"] = it->substr(pos + 1);
    packages.append(package);
  }
  return packages;
}

// unimplemented
std::string SwupdateManager::getCurrentHash() const {
  return "dummy_hash";
}

Uptane::Target SwupdateManager::getCurrent() const {
  const std::string current_hash = getCurrentHash();
  boost::optional<Uptane::Target> current_version;
  // This may appear Primary-specific, but since Secondaries only know about
  // themselves, this actually works just fine for them, too.
  storage_->loadPrimaryInstalledVersions(&current_version, nullptr);

  if (!!current_version && current_version->sha256Hash() == current_hash) {
    return *current_version;
  }

  LOG_ERROR << "Current versions in storage and reported by Swupdate do not match";

  // Look into installation log to find a possible candidate. Again, despite the
  // name, this will work for Secondaries as well.
  std::vector<Uptane::Target> installed_versions;
  storage_->loadPrimaryInstallationLog(&installed_versions, false);

  // Version should be in installed versions. It's possible that multiple
  // targets could have the same sha256Hash. In this case the safest assumption
  // is that the most recent (the reverse of the vector) target is what we
  // should return.
  std::vector<Uptane::Target>::reverse_iterator it;
  for (it = installed_versions.rbegin(); it != installed_versions.rend(); it++) {
    if (it->sha256Hash() == current_hash) {
      return *it;
    }
  }
  // Failed to find a matching target
  Uptane::EcuMap ecus;
  std::vector<Hash> hashes{Hash(Hash::Type::kSha256, current_hash)};
  return {"unknown", ecus, hashes, 0, "SWUPDATE"};
}

// Might need to be expanded? later
data::InstallationResult SwupdateManager::install(const Uptane::Target& target) const {
  swupdate_install(target);
  return data::InstallationResult(data::ResultCode::Numeric::kNeedCompletion, "Application successful, need reboot");
}

void SwupdateManager::completeInstall() const {
  LOG_INFO << "About to reboot the system in order to apply pending updates...";
  bootloader_->reboot();
}

data::InstallationResult SwupdateManager::finalizeInstall(const Uptane::Target& target) {
  if (!bootloader_->rebootDetected()) {
    return data::InstallationResult(data::ResultCode::Numeric::kNeedCompletion,
                                    "Reboot is required for the pending update application");
  }

  LOG_INFO << "Checking installation of new Swupdate sysroot";
  const std::string current_hash = getCurrentHash();

  data::InstallationResult install_result =
      data::InstallationResult(data::ResultCode::Numeric::kOk, "Successfully booted on new version");

  if (current_hash != target.sha256Hash()) {
    LOG_ERROR << "Expected to boot " << target.sha256Hash() << " but found " << current_hash
              << ". The system may have been rolled back.";
    install_result = data::InstallationResult(data::ResultCode::Numeric::kInstallFailed, "Wrong version booted");
  }

  bootloader_->rebootFlagClear();
  return install_result;
}

void SwupdateManager::updateNotify() { bootloader_->updateNotify(); }

SwupdateManager::SwupdateManager(const PackageConfig &pconfig, const BootloaderConfig &bconfig,
                             const std::shared_ptr<INvStorage> &storage, const std::shared_ptr<HttpInterface> &http,
                             Bootloader *bootloader)
    : PackageManagerInterface(pconfig, BootloaderConfig(), storage, http),
      bootloader_(bootloader == nullptr ? new Bootloader(bconfig, *storage) : bootloader) {
  // data_ready(false);
  // data_read(false);
  // unrecoverable_error(false);
}

SwupdateManager::~SwupdateManager() { bootloader_.reset(nullptr); }

bool SwupdateManager::fetchTarget(const Uptane::Target& target, Uptane::Fetcher& fetcher, const KeyManager& keys,
                                     const FetcherProgressCb& progress_cb, const api::FlowControlToken* token) {

  if (target.IsSwupdate()) {
    LOG_ERROR << "Cannot download Swupdate target " << target.filename() << " with the fake package manager!";
    return false;
  }

  return PackageManagerInterface::fetchTarget(target, fetcher, keys, progress_cb, token);
}


// Download Handler Callback
static size_t DownloadHandler(char* contents, size_t size, size_t nmemb, void* userp) {
    if (unrecoverable_error.load()) {
        // If an error has already occurred, abort further processing
        exit(-1);
    }

    assert(userp);
    auto* dst = static_cast<DownloadMetaStruct*>(userp);
    size_t downloaded = size * nmemb;
    uint64_t expected = dst->target.length();

    if ((dst->downloaded_length + downloaded) > expected) {
        std::cerr << "Download size exceeds expected length." << std::endl;
        unrecoverable_error.store(true);
        buffer_cv.notify_all(); // Wake up any waiting threads
        exit(-1);
    }

    try {
        std::unique_lock<std::mutex> lock(buffer_mutex);

        // Wait until data_ready is false or an error occurs
        buffer_cv.wait(lock, [] { return !data_ready.load() || unrecoverable_error.load(); });

        if (unrecoverable_error.load()) {
            exit(-1);
        }

        data_buffer.clear();
        data_buffer.resize(downloaded);  // Resize buffer to match the downloaded size
        std::memcpy(data_buffer.data(), contents, downloaded);

        dst->hasher().update(reinterpret_cast<const unsigned char*>(contents), downloaded);
        dst->downloaded_length += downloaded;

        if (dst->downloaded_length == expected) {
            auto final_hash = ds->hasher().getHash().HashString();

            std::string expected_hash = dst->target.sha256Hash(); // could be wrong
            if (final_hash != expected_hash) {
                std::cerr << "Hash mismatch! Expected: " << expected_hash << ", Got: " << final_hash << std::endl;
                unrecoverable_error.store(true);
                buffer_cv.notify_all();
                exit(-1);
            }
            std::cerr << "Full update verified successfully!" << std::endl;
        }

        data_ready.store(true);
        data_read.store(false);

        // Notify readimage that data is available
        buffer_cv.notify_one();

        // Now wait until readimage has finished reading the data or an error occurs
        buffer_cv.wait(lock, [] { return data_read.load() || unrecoverable_error.load(); });

        if (unrecoverable_error.load()) {
            exit(-1);
        }

        // std::cout << "Downloaded: " << dst->downloaded_length << "/" << expected << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception in DownloadHandler: " << e.what() << std::endl;
        unrecoverable_error.store(true);
        buffer_cv.notify_all(); // Wake up any waiting threads
        exit(-1);
    }

    return downloaded;
}

// Read Image Function
int SwupdateManager::readimage(char** pbuf, int* size) {
    std::unique_lock<std::mutex> lock(buffer_mutex);

    // Wait until data is ready or an error has occurred
    buffer_cv.wait(lock, [] { return data_ready.load() || unrecoverable_error.load(); });

    if (unrecoverable_error.load()) {
        return -1; // Indicate an error to stop the update process
    }

    // Copy data to avoid data races
    std::vector<char> data_buffer_copy(data_buffer.size());
    std::memcpy(data_buffer_copy.data(), data_buffer.data(), data_buffer.size());

    *pbuf = data_buffer_copy.data();
    *size = static_cast<int>(data_buffer.size());

    // After the data has been read, mark it as read and notify DownloadHandler
    data_ready.store(false);
    data_read.store(true);
    buffer_cv.notify_one();

    return *size;
}

// Status Print Function
int SwupdateManager::printstatus(ipc_message *msg) {
    if (verbose) {
        std::printf("Status: %d message: %s\n",
                  msg->data.notify.status,
                  msg->data.notify.msg);
    }
    return 0;
}

// End Function
int SwupdateManager::endupdate(RECOVERY_STATUS status) {
    int end_status = (status == SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
    std::printf("SWUpdate %s\n", (status == FAILURE) ? "*failed* !" : "was successful !");

    if (status == SUCCESS) {
        std::printf("Executing post-update actions.\n");
    } else {
        std::printf("Update failed. Performing cleanup.\n");
    }

    pthread_mutex_lock(&mymutex);
    pthread_cond_signal(&cv_end);
    pthread_mutex_unlock(&mymutex);

    return end_status;
}

int SwupdateManager::swupdate_install(const Uptane::Target &target) const {
    struct swupdate_request req;
    int rc;
    std::shared_ptr<HttpInterface> http;

    http = std::make_shared<HttpClient>();
    // std::dynamic_pointer_cast<HttpClient>(http)->timeout(10000000);
    // Uptane::Target target("test", jsonDataOut);
    ds = std_::make_unique<DownloadMetaStruct>(target, nullptr, nullptr);

    swupdate_prepare_req(&req);

    rc = swupdate_async_start(readimage, printstatus, endupdate, &req, sizeof(req));
    if (rc < 0) {
        std::cerr << "swupdate start error" << std::endl;
        return EXIT_FAILURE;
    }

    // std::string url = jsonDataOut["url"].asString();
    std::string url = target.uri();

    // Start the download in a separate thread to avoid blocking
    std::thread download_thread([&]() {
        HttpResponse response = http->download(
            url,
            DownloadHandler,
            nullptr,  // ProgressHandler can be added if needed
            ds.get(), // userp
            static_cast<curl_off_t>(ds->downloaded_length)  // from
        );

        // Check the response for errors
        if (response.http_status_code != 200) {
            std::cerr << "HTTP download failed with status: " << response.http_status_code << std::endl;
            unrecoverable_error.store(true);
            buffer_cv.notify_all(); // Wake up any waiting threads
        }
    });

    // Initialize the mutex before waiting
    pthread_mutex_init(&mymutex, NULL);

    pthread_mutex_lock(&mymutex);
    pthread_cond_wait(&cv_end, &mymutex);
    pthread_mutex_unlock(&mymutex);

    // Join the download thread to ensure clean exit
    if (download_thread.joinable()) {
        download_thread.join();
    }

    if (unrecoverable_error.load()) {
        std::cerr << "An unrecoverable error occurred. Update process stopped." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}