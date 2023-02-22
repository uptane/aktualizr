#include "device_data_proxy.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include "logging/logging.h"
#include "utilities/utils.h"

static const uint16_t default_port = 8850;

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
DeviceDataProxy::DeviceDataProxy(Aktualizr* aktualizr_in) {
  port = default_port;
  running = false;
  enabled = false;
  aktualizr = aktualizr_in;
}

DeviceDataProxy::~DeviceDataProxy() { Stop(false, true); }

void DeviceDataProxy::Initialize(uint16_t p) {
  LOG_INFO << "PROXY: initializing...";

  enabled = true;

  if (p > 0 && p <= 1023) {
    status_message = "invalid TCP port";
    throw std::runtime_error(status_message);
  } else if (p >= 1024) {
    port = p;
  }

  LOG_INFO << "PROXY: using TCP port " << port << ".";

  if (pipe(cancel_pipe) != 0) {
    status_message = "could not create pipe for thread synchronization";
    throw std::runtime_error(status_message);
  }
}

int DeviceDataProxy::ConnectionSetNonblock(int socketfd) {
  int flags;

  if ((flags = fcntl(socketfd, F_GETFL, 0)) == -1) {
    LOG_ERROR << "PROXY: error reading socket flags! [" << strerror(errno) << "]";
    return -1;
  }

  if (fcntl(socketfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    LOG_ERROR << "PROXY: error setting nonblock mode! [" << strerror(errno) << "]";
    return -1;
  }

  return 0;
}

int DeviceDataProxy::ConnectionCreate() const {
  int socketfd;

  if ((socketfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    LOG_ERROR << "PROXY: could not create socket! [" << strerror(errno) << "]";
    return -1;
  }

  int enable = 1;
  if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    LOG_ERROR << "PROXY: could not set SO_REUSEADDR option! [" << strerror(errno) << "]";
    return -1;
  }

  sockaddr_in sockaddr{};
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  sockaddr.sin_port = htons(port);

  if (bind(socketfd, reinterpret_cast<struct sockaddr*>(&sockaddr), sizeof(sockaddr)) < 0) {
    LOG_ERROR << "PROXY: failed to bind to TCP port! [" << strerror(errno) << "]";
    return -1;
  }

  if (ConnectionSetNonblock(socketfd) == -1) {
    return -1;
  }

  if (listen(socketfd, 32) < 0) {
    LOG_ERROR << "PROXY: failed to listen to TCP port! [" << strerror(errno) << "]";
    return -1;
  }

  return socketfd;
}

std::string DeviceDataProxy::FindAndReplaceString(std::string str, const std::string& from, const std::string& to) {
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
  return str;
}

void DeviceDataProxy::SendDeviceData(std::string& str_data) {
  if (!str_data.empty()) {
    LOG_INFO << "PROXY: sending device data to Torizon OTA.";
    str_data = "{" + FindAndReplaceString(str_data, "}\n{", ",") + "}";
    Json::Value json_data = Utils::parseJSON(str_data);
    LOG_TRACE << "PROXY: Sending Json formatted message:" << std::endl << json_data;
    aktualizr->SendDeviceData(json_data).get();
    str_data.clear();
  }
}

void DeviceDataProxy::ReportStatus(bool error) {
  std::string str_data;

  // start proxy info
  str_data += "\"proxy\": {";

  // proxy status
  if (error) {
    // NOLINTNEXTLINE(modernize-raw-string-literal)
    str_data += "\"status\": \"error\",";
  } else {
    // NOLINTNEXTLINE(modernize-raw-string-literal)
    str_data += "\"status\": \"stopped\",";
  }

  // proxy status message
  if (!status_message.empty()) {
    // NOLINTNEXTLINE(modernize-raw-string-literal)
    str_data += "\"message\": \"" + status_message + "\"";
  } else {
    // NOLINTNEXTLINE(modernize-raw-string-literal)
    str_data += "\"message\": \"status message not available\"";
  }

  // end proxy info
  str_data += "}";

  SendDeviceData(str_data);
}

void DeviceDataProxy::Start() {
  future = std::async(std::launch::async, [this]() {
    LOG_INFO << "PROXY: starting thread.";

    std::string device_buffered_data;
    struct epoll_event events {};
    int epoll_errors = 0;
    int listener_socket;
    int timeout = -1;

    if ((listener_socket = ConnectionCreate()) == -1) {
      status_message = "could not create connection";
      LOG_ERROR << "PROXY: " << status_message << "! Exiting...";
      ReportStatus(true);
      return;
    }

    int epfd = epoll_create(2);

    // set up file descriptor to cancel (stop) the thread
    struct epoll_event ev1 {};
    ev1.events = EPOLLIN | EPOLLPRI;
    ev1.data.fd = cancel_pipe[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, cancel_pipe[0], &ev1);

    // set up file descriptor to listen to TCP connections
    struct epoll_event ev2 {};
    ev2.events = EPOLLIN | EPOLLPRI;
    ev2.data.fd = listener_socket;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listener_socket, &ev2);

    LOG_INFO << "PROXY: listening to connections...";
    running = true;

    while (true) {
      const unsigned int TIMEOUT_DEFAULT = 3000;
      int ret;

      // wait for the following events:
      // 1. message in cancel_pipe to finish thread execution
      // 2. connection request in listener_socket
      // 3. timer expired (in case timeout>0)
      if ((ret = epoll_wait(epfd, &events, 1, timeout)) < 0) {
        LOG_ERROR << "PROXY: unexpected error when waiting for data!";
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (++epoll_errors >= 5) {
          status_message = "maximum epoll errors reached";
          LOG_ERROR << "PROXY: " << status_message << ". Exiting thread!";
          ReportStatus(true);
          break;
        }
      }

      // timer expired, send data (if available) to Torizon OTA
      else if (ret == 0) {
        SendDeviceData(device_buffered_data);
        timeout = -1;
      }

      // cancel thread execution
      else if (events.data.fd == cancel_pipe[0]) {
        LOG_INFO << "PROXY: command received to stop execution.";
        break;
      }

      // connection from TCP socket
      else if (events.data.fd == listener_socket) {
        int connection_socket = accept(listener_socket, nullptr, nullptr);
        LOG_DEBUG << "PROXY: receiving connection from client. fd=" << connection_socket;
        if (connection_socket >= 0) {
          // set up file descriptor to listen to client connection
          struct epoll_event evconn {};
          evconn.events = EPOLLIN | EPOLLPRI;
          evconn.data.fd = connection_socket;
          epoll_ctl(epfd, EPOLL_CTL_ADD, connection_socket, &evconn);
          ConnectionSetNonblock(connection_socket);
        }
      }

      // receiving data from client
      else if ((events.events & EPOLLIN) != 0u) {
        const unsigned int MAX_BUF_LENGTH = 4096;
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
        char buffer[MAX_BUF_LENGTH];
        std::string str_data;
        ssize_t bytesReceived;

        LOG_DEBUG << "PROXY: receiving data from client. fd=" << events.data.fd;

        // receive data
        do {
          bytesReceived = recv(events.data.fd, buffer, MAX_BUF_LENGTH, 0);

          if (bytesReceived > 0) {
            str_data += std::string(&buffer[0], static_cast<std::string::size_type>(bytesReceived));
          }

        } while (bytesReceived == MAX_BUF_LENGTH);

        if (!str_data.empty()) {
          LOG_TRACE << "PROXY: Data received."
                    << " SIZE=" << str_data.size() << " DATA=" << str_data;

          // received data should be inside brackets -> { ... }
          if (str_data.at(0) == '{' && str_data.at(str_data.size() - 2) == '}') {
            // discard brackets from root node
            str_data = str_data.substr(1, str_data.size() - 3);

            // add comma to separate entries
            if (!device_buffered_data.empty()) {
              device_buffered_data += ",";
            }

            // concatenate new entry
            device_buffered_data += str_data;

            // set timeout to wait for more data before sending to Torizon OTA
            timeout = TIMEOUT_DEFAULT;
          } else {
            LOG_ERROR << "PROXY: received data not in the expected format! Discarding...";
          }
        }

        // client disconnected
        else if (bytesReceived == 0) {
          LOG_DEBUG << "PROXY: client disconnected! fd=" << events.data.fd;
          epoll_ctl(epfd, EPOLL_CTL_DEL, events.data.fd, nullptr);
          close(events.data.fd);
        }
      }

      // event unknown (should never reach here)
      else {
        LOG_ERROR << "PROXY: invalid file descriptor event! [" << events.data.fd << events.events << "]";
      }
    }

    LOG_INFO << "PROXY: stopping thread.";

    close(epfd);
    close(listener_socket);
    running = false;
  });
}

void DeviceDataProxy::Stop(bool error, bool hard_stop) {
  std::lock_guard<std::mutex> lock(stop_mutex);
  if (enabled) {
    if (running) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
      write(cancel_pipe[1], "stop", 4);
#pragma GCC diagnostic pop
      future.get();
    }
    if (!error) {
      status_message = "execution stopped by the user";
    }
    if (!hard_stop) {
      ReportStatus(error);
    }
  }
}
