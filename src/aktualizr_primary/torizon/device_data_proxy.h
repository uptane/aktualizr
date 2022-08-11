#ifndef DEVICE_DATA_PROXY_H_
#define DEVICE_DATA_PROXY_H_

#include <cstdint>
#include <future>
#include "libaktualizr/aktualizr.h"

class DeviceDataProxy {
 private:
  std::future<void> future;
  std::atomic<bool> running;
  std::atomic<bool> enabled;
  std::string status_message;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
  int cancel_pipe[2];
  uint16_t port;

  int ConnectionCreate() const;
  static int ConnectionSetNonblock(int socketfd);
  static void SendDeviceData(Aktualizr& aktualizr, std::string& str_data);
  void ReportStatus(Aktualizr& aktualizr, bool error);

  static std::string FindAndReplaceString(std::string str, const std::string& from, const std::string& to);

 public:
  DeviceDataProxy();
  void Initialize(uint16_t p);
  void Start(Aktualizr& aktualizr);
  void Stop(Aktualizr& aktualizr, bool error);
};

#endif  // DEVICE_DATA_PROXY_H_
