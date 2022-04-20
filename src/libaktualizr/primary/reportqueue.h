#ifndef REPORTQUEUE_H_
#define REPORTQUEUE_H_

#include <json/json.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>  // for move

#include "libaktualizr/types.h"  // for EcuSerial (ptr only), TimeStamp
#include "utilities/utils.h"     // for Utils

class Config;
class HttpInterface;
class INvStorage;

class ReportEvent {
 public:
  std::string id;
  std::string type;
  int version;
  Json::Value custom;
  TimeStamp timestamp;

  Json::Value toJson() const;

 protected:
  ReportEvent(std::string event_type, int event_version)
      : id(Utils::randomUuid()), type(std::move(event_type)), version(event_version), timestamp(TimeStamp::Now()) {}

  void setEcu(const Uptane::EcuSerial& ecu);
  void setCorrelationId(const std::string& correlation_id);
};

class CampaignAcceptedReport : public ReportEvent {
 public:
  explicit CampaignAcceptedReport(const std::string& campaign_id);
};

class CampaignDeclinedReport : public ReportEvent {
 public:
  explicit CampaignDeclinedReport(const std::string& campaign_id);
};

class CampaignPostponedReport : public ReportEvent {
 public:
  explicit CampaignPostponedReport(const std::string& campaign_id);
};

class DevicePausedReport : public ReportEvent {
 public:
  explicit DevicePausedReport(const std::string& correlation_id);
};

class DeviceResumedReport : public ReportEvent {
 public:
  explicit DeviceResumedReport(const std::string& correlation_id);
};

class EcuDownloadStartedReport : public ReportEvent {
 public:
  EcuDownloadStartedReport(const Uptane::EcuSerial& ecu, const std::string& correlation_id);
};

class EcuDownloadCompletedReport : public ReportEvent {
 public:
  EcuDownloadCompletedReport(const Uptane::EcuSerial& ecu, const std::string& correlation_id, bool success);
};

class EcuInstallationStartedReport : public ReportEvent {
 public:
  EcuInstallationStartedReport(const Uptane::EcuSerial& ecu, const std::string& correlation_id);
};

class EcuInstallationAppliedReport : public ReportEvent {
 public:
  EcuInstallationAppliedReport(const Uptane::EcuSerial& ecu, const std::string& correlation_id);
};

class EcuInstallationCompletedReport : public ReportEvent {
 public:
  EcuInstallationCompletedReport(const Uptane::EcuSerial& ecu, const std::string& correlation_id, bool success);
};

class ReportQueue {
 public:
  ReportQueue(const Config& config_in, std::shared_ptr<HttpInterface> http_client,
              std::shared_ptr<INvStorage> storage_in);
  ~ReportQueue();
  ReportQueue(const ReportQueue&) = delete;
  ReportQueue(ReportQueue&&) = delete;
  ReportQueue& operator=(const ReportQueue&) = delete;
  ReportQueue& operator=(ReportQueue&&) = delete;
  void run();
  void enqueue(std::unique_ptr<ReportEvent> event);

 private:
  void flushQueue();

  const Config& config;
  std::shared_ptr<HttpInterface> http;
  std::thread thread_;
  std::condition_variable cv_;
  std::mutex m_;
  std::queue<std::unique_ptr<ReportEvent>> report_queue_;
  bool shutdown_{false};
  std::shared_ptr<INvStorage> storage;
};

#endif  // REPORTQUEUE_H_
