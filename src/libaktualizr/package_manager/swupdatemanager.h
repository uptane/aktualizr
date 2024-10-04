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

class SwupdateManager : public PackageManagerInterface {
public:
    SwupdateManager(const PackageConfig &pconfig, const BootloaderConfig &bconfig,
                       const std::shared_ptr<INvStorage> &storage, const std::shared_ptr<HttpInterface> &http)
        : PackageManagerInterface(pconfig, bconfig, storage, http), bootloader_{new Bootloader(bconfig, *storage_)} {}
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
    void completeInstall() const override;
    data::InstallationResult finalizeInstall(const Uptane::Target &target) override;
    void updateNotify() override { bootloader_->updateNotify(); };
    bool fetchTarget(const Uptane::Target &target, Uptane::Fetcher &fetcher, const KeyManager &keys,
                    const FetcherProgressCb &progress_cb, const api::FlowControlToken *token) override;

private:
    // bool swupdate_init();
    // void swupdate_finalize();
    // std::vector<SwupdatePackage> swupdate_get_installed_packages() const;
    // SwupdateTarget swupdate_get_current_target() const;
    // bool swupdate_install_target(const Uptane::Target &target) const;
    // std::string swupdate_get_current_hash() const;
    // void swupdate_download_target(const Uptane::Target &target, Uptane::Fetcher &fetcher,
    //                               const KeyManager &keys, const FetcherProgressCb &progress_cb,
    //                               const api::FlowControlToken *token);
    // bool swupdate_verify_target(const Uptane::Target &target) const;

    std::unique_ptr<Bootloader> bootloader_;
};

#endif // SWUPDATEMANAGER_H
