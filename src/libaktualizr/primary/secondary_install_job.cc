#include "primary/secondary_install_job.h"
#include "logging/logging.h"
#include "primary/sotauptaneclient.h"

void SecondaryEcuInstallationJob::SendFirmwareAsync() {
  firmware_send_ = std::async(std::launch::async, &SecondaryEcuInstallationJob::SendFirmware, this);
}

// Called in bg thread from SendFirmwareAsync
void SecondaryEcuInstallationJob::SendFirmware() {
  uptane_client_.sendEvent<event::InstallStarted>(ecu_serial_);
  uptane_client_.report_queue->enqueue(std_::make_unique<EcuInstallationStartedReport>(ecu_serial_, correlation_id_));

  try {
    installation_result_ = secondary_.sendFirmware(target_);
  } catch (const std::exception &ex) {
    installation_result_ = data::InstallationResult(data::ResultCode::Numeric::kInternalError, ex.what());
  }
}

void SecondaryEcuInstallationJob::WaitForFirmwareSent() { firmware_send_.wait(); }

void SecondaryEcuInstallationJob::InstallAsync() {
  install_ = std::async(std::launch::async, &SecondaryEcuInstallationJob::Install, this);
}

// Called in bg thread from InstallAsync
void SecondaryEcuInstallationJob::Install() {
  if (!Ok()) {
    LOG_ERROR << "SecondaryEcuInstallationJob::InstallAsync() called even though sending firmware failed";
    return;
  }

  try {
    InstallInfo info(update_type_);
    if (update_type_ == UpdateType::kOffline) {
      if (!uptane_client_.uptane_fetcher_offupd) {
        installation_result_ = data::InstallationResult(data::ResultCode(data::ResultCode::Numeric::kGeneralError),
                                                        "sendFirmwareAsync: offline fetcher not set");
        return;
      }
      info.initOffline(uptane_client_.uptane_fetcher_offupd->getImagesPath(),
                       uptane_client_.uptane_fetcher_offupd->getMetadataPath());
    }
    installation_result_ = secondary_.install(target_, info);
  } catch (const std::exception &ex) {
    installation_result_ = data::InstallationResult(data::ResultCode::Numeric::kInternalError, ex.what());
  }

  if (installation_result_.result_code == data::ResultCode::Numeric::kNeedCompletion) {
    uptane_client_.report_queue->enqueue(std_::make_unique<EcuInstallationAppliedReport>(ecu_serial_, correlation_id_));
  } else {
    uptane_client_.report_queue->enqueue(std_::make_unique<EcuInstallationCompletedReport>(
        ecu_serial_, correlation_id_, installation_result_.isSuccess()));
  }

  uptane_client_.sendEvent<event::InstallTargetComplete>(ecu_serial_, Ok());
  have_installed_ = true;
}

void SecondaryEcuInstallationJob::WaitForInstall() { install_.wait(); }

bool SecondaryEcuInstallationJob::Ok() const { return installation_result_.isSuccess(); }

result::Install::EcuReport SecondaryEcuInstallationJob::InstallationReport() const {
  if (have_installed_) {
    // If both steps of the installation ran, return the final result
    return result::Install::EcuReport(target_, ecu_serial_, installation_result_);
  }
  // If we only ran the first step and it failed, report the failure
  if (!Ok()) {
    return result::Install::EcuReport(target_, ecu_serial_, installation_result_);
  }
  // If the overall install aborted (due to some other ECU failing to send), return a result that shows the installation
  // aborted and not a 'success' code, even though the first part was a success.
  return result::Install::EcuReport(
      target_, ecu_serial_,
      data::InstallationResult(data::ResultCode(data::ResultCode::Numeric::kOperationCancelled),
                               "Install aborted because not all ECUs received the update"));
}