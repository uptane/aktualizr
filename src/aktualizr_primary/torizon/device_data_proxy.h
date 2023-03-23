#ifndef DEVICE_DATA_PROXY_H_
#define DEVICE_DATA_PROXY_H_

#include <cstdint>
#include <future>
#include <mutex>
#include "libaktualizr/aktualizr.h"

class DeviceDataProxy {
 private:
  std::future<void> future;
  std::atomic<bool> running;
  std::atomic<bool> enabled;
  std::string status_message;
  std::mutex stop_mutex;
  Aktualizr* aktualizr;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
  int cancel_pipe[2];
  uint16_t port;

  int ConnectionCreate() const;
  static int ConnectionSetNonblock(int socketfd);
  void SendDeviceData(std::string& str_data);
  void ReportStatus(bool error);

  static std::string FindAndReplaceString(std::string str, const std::string& from, const std::string& to);

 public:
  explicit DeviceDataProxy(Aktualizr* aktualizr_in);
  DeviceDataProxy(const DeviceDataProxy&) = delete;
  DeviceDataProxy(DeviceDataProxy&&) = delete;
  ~DeviceDataProxy();

  DeviceDataProxy& operator=(const DeviceDataProxy&) = delete;
  DeviceDataProxy& operator=(DeviceDataProxy&&) = delete;

  void Initialize(uint16_t p);
  void Start();
  void Stop(bool error, bool hard_stop = false);
};

#endif  // DEVICE_DATA_PROXY_H_
