#include "swupdatemanager.h"

#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "logging/logging.h"
#include "storage/invstorage.h"
#include "http/httpclient.h"
#include "utilities/fault_injection.h"
#include "libaktualizr/packagemanagerfactory.h"

// figure out later which to include
#include <pthread.h>
#include "json/json.h"
#include <unistd.h>
#include <fcntl.h>
#include <gio/gio.h>

extern "C" {
#include "network_ipc.h"
}

#include <sys/statvfs.h>
#include <boost/filesystem.hpp>
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

// ??? implement later
Uptane::Target SwupdateManager::getCurrent() const {
  boost::optional<Uptane::Target> current_version;
  storage_->loadPrimaryInstalledVersions(&current_version, nullptr);

  if (!!current_version) {
    return *current_version;
  }

  return Uptane::Target::Unknown();
}

// Might need to be expanded? later
data::InstallationResult SwupdateManager::install(const Uptane::Target& target) const {
  swupdate_install();
  return data::InstallationResult(data::ResultCode::Numeric::kNeedCompletion, "Application successful, need reboot");
}

data::InstallationResult SwupdateManager::finalizeInstall(const Uptane::Target& target) {
  data::InstallationResult install_res;
  if (this->unrecoverable_error) {
    install_res = data::ResultCode(data::ResultCode::Numeric::kInstallFailed, "Failed to finalize the pending update installation");
  } else {
    install_res = data::InstallationResult(data::ResultCode::Numeric::kOk, "Installing fake package was successful");
  }

  return install_res;
}


SwupdateManager::~SwupdateManager(const PackageConfig &pconfig, const BootloaderConfig &bconfig,
                             const std::shared_ptr<INvStorage> &storage, const std::shared_ptr<HttpInterface> &http,
                             Bootloader *bootloader)
    : PackageManagerInterface(pconfig, BootloaderConfig(), storage, http) {
  this->data_ready(false);
  this->data_read(false);
  this->unrecoverable_error(false);
}

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
    if (this->unrecoverable_error.load()) {
        // If an error has already occurred, abort further processing
        exit(-1);
    }

    assert(userp);
    auto* dst = static_cast<DownloadMetaStruct*>(userp);
    size_t downloaded = size * nmemb;
    uint64_t expected = dst->target.length();

    if ((dst->downloaded_length + downloaded) > expected) {
        std::cerr << "Download size exceeds expected length." << std::endl;
        this->unrecoverable_error.store(true);
        this->buffer_cv.notify_all(); // Wake up any waiting threads
        exit(-1);
    }

    try {
        std::unique_lock<std::mutex> lock(this->buffer_mutex);

        // Wait until data_ready is false or an error occurs
        this->buffer_cv.wait(lock, [] { return !this->data_ready.load() || this->unrecoverable_error.load(); });

        if (this->unrecoverable_error.load()) {
            exit(-1);
        }

        this->data_buffer.clear();
        this->data_buffer.resize(downloaded);  // Resize buffer to match the downloaded size
        std::memcpy(this->data_buffer.data(), contents, downloaded);

        dst->hasher().update(reinterpret_cast<const unsigned char*>(contents), downloaded);
        dst->downloaded_length += downloaded;

        if (dst->downloaded_length == expected) {
            auto final_hash = this->ds->hasher().getHash().HashString();

            std::string expected_hash = jsonDataOut["custom"]["swupdate"]["rawHashes"]["sha256"].asString();
            if (final_hash != expected_hash) {
                std::cerr << "Hash mismatch! Expected: " << expected_hash << ", Got: " << final_hash << std::endl;
                this->unrecoverable_error.store(true);
                this->buffer_cv.notify_all();
                exit(-1);
            }
            std::cerr << "Full update verified successfully!" << std::endl;
        }

        this->data_ready.store(true);
        this->data_read.store(false);

        // Notify readimage that data is available
        this->buffer_cv.notify_one();

        // Now wait until readimage has finished reading the data or an error occurs
        this->buffer_cv.wait(lock, [] { return this->data_read.load() || this->unrecoverable_error.load(); });

        if (this->unrecoverable_error.load()) {
            exit(-1);
        }

        // std::cout << "Downloaded: " << dst->downloaded_length << "/" << expected << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception in DownloadHandler: " << e.what() << std::endl;
        this->unrecoverable_error.store(true);
        this->buffer_cv.notify_all(); // Wake up any waiting threads
        exit(-1);
    }

    return downloaded;
}

// Read Image Function
int readimage(char** pbuf, int* size) {
    std::unique_lock<std::mutex> lock(this->buffer_mutex);

    // Wait until data is ready or an error has occurred
    this->buffer_cv.wait(lock, [] { return this->data_ready.load() || this->unrecoverable_error.load(); });

    if (this->unrecoverable_error.load()) {
        return -1; // Indicate an error to stop the update process
    }

    // Copy data to avoid data races
    std::vector<char> data_buffer_copy(this->data_buffer.size());
    std::memcpy(data_buffer_copy.data(), this->data_buffer.data(), this->data_buffer.size());

    *pbuf = data_buffer_copy.data();
    *size = static_cast<int>(this->data_buffer.size());

    // After the data has been read, mark it as read and notify DownloadHandler
    this->data_ready.store(false);
    this->data_read.store(true);
    this->buffer_cv.notify_one();

    return *size;
}

// Status Print Function
int printstatus(ipc_message *msg) {
    if (verbose) {
        std::printf("Status: %d message: %s\n",
                  msg->data.notify.status,
                  msg->data.notify.msg);
    }
    return 0;
}

// End Function
int end_update(RECOVERY_STATUS status) {
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

int swupdate_install() {
    struct swupdate_request req;
    int rc;
    std::shared_ptr<HttpInterface> http;

    http = std::make_shared<HttpClient>();
    // std::dynamic_pointer_cast<HttpClient>(http)->timeout(10000000);
    Uptane::Target target("test", jsonDataOut);
    this->ds = std_::make_unique<DownloadMetaStruct>(target, nullptr, nullptr);

    swupdate_prepare_req(&req);

    rc = swupdate_async_start(readimage, printstatus, end_update, &req, sizeof(req));
    if (rc < 0) {
        std::cerr << "swupdate start error" << std::endl;
        return EXIT_FAILURE;
    }

    std::string url = jsonDataOut["url"].asString();

    // Start the download in a separate thread to avoid blocking
    std::thread download_thread([&]() {
        HttpResponse response = http->download(
            url,
            DownloadHandler,
            nullptr,  // ProgressHandler can be added if needed
            this->ds.get(), // userp
            static_cast<curl_off_t>(this->ds->downloaded_length)  // from
        );

        // Check the response for errors
        if (response.http_status_code != 200) {
            std::cerr << "HTTP download failed with status: " << response.http_status_code << std::endl;
            this->unrecoverable_error.store(true);
            this->buffer_cv.notify_all(); // Wake up any waiting threads
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

    if (this->unrecoverable_error.load()) {
        std::cerr << "An unrecoverable error occurred. Update process stopped." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}