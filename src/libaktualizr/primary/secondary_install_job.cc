#include "primary/secondary_install_job.h"
#include "logging/logging.h"
#include "primary/sotauptaneclient.h"

// These are passed as const reference not value because they are references,
// not rvalues at the call site.
SecondaryEcuInstallationJob::SecondaryEcuInstallationJob(
    SotaUptaneClient& uptane_client, SecondaryInterface& secondary,
    const Uptane::EcuSerial& ecu_serial,  // NOLINT(modernize-pass-by-value)
    const Uptane::Target& target,         // NOLINT(modernize-pass-by-value)
    const std::string& correlation_id,    // NOLINT(modernize-pass-by-value)
    UpdateType update_type)

    : uptane_client_{uptane_client},
      secondary_{secondary},
      target_{target},
      ecu_serial_{ecu_serial},
      correlation_id_{correlation_id},
      install_info_{update_type} {
  target_.setCorrelationId(correlation_id);  // TODO: necessary?

  if (update_type == UpdateType::kOffline) {
    if (uptane_client_.uptane_fetcher_offupd) {
      install_info_.initOffline(uptane_client_.uptane_fetcher_offupd->getImagesPath(),
                                uptane_client_.uptane_fetcher_offupd->getMetadataPath());
    } else {
      installation_result_ = data::InstallationResult(data::ResultCode(data::ResultCode::Numeric::kGeneralError),
                                                      "sendFirmwareAsync: offline fetcher not set");
    }
  }
}

void SecondaryEcuInstallationJob::SendFirmwareAsync() {
  firmware_send_ = std::async(std::launch::async, &SecondaryEcuInstallationJob::SendFirmware, this);
}

// Called in bg thread from SendFirmwareAsync
void SecondaryEcuInstallationJob::SendFirmware() {
  if (!installation_result_.isSuccess()) {
    // Can fail in the ctor, but we can't report it until now
    return;
  }
  uptane_client_.sendEvent<event::InstallStarted>(ecu_serial_);
  // TODO: Is this the right time to send EcuInstallationStartedReport
  uptane_client_.report_queue->enqueue(std_::make_unique<EcuInstallationStartedReport>(ecu_serial_, correlation_id_));

  try {
    installation_result_ = secondary_.sendFirmware(target_, install_info_, uptane_client_.flow_control_);
  } catch (const std::exception& ex) {
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
    installation_result_ = secondary_.install(target_, install_info_, uptane_client_.flow_control_);
  } catch (const std::exception& ex) {
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