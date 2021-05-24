#ifndef DEVICE_DATA_PROXY_H_
#define DEVICE_DATA_PROXY_H_

#include <cstdint>
#include <future>
#include "libaktualizr/aktualizr.h"

class DeviceDataProxy {

  std::future<void> future;
  std::atomic<bool> running;
  std::atomic<bool> enabled;
  std::string status_message;
  int cancel_pipe[2];
  uint16_t port;

  int  ConnectionCreate(void);
  int  ConnectionSetNonblock(int socketfd);
  void SendDeviceData(Aktualizr& aktualizr, std::string& str_data);
  void ReportStatus(Aktualizr& aktualizr, bool error);

  std::string FindAndReplaceString(std::string str, const std::string& from,
                                   const std::string& to);

 public:
  DeviceDataProxy();
  void Initialize(const uint16_t p);
  void Start(Aktualizr& aktualizr);
  void Stop(Aktualizr& aktualizr, bool error);

};

#endif  // DEVICE_DATA_PROXY_H_
