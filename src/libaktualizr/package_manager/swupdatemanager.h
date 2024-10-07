#ifndef SWUPDATEMANAGER_H
#define SWUPDATEMANAGER_H

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

class SwupdateManager : public PackageManagerInterface {
public:
    SwupdateManager(const PackageConfig &pconfig, const BootloaderConfig &bconfig,
                const std::shared_ptr<INvStorage> &storage, const std::shared_ptr<HttpInterface> &http,
                Bootloader *bootloader = nullptr);
    ~SwupdateManager() override;
    SwupdateManager(const SwupdateManager &) = delete;
    SwupdateManager(SwupdateManager &&) = delete;
    SwupdateManager &operator=(const SwupdateManager &) = delete;
    SwupdateManager &operator=(SwupdateManager &&) = delete;

    std::string name() const override { return "swupdate"; }
    Json::Value getInstalledPackages() const override;
    virtual std::string getCurrentHash() const;
    Uptane::Target getCurrent() const override;

    data::InstallationResult install(const Uptane::Target &target) const override;
    void completeInstall() const override;
    data::InstallationResult finalizeInstall(const Uptane::Target &target) override;
    void updateNotify() override;
    bool fetchTarget(const Uptane::Target &target, Uptane::Fetcher &fetcher, const KeyManager &keys,
                    const FetcherProgressCb &progress_cb, const api::FlowControlToken *token) override;

    static int readimage(char** pbuf, int* size);
    static int printstatus(ipc_message *msg);
    static int endupdate(RECOVERY_STATUS status);
    int swupdate_install(const Uptane::Target& target) const;

private:
    std::unique_ptr<Bootloader> bootloader_;
    std::shared_ptr<HttpInterface> http_;
};

#endif // SWUPDATEMANAGER_H
