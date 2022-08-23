#include "reportqueue.h"

#include <chrono>

#include "http/httpclient.h"
#include "libaktualizr/config.h"
#include "logging/logging.h"
#include "storage/invstorage.h"

ReportQueue::ReportQueue(const Config& config_in, std::shared_ptr<HttpInterface> http_client,
                         std::shared_ptr<INvStorage> storage_in, int run_pause_s, int event_number_limit)
    : config(config_in),
      http(std::move(http_client)),
      storage(std::move(storage_in)),
      run_pause_s_{run_pause_s},
      event_number_limit_{event_number_limit},
      cur_event_number_limit_{event_number_limit_} {
  if (event_number_limit == 0) {
    throw std::invalid_argument("Event number limit is set to 0 what leads to event accumulation in DB");
  }
  thread_ = std::thread(std::bind(&ReportQueue::run, this));
}

ReportQueue::~ReportQueue() {
  {
    std::lock_guard<std::mutex> lock(m_);
    shutdown_ = true;
  }
  cv_.notify_all();
  thread_.join();

  LOG_TRACE << "Flushing report queue";
  flushQueue();
}

void ReportQueue::run() {
  // Check if queue is nonempty. If so, move any reports to the Json array and
  // try to send it to the server. Clear the Json array only if the send
  // succeeds.
  std::unique_lock<std::mutex> lock(m_);
  while (!shutdown_) {
    flushQueue();
    cv_.wait_for(lock, std::chrono::seconds(run_pause_s_));
  }
}

void ReportQueue::enqueue(std::unique_ptr<ReportEvent> event) {
  {
    std::lock_guard<std::mutex> lock(m_);
    storage->saveReportEvent(event->toJson());
  }
  cv_.notify_all();
}

void ReportQueue::flushQueue() {
  int64_t max_id = 0;
  Json::Value report_array{Json::arrayValue};
  storage->loadReportEvents(&report_array, &max_id, cur_event_number_limit_);

  if (config.tls.server.empty()) {
    // Prevent a lot of unnecessary garbage output in uptane vector tests.
    LOG_TRACE << "No server specified. Clearing report queue.";
    report_array.clear();
  }

  if (!report_array.empty()) {
    HttpResponse response = http->post(config.tls.server + "/events", report_array);

    bool delete_events{response.isOk()};
    // 404 implies the server does not support this feature. Nothing we can
    // do, just move along.
    if (response.http_status_code == 404) {
      LOG_DEBUG << "Server does not support event reports. Clearing report queue.";
      delete_events = true;
    } else if (response.http_status_code == 413) {
      if (report_array.size() > 1) {
        // if 413 is received to posting of more than one event then try sending less events next time
        cur_event_number_limit_ = report_array.size() > 2 ? static_cast<int>(report_array.size() / 2U) : 1;
        LOG_DEBUG << "Got 413 response to request that contains " << report_array.size() << " events. Will try to send "
                  << cur_event_number_limit_ << " events.";
      } else {
        // An event is too big to be accepted by the server, let's drop it
        LOG_WARNING << "Dropping a report event " << report_array[0].get("id", "unknown") << " since the server `"
                    << config.tls.server << "` cannot digest it (413).";
        delete_events = true;
      }
    } else if (!response.isOk()) {
      LOG_WARNING << "Failed to post update events: " << response.getStatusStr();
    }
    if (delete_events) {
      report_array.clear();
      storage->deleteReportEvents(max_id);
      cur_event_number_limit_ = event_number_limit_;
    }
  }
}

void ReportEvent::setEcu(const Uptane::EcuSerial& ecu) { custom["ecu"] = ecu.ToString(); }
void ReportEvent::setCorrelationId(const std::string& correlation_id) {
  if (!correlation_id.empty()) {
    custom["correlationId"] = correlation_id;
  }
}

Json::Value ReportEvent::toJson() const {
  Json::Value out;

  out["id"] = id;
  out["deviceTime"] = timestamp.ToString();
  out["eventType"]["id"] = type;
  out["eventType"]["version"] = version;
  out["event"] = custom;

  return out;
}

CampaignAcceptedReport::CampaignAcceptedReport(const std::string& campaign_id) : ReportEvent("campaign_accepted", 0) {
  custom["campaignId"] = campaign_id;
}

CampaignDeclinedReport::CampaignDeclinedReport(const std::string& campaign_id) : ReportEvent("campaign_declined", 0) {
  custom["campaignId"] = campaign_id;
}

CampaignPostponedReport::CampaignPostponedReport(const std::string& campaign_id)
    : ReportEvent("campaign_postponed", 0) {
  custom["campaignId"] = campaign_id;
}

DevicePausedReport::DevicePausedReport(const std::string& correlation_id) : ReportEvent("DevicePaused", 0) {
  setCorrelationId(correlation_id);
}

DeviceResumedReport::DeviceResumedReport(const std::string& correlation_id) : ReportEvent("DeviceResumed", 0) {
  setCorrelationId(correlation_id);
}

EcuDownloadStartedReport::EcuDownloadStartedReport(const Uptane::EcuSerial& ecu, const std::string& correlation_id)
    : ReportEvent("EcuDownloadStarted", 0) {
  setEcu(ecu);
  setCorrelationId(correlation_id);
}

EcuDownloadCompletedReport::EcuDownloadCompletedReport(const Uptane::EcuSerial& ecu, const std::string& correlation_id,
                                                       bool success)
    : ReportEvent("EcuDownloadCompleted", 0) {
  setEcu(ecu);
  setCorrelationId(correlation_id);
  custom["success"] = success;
}

EcuInstallationStartedReport::EcuInstallationStartedReport(const Uptane::EcuSerial& ecu,
                                                           const std::string& correlation_id)
    : ReportEvent("EcuInstallationStarted", 0) {
  setEcu(ecu);
  setCorrelationId(correlation_id);
}

EcuInstallationAppliedReport::EcuInstallationAppliedReport(const Uptane::EcuSerial& ecu,
                                                           const std::string& correlation_id)
    : ReportEvent("EcuInstallationApplied", 0) {
  setEcu(ecu);
  setCorrelationId(correlation_id);
}

EcuInstallationCompletedReport::EcuInstallationCompletedReport(const Uptane::EcuSerial& ecu,
                                                               const std::string& correlation_id, bool success)
    : ReportEvent("EcuInstallationCompleted", 0) {
  setEcu(ecu);
  setCorrelationId(correlation_id);
  custom["success"] = success;
}
