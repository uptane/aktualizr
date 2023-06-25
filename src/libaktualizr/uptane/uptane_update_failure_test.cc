#include <gtest/gtest.h>

#include <iostream>
#include <memory>

#include <boost/filesystem.hpp>
#include <boost/polymorphic_pointer_cast.hpp>

#include "libaktualizr/packagemanagerfactory.h"

#include "http/httpclient.h"
#include "httpfake.h"
#include "logging/logging.h"
#include "package_manager/ostreemanager.h"
#include "primary/reportqueue.h"
#include "primary/sotauptaneclient.h"
#include "storage/invstorage.h"
#include "uptane/uptanerepository.h"
#include "uptane_test_common.h"
#include "utilities/utils.h"

namespace {
/**
 * A Mock Secondary that fails on demand
 */
class FailingSecondary : public SecondaryInterface {
 public:
  explicit FailingSecondary(Primary::VirtualSecondaryConfig &sconfig_in) : sconfig(std::move(sconfig_in)) {
    std::string public_key_str;
    if (!Crypto::generateKeyPair(sconfig.key_type, &public_key_str, &private_key)) {
      throw std::runtime_error("Key generation failure");
    }
    public_key = PublicKey(public_key_str, sconfig.key_type);
    Json::Value manifest_unsigned;
    manifest_unsigned["key"] = "value";

    std::string const b64sig = Utils::toBase64(
        Crypto::Sign(sconfig.key_type, nullptr, private_key, Utils::jsonToCanonicalStr(manifest_unsigned)));
    Json::Value signature;
    signature["method"] = "rsassa-pss";
    signature["sig"] = b64sig;
    signature["keyid"] = public_key.KeyId();
    manifest_["signed"] = manifest_unsigned;
    manifest_["signatures"].append(signature);
  }
  void init(std::shared_ptr<SecondaryProvider> secondary_provider_in) override {
    secondary_provider = std::move(secondary_provider_in);
  }
  std::string Type() const override { return "mock"; }
  PublicKey getPublicKey() const override { return public_key; }

  Uptane::HardwareIdentifier getHwId() const override { return Uptane::HardwareIdentifier(sconfig.ecu_hardware_id); }
  Uptane::EcuSerial getSerial() const override {
    if (!sconfig.ecu_serial.empty()) {
      return Uptane::EcuSerial(sconfig.ecu_serial);
    }
    return Uptane::EcuSerial(public_key.KeyId());
  }
  Uptane::Manifest getManifest() const override {
    Json::Value manifest = Uptane::ManifestIssuer::assembleManifest(firmware_info, getSerial());
    manifest["attacks_detected"] = "";
    Json::Value signed_ecu_version;
    auto const b64sig = Utils::toBase64(Crypto::RSAPSSSign(nullptr, private_key, Utils::jsonToCanonicalStr(manifest)));
    Json::Value signature;
    signature["method"] = "rsassa-pss";
    signature["sig"] = b64sig;

    signature["keyid"] = public_key.KeyId();
    signed_ecu_version["signed"] = manifest;
    signed_ecu_version["signatures"] = Json::Value(Json::arrayValue);
    signed_ecu_version["signatures"].append(signature);

    return signed_ecu_version;
  }
  bool ping() const override { return true; }

  data::InstallationResult putMetadata(const Uptane::Target & /*target*/) override {
    return {data::ResultCode::Numeric::kOk, ""};
  }
  int32_t getRootVersion(bool /*director*/) const override { return 1; }

  data::InstallationResult putRoot(const std::string & /*root*/, bool /*director*/) override {
    return {data::ResultCode::Numeric::kOk, ""};
  }
  data::InstallationResult sendFirmware(const Uptane::Target & /*target*/, const InstallInfo & /*install_info*/,
                                        const api::FlowControlToken *flow_control) override {
    send_firmware_calls++;
    if (abort_during_send_firmware) {
      assert(flow_control != nullptr);
      // Simulate a user on a separate thread cancelling the ongoing operation
      auto *fct = const_cast<api::FlowControlToken *>(flow_control);
      fct->setAbort();
      assert(flow_control->hasAborted());
      return {data::ResultCode::Numeric::kOperationCancelled, ""};
    }
    return {send_firmware_result, ""};
  }
  data::InstallationResult install(const Uptane::Target &target, const InstallInfo & /*info*/,
                                   const api::FlowControlToken * /*flow_control*/) override {
    was_sync_update = secondary_provider->pendingPrimaryUpdate();
    if (was_sync_update) {
      // For a synchronous update, most of this step happens on reboot.
      return {data::ResultCode::Numeric::kNeedCompletion, ""};
    }
    install_calls++;
    return installCommon(target);
  }

  boost::optional<data::InstallationResult> completePendingInstall(const Uptane::Target &target) override {
    complete_pending_install_calls++;
    return installCommon(target);
  }

  data::InstallationResult installCommon(const Uptane::Target &target) {
    if (install_result == data::ResultCode::Numeric::kOk) {
      // Record the fact we did an update so it appears in getManifest()
      firmware_info.hash = target.sha256Hash();
      firmware_info.len = target.length();
      firmware_info.name = target.filename();
    }
    return {install_result, ""};
  }

  void rollbackPendingInstall() override { rollback_pending_install_calls++; }

  void cleanStartup() override { nothing_pending_calls++; }

#ifdef BUILD_OFFLINE_UPDATES
  data::InstallationResult putMetadataOffUpd(const Uptane::Target &, const Uptane::OfflineUpdateFetcher &) override {
    return {data::ResultCode::Numeric::kInternalError, "SecondaryInterfaceMock::putMetadataOffUpd not implemented"};
  }
#endif

  std::shared_ptr<SecondaryProvider> secondary_provider;
  PublicKey public_key;
  std::string private_key;
  Json::Value manifest_;

  Primary::VirtualSecondaryConfig sconfig;
  int send_firmware_calls{0};
  data::ResultCode::Numeric send_firmware_result{data::ResultCode::Numeric::kOk};
  int install_calls{0};
  int complete_pending_install_calls{0};
  int rollback_pending_install_calls{0};
  int nothing_pending_calls{0};
  // This result is used for both install and completePendingInstall
  data::ResultCode::Numeric install_result{data::ResultCode::Numeric::kOk};
  bool was_sync_update{false};
  Uptane::InstalledImageInfo firmware_info;
  // Simulate a user abort during sendFirmware
  bool abort_during_send_firmware{false};
};

struct TestOptions {
  bool fail_primary_install{false};
  bool primary_installs_on_reboot{true};
};

struct TestScaffolding {
  explicit TestScaffolding(TestOptions test_options = TestOptions())
      : conf{"tests/config/basic.toml"},
        temp_dir{},
        http{std::make_shared<HttpFake>(temp_dir.Path(), "hasupdates")},
        events_channel{std::make_shared<event::Channel>()} {
    conf.provision.primary_ecu_serial = "CA:FE:A6:D2:84:9D";
    conf.provision.primary_ecu_hardware_id = "primary_hw";
    conf.uptane.director_server = http->tls_server + "/director";
    conf.uptane.repo_server = http->tls_server + "/repo";
    conf.uptane.force_install_completion = true;
    conf.pacman.images_path = temp_dir.Path() / "images";
    conf.bootloader.reboot_sentinel_dir = temp_dir.Path();
    conf.pacman.fake_need_reboot = test_options.primary_installs_on_reboot;
    conf.pacman.fake_fail_install = test_options.fail_primary_install;

    conf.storage.path = temp_dir.Path();
    conf.tls.server = http->tls_server;

    storage = INvStorage::newStorage(conf.storage);

    ecu_config.partial_verifying = false;
    ecu_config.full_client_dir = temp_dir.Path();
    ecu_config.ecu_serial = "secondary_ecu_serial";
    ecu_config.ecu_hardware_id = "secondary_hw";
    ecu_config.ecu_private_key = "secondary.priv";
    ecu_config.ecu_public_key = "secondary.pub";
    ecu_config.firmware_path = temp_dir / "firmware.txt";
    ecu_config.target_name_path = temp_dir / "firmware_name.txt";
    ecu_config.metadata_path = temp_dir / "secondary_metadata";
    secondary = std::make_shared<FailingSecondary>(ecu_config);

    events_channel->connect([this](const std::shared_ptr<event::BaseEvent> &event) {
      events[event->variant]++;
      if (event->variant == "AllInstallsComplete") {
        auto concrete_event = std::static_pointer_cast<event::AllInstallsComplete>(event);
        EXPECT_EQ(expected_install_report, concrete_event->result.dev_report.result_code.num_code);
      }
    });

    dut = std_::make_unique<UptaneTestCommon::TestUptaneClient>(conf, storage, http, events_channel);
    dut->addSecondary(secondary);
  }

  void Reboot() {
    boost::filesystem::remove(conf.bootloader.reboot_sentinel_dir / conf.bootloader.reboot_sentinel_name);
    dut = std_::make_unique<UptaneTestCommon::TestUptaneClient>(conf, storage, http);
    dut->addSecondary(secondary);
  }

  Config conf;
  TemporaryDirectory temp_dir;
  std::shared_ptr<HttpFake> http;
  Primary::VirtualSecondaryConfig ecu_config;
  std::shared_ptr<FailingSecondary> secondary;
  std::shared_ptr<INvStorage> storage;
  std::unique_ptr<UptaneTestCommon::TestUptaneClient> dut;
  std::shared_ptr<event::Channel> events_channel;
  std::map<std::string, int> events;
  data::ResultCode::Numeric expected_install_report{data::ResultCode::Numeric::kUnknown};
};

}  // anonymous namespace

/*
 * Send metadata to Secondary ECUs
 * Send EcuInstallationStartedReport to server for Secondaries
 */
TEST(UptaneUpdateFailure, SynchronousSecondaryUpdatesSuccess) {
  TestScaffolding s;  // NOLINT

  EXPECT_NO_THROW(s.dut->initialize());
  EXPECT_EQ(s.secondary->nothing_pending_calls, 1);

  result::UpdateCheck const update_result = s.dut->fetchMeta();
  EXPECT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
  // Preparatory work
  result::Download const download_result = s.dut->downloadImages(update_result.updates);
  EXPECT_EQ(download_result.status, result::DownloadStatus::kSuccess);
  EXPECT_EQ(s.secondary->install_calls, 0);
  EXPECT_EQ(s.secondary->send_firmware_calls, 0);

  s.expected_install_report = data::ResultCode::Numeric::kNeedCompletion;
  result::Install const install_result = s.dut->uptaneInstall(download_result.updates);
  EXPECT_FALSE(install_result.dev_report.isSuccess());
  EXPECT_EQ(install_result.dev_report.result_code, data::ResultCode::Numeric::kNeedCompletion);
  EXPECT_EQ(s.secondary->send_firmware_calls, 1);
  EXPECT_EQ(s.secondary->install_calls, 0) << "Secondary will have reported kNeedCompletion";
  EXPECT_EQ(s.secondary->complete_pending_install_calls, 0);
  EXPECT_TRUE(s.secondary->was_sync_update);
  EXPECT_EQ(s.events["AllInstallsComplete"], 1);

  // Simulate a reboot
  s.Reboot();
  s.expected_install_report = data::ResultCode::Numeric::kOk;
  EXPECT_NO_THROW(s.dut->initialize());
  EXPECT_EQ(s.secondary->nothing_pending_calls, 1) << "Shouldn't be called when there is a pending update";

  EXPECT_EQ(s.secondary->send_firmware_calls, 1);
  EXPECT_EQ(s.secondary->install_calls, 0);
  EXPECT_EQ(s.secondary->complete_pending_install_calls, 1);
  EXPECT_EQ(s.events["AllInstallsComplete"], 1);
}

/**
 * Exercise a couple of failure cases during a synchronous install
 * 1) Download failure
 * 2) Secondary Installation Failure
 * 3) Success
 */
TEST(UptaneUpdateFailure, SynchronousSecondaryUpdatesFailure) {
  TestScaffolding s;  // NOLINT

  EXPECT_NO_THROW(s.dut->initialize());
  result::UpdateCheck update_result = s.dut->fetchMeta();
  EXPECT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
  // Preparatory work
  result::Download download_result = s.dut->downloadImages(update_result.updates);
  EXPECT_EQ(download_result.status, result::DownloadStatus::kSuccess);
  EXPECT_EQ(s.secondary->install_calls, 0);
  EXPECT_EQ(s.secondary->send_firmware_calls, 0);

  // Case 1: Sending the firmware fails
  s.secondary->send_firmware_result = data::ResultCode::Numeric::kDownloadFailed;
  s.expected_install_report = data::ResultCode::Numeric::kDownloadFailed;
  result::Install install_result = s.dut->uptaneInstall(download_result.updates);
  EXPECT_FALSE(install_result.dev_report.isSuccess());
  EXPECT_EQ(install_result.dev_report.result_code, data::ResultCode::Numeric::kDownloadFailed);
  EXPECT_EQ(s.secondary->install_calls, 0);
  EXPECT_EQ(s.secondary->send_firmware_calls, 1);

  // Case 2: Installing the secondary firmware fails
  s.secondary->send_firmware_result = data::ResultCode::Numeric::kOk;
  s.secondary->install_result = data::ResultCode::Numeric::kDownloadFailed;
  s.secondary->install_calls = 0;
  s.secondary->send_firmware_calls = 0;
  // First time through it needs a reboot
  s.expected_install_report = data::ResultCode::Numeric::kNeedCompletion;
  install_result = s.dut->uptaneInstall(download_result.updates);
  EXPECT_FALSE(install_result.dev_report.isSuccess());
  EXPECT_EQ(install_result.dev_report.result_code, data::ResultCode::Numeric::kNeedCompletion);
  EXPECT_EQ(s.secondary->install_calls, 0);
  EXPECT_EQ(s.secondary->send_firmware_calls, 1);
  EXPECT_EQ(s.secondary->complete_pending_install_calls, 0);
  EXPECT_TRUE(s.dut->isInstallCompletionRequired());

  // Simulate a reboot
  s.Reboot();
  EXPECT_NO_THROW(s.dut->initialize());

  EXPECT_EQ(s.secondary->install_calls, 0);
  EXPECT_EQ(s.secondary->send_firmware_calls, 1);
  EXPECT_EQ(s.secondary->complete_pending_install_calls, 1);

  // Case 3: Happy path
  s.secondary->send_firmware_result = data::ResultCode::Numeric::kOk;
  s.secondary->install_result = data::ResultCode::Numeric::kOk;
  s.secondary->install_calls = 0;
  s.secondary->send_firmware_calls = 0;
  s.secondary->complete_pending_install_calls = 0;

  update_result = s.dut->fetchMeta();
  EXPECT_EQ(update_result.status, result::UpdateStatus::kUpdatesAvailable);
  download_result = s.dut->downloadImages(update_result.updates);
  s.expected_install_report = data::ResultCode::Numeric::kOk;
  install_result = s.dut->uptaneInstall(download_result.updates);
  EXPECT_TRUE(install_result.dev_report.isSuccess());
  EXPECT_FALSE(s.secondary->was_sync_update);  // The primary update installed already, we don't try and roll them back
  EXPECT_EQ(install_result.dev_report.result_code, data::ResultCode::Numeric::kOk);
  EXPECT_EQ(s.secondary->install_calls, 1);
  EXPECT_EQ(s.secondary->send_firmware_calls, 1);
  EXPECT_EQ(s.secondary->complete_pending_install_calls, 0);
  EXPECT_FALSE(s.dut->isInstallCompletionRequired());
}

/**
 * The user cancels during an installation
 */
TEST(UptaneUpdateFailure, Cancellation) {
  TestScaffolding s;  // NOLINT
  s.dut->initialize();

  result::UpdateCheck const update_result = s.dut->fetchMeta();
  result::Download const download_result = s.dut->downloadImages(update_result.updates);
  EXPECT_EQ(download_result.status, result::DownloadStatus::kSuccess);

  s.secondary->abort_during_send_firmware = true;
  s.expected_install_report = data::ResultCode::Numeric::kOperationCancelled;
  result::Install const install_result = s.dut->uptaneInstall(download_result.updates);
  EXPECT_FALSE(install_result.dev_report.isSuccess());
  EXPECT_EQ(install_result.dev_report.result_code, data::ResultCode::Numeric::kOperationCancelled);
  EXPECT_EQ(s.secondary->install_calls, 0);
  EXPECT_EQ(s.secondary->send_firmware_calls, 1);
}

/**
 * A sync update where both primary and secondary install without reboot
 */
TEST(UptaneUpdateFailure, SuccessNoReboot) {
  TestOptions test_options;
  test_options.primary_installs_on_reboot = false;
  TestScaffolding s{test_options};  // NOLINT
  s.dut->initialize();

  result::UpdateCheck const update_result = s.dut->fetchMeta();
  result::Download const download_result = s.dut->downloadImages(update_result.updates);
  EXPECT_EQ(download_result.status, result::DownloadStatus::kSuccess);

  s.expected_install_report = data::ResultCode::Numeric::kOk;
  result::Install const install_result = s.dut->uptaneInstall(download_result.updates);

  EXPECT_TRUE(install_result.dev_report.isSuccess());
  EXPECT_FALSE(s.secondary->was_sync_update);
  EXPECT_EQ(install_result.dev_report.result_code, data::ResultCode::Numeric::kOk);
  EXPECT_EQ(s.secondary->install_calls, 1);
  EXPECT_EQ(s.secondary->send_firmware_calls, 1);
}

/**
 * The primary can install without a reboot, and the installation on it fails.
 */
TEST(UptaneUpdateFailure, PrimaryInstallFailureNoReboot) {
  TestOptions test_options;
  test_options.primary_installs_on_reboot = false;
  test_options.fail_primary_install = true;
  TestScaffolding s{test_options};  // NOLINT
  s.dut->initialize();

  result::UpdateCheck const update_result = s.dut->fetchMeta();
  result::Download const download_result = s.dut->downloadImages(update_result.updates);
  EXPECT_EQ(download_result.status, result::DownloadStatus::kSuccess);

  s.expected_install_report = data::ResultCode::Numeric::kInstallFailed;
  result::Install const install_result = s.dut->uptaneInstall(download_result.updates);

  EXPECT_FALSE(install_result.dev_report.isSuccess());
  EXPECT_FALSE(s.secondary->was_sync_update);
  EXPECT_EQ(install_result.dev_report.result_code,
            data::ResultCode(data::ResultCode::Numeric::kInstallFailed, "primary_hw:INSTALL_FAILED"));
  EXPECT_EQ(s.secondary->send_firmware_calls, 1);
  EXPECT_EQ(s.secondary->install_calls, 0);

  // Check the manifest that was reported to the backend
  s.dut->putManifest();
  auto manifest = s.http->last_manifest["signed"];
  auto report = manifest["installation_report"];

  auto expected_report = Utils::parseJSON(R"(
  {
          "content_type" : "application/vnd.com.here.otac.installationReport.v1",
          "report" :
          {
                  "correlation_id" : "id0",
                  "items" :
                  [
                          {
                                  "ecu" : "CA:FE:A6:D2:84:9D",
                                  "result" :
                                  {
                                          "code" : "INSTALL_FAILED",
                                          "description" : "PackageManagerFake install failed because of fake_fail_install",
                                          "success" : false
                                  }
                          }
                  ],
                  "raw_report" : "Installation failed on one or more ECUs",
                  "result" :
                  {
                          "code" : "primary_hw:INSTALL_FAILED",
                          "description" : "Installation failed on one or more ECUs",
                          "success" : false
                  }
          }
  })");
  EXPECT_EQ(expected_report, report);
}

/**
 * The primary needs a reboot to install, and on the reboot the installation fails.
 */
TEST(UptaneUpdateFailure, PrimaryInstallFailure) {
  TestOptions test_options;
  test_options.fail_primary_install = true;
  TestScaffolding s{test_options};  // NOLINT
  s.dut->initialize();

  result::UpdateCheck const update_result = s.dut->fetchMeta();
  result::Download const download_result = s.dut->downloadImages(update_result.updates);
  EXPECT_EQ(download_result.status, result::DownloadStatus::kSuccess);

  s.expected_install_report = data::ResultCode::Numeric::kNeedCompletion;
  result::Install const install_result = s.dut->uptaneInstall(download_result.updates);

  EXPECT_FALSE(install_result.dev_report.isSuccess());
  EXPECT_EQ(install_result.dev_report.result_code, data::ResultCode::Numeric::kNeedCompletion);
  EXPECT_EQ(s.secondary->install_calls, 0);
  EXPECT_EQ(s.secondary->send_firmware_calls, 1);

  // Simulate a reboot
  s.Reboot();
  // The AllInstallsComplete event isn't sent
  // s.expected_install_report = data::ResultCode::Numeric::kVerificationFailed;
  EXPECT_NO_THROW(s.dut->initialize());

  // Check the manifest that was reported to the backend
  auto manifest = s.http->last_manifest["signed"];
  auto report = manifest["installation_report"];
  // std::cout << "Actual installation report is:" << report;

  auto expected_report = Utils::parseJSON(R"(
  {
    "content_type" : "application/vnd.com.here.otac.installationReport.v1",
    "report" :
    {
      "correlation_id" : "id0",
      "items" :
      [
        {
          "ecu" : "CA:FE:A6:D2:84:9D",
          "result" :
          {
            "code" : "INSTALL_FAILED",
            "description" : "PackageManagerFake install failed after reboot because of fake_fail_install",
            "success" : false
          }
        },
        {
          "ecu" : "secondary_ecu_serial",
          "result" :
          {
            "code" : "OK",
            "description" : "",
            "success" : true
          }
        }
      ],
      "raw_report" : "Installation failed on one or more ECUs",
      "result" :
      {
        "code" : "primary_hw:INSTALL_FAILED",
        "description" : "Installation failed on one or more ECUs",
        "success" : false
      }
    }
  })");
  EXPECT_EQ(expected_report, report);

  EXPECT_EQ(s.secondary->install_calls, 0);
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  logger_init();
  return RUN_ALL_TESTS();
}
#endif