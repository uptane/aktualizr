#include <iostream>
#include <fstream>
#include <string>
#include <pthread.h>
#include "json/json.h"
#include "libaktualizr/packagemanagerfactory.h"
#include "http/httpclient.h"
#include <unistd.h>  // For the read system call
#include <fcntl.h>   // For open, O_RDONLY

extern "C" {
#include "network_ipc.h"
}

#include <sys/statvfs.h>
#include <boost/filesystem.hpp>
#include <chrono>
#include "crypto/crypto.h"
#include "crypto/keymanager.h"
#include "logging/logging.h"
#include "uptane/exceptions.h"
#include "uptane/fetcher.h"
#include "utilities/apiqueue.h"

#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

// Global Variables
std::ofstream outFile;

std::mutex buffer_mutex;
std::condition_variable buffer_cv;
std::vector<char> data_buffer;

// Flags to control the flow
std::atomic<bool> pause_download(false);
std::atomic<bool> data_ready(false);
std::atomic<bool> data_read(false);
std::atomic<bool> unrecoverable_error(false); // New flag for errors

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

Json::Value jsonDataOut;

// Initialize mutex and condition variable
pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv_end = PTHREAD_COND_INITIALIZER;

int verbose = 1;

// URL
std::string url = "http://192.168.219.89:8080/swupdate-torizon-benchmark-image-verdin-imx8mm-20240907181051.swu";
std::shared_ptr<HttpInterface> http;
std::unique_ptr<DownloadMetaStruct> ds;

// Function to parse JSON file
int parseJsonFile(const std::string& jsonFilePath, Json::Value& jsonData) {
    std::ifstream jsonFile(jsonFilePath, std::ifstream::binary);
    if (!jsonFile.is_open()) {
        std::cerr << "JSON could not be opened: " << jsonFilePath << std::endl;
        return 1;
    }

    Json::CharReaderBuilder readerBuilder;
    std::string errs;

    if (!Json::parseFromStream(readerBuilder, jsonFile, &jsonData, &errs)) {
        std::cerr << "JSON parsing error: " << errs << std::endl;
        return 1;
    }
    return 0;
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

            std::string expected_hash = jsonDataOut["custom"]["swupdate"]["rawHashes"]["sha256"].asString();
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
int readimage(char** pbuf, int* size) {
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

    // Write data to the output file
    if (!outFile.write(data_buffer.data(), data_buffer.size())) {
        std::cerr << "Error writing to output file." << std::endl;
        unrecoverable_error.store(true);
        buffer_cv.notify_all(); // Wake up any waiting threads
        return -1; // Indicate an error
    }

    // After the data has been read, mark it as read and notify DownloadHandler
    data_ready.store(false);
    data_read.store(true);
    buffer_cv.notify_one();

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
    outFile.close();
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

// SWUpdate Test Function
int swupdate_test_func() {
    struct swupdate_request req;
    int rc;

    http = std::make_shared<HttpClient>();
    // std::dynamic_pointer_cast<HttpClient>(http)->timeout(10000000);
    Uptane::Target target("test", jsonDataOut);
    ds = std_::make_unique<DownloadMetaStruct>(target, nullptr, nullptr);

    swupdate_prepare_req(&req);

    outFile.open("filename.swu", std::ios::out | std::ios::trunc);
    if (!outFile) {
        std::cerr << "Error opening file" << std::endl;
        return EXIT_FAILURE; // Return an error code
    }

    rc = swupdate_async_start(readimage, printstatus, end_update, &req, sizeof(req));
    if (rc < 0) {
        std::cerr << "swupdate start error" << std::endl;
        return EXIT_FAILURE;
    }

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

// Main Function
int main() {
    std::string jsonFilePath = "./test.json";

    if (parseJsonFile(jsonFilePath, jsonDataOut) == 0) {
        int result = swupdate_test_func();
        return result;
    } else {
        std::cerr << "Failed to parse JSON file. Exiting." << std::endl;
        return EXIT_FAILURE;
    }
}
