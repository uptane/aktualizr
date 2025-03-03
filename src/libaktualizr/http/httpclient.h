#ifndef HTTPCLIENT_H_
#define HTTPCLIENT_H_

#include <future>
#include <memory>

#include <curl/curl.h>
#include "gtest/gtest_prod.h"
#include "json/json.h"

#include "httpinterface.h"

/**
 * Helper class to manage curl_global_init/curl_global_cleanup calls
 */
class CurlGlobalInitWrapper {
 public:
  CurlGlobalInitWrapper() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobalInitWrapper() { curl_global_cleanup(); }
  CurlGlobalInitWrapper(const CurlGlobalInitWrapper &) = delete;
  CurlGlobalInitWrapper(CurlGlobalInitWrapper &&) = delete;
  CurlGlobalInitWrapper &operator=(const CurlGlobalInitWrapper &) = delete;
  CurlGlobalInitWrapper &operator=(CurlGlobalInitWrapper &&) = delete;
};

class HttpClient : public HttpInterface {
 public:
  explicit HttpClient(const std::vector<std::string> *extra_headers = nullptr);
  explicit HttpClient(const std::string &socket);
  HttpClient(const HttpClient &curl_in);  // non-default!
  ~HttpClient() override;
  HttpClient(HttpClient &&) = default;
  HttpClient &operator=(const HttpClient &) = delete;
  HttpClient &operator=(HttpClient &&) = default;
  HttpResponse get(const std::string &url, int64_t maxsize, const api::FlowControlToken *flow_control) override;
  HttpResponse post(const std::string &url, const std::string &content_type, const std::string &data) override;
  HttpResponse post(const std::string &url, const Json::Value &data) override;
  HttpResponse put(const std::string &url, const std::string &content_type, const std::string &data) override;
  HttpResponse put(const std::string &url, const Json::Value &data) override;

  HttpResponse download(const std::string &url, curl_write_callback write_cb, curl_xferinfo_callback progress_cb,
                        void *userp, curl_off_t from) override;
  std::future<HttpResponse> downloadAsync(const std::string &url, curl_write_callback write_cb,
                                          curl_xferinfo_callback progress_cb, void *userp, curl_off_t from,
                                          CurlHandler *easyp) override;
  void setCerts(const std::string &ca, CryptoSource ca_source, const std::string &cert, CryptoSource cert_source,
                const std::string &pkey, CryptoSource pkey_source) override;
  bool updateHeader(const std::string &name, const std::string &value);
  void timeout(int64_t ms);

 private:
  FRIEND_TEST(HttpClient, DownloadSpeedLimit);

  static const CurlGlobalInitWrapper manageCurlGlobalInit_;
  CURL *curl;
  curl_slist *headers;
  HttpResponse perform(CURL *curl_handler, int retry_times, int64_t size_limit);
  static curl_slist *curl_slist_dup(curl_slist *sl);

  std::unique_ptr<TemporaryFile> tls_ca_file;
  std::unique_ptr<TemporaryFile> tls_cert_file;
  std::unique_ptr<TemporaryFile> tls_pkey_file;
  static const int RETRY_TIMES = 2;
  static const long kSpeedLimitTimeInterval = 60L;   // NOLINT(google-runtime-int)
  static const long kSpeedLimitBytesPerSec = 5000L;  // NOLINT(google-runtime-int)

  long speed_limit_time_interval_{kSpeedLimitTimeInterval};                // NOLINT(google-runtime-int)
  long speed_limit_bytes_per_sec_{kSpeedLimitBytesPerSec};                 // NOLINT(google-runtime-int)
  void overrideSpeedLimitParams(long time_interval, long bytes_per_sec) {  // NOLINT(google-runtime-int)
    speed_limit_time_interval_ = time_interval;
    speed_limit_bytes_per_sec_ = bytes_per_sec;
  }
  bool pkcs11_key{false};
  bool pkcs11_cert{false};
};
#endif
