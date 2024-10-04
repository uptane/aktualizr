#ifndef SWUPDATEMANAGER_H
#define SWUPDATEMANAGER_H

#include "libaktualizr/packagemanagerinterface.h"
#include "bootloader/bootloader.h"
#include "http/httpinterface.h"
#include "storage/invstorage.h"

#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>

class SwupdateManager : public PackageManagerInterface {
public:
    SwupdateManager(const PackageConfig &pconfig, const BootloaderConfig &bconfig,
                       const std::shared_ptr<INvStorage> &storage, const std::shared_ptr<HttpInterface> &http)
        : PackageManagerInterface(pconfig, bconfig, storage, http) {}
    ~SwupdateManager() override = default;
    SwupdateManager(const SwupdateManager &) = delete;
    SwupdateManager(SwupdateManager &&) = delete;
    SwupdateManager &operator=(const SwupdateManager &) = delete;
    SwupdateManager &operator=(SwupdateManager &&) = delete;

    std::string name() const override { return "swupdate"; }
    Json::Value getInstalledPackages() const override;
    virtual std::string getCurrentHash() const; // added
    Uptane::Target getCurrent() const override;

    data::InstallationResult install(const Uptane::Target &target) const override;
    data::InstallationResult finalizeInstall(const Uptane::Target &target) override;
    bool fetchTarget(const Uptane::Target &target, Uptane::Fetcher &fetcher, const KeyManager &keys,
                    const FetcherProgressCb &progress_cb, const api::FlowControlToken *token) override;

private:
    static size_t DownloadHandler(char* contents, size_t size, size_t nmemb, void* userp);
    int readimage(char** pbuf, int* size);
    int printstatus(ipc_message *msg);
    int end_update(RECOVERY_STATUS status);
    int swupdate_install();
    
    std::mutex buffer_mutex;
    std::condition_variable buffer_cv;
    std::vector<char> data_buffer;
    std::unique_ptr<DownloadMetaStruct> ds;

    std::atomic<bool> data_ready(false);
    std::atomic<bool> data_read(false);
    std::atomic<bool> unrecoverable_error(false);
};

#endif // SWUPDATEMANAGER_H
