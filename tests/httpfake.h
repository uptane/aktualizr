#ifndef HTTPFAKE_H_
#define HTTPFAKE_H_

#include <boost/filesystem/path.hpp>
#include <string>

#include "json/json.h"

#include "http/httpinterface.h"
#include "utilities/utils.h"

class HttpFake : public HttpInterface {
 public:
  // old style HttpFake with centralized multi repo and url rewriting
  explicit HttpFake(boost::filesystem::path test_dir_in, std::string flavor = "",
                    boost::filesystem::path meta_dir_in = "");

  ~HttpFake() override = default;

  void setCerts(const std::string &ca, CryptoSource ca_source, const std::string &cert, CryptoSource cert_source,
                const std::string &pkey, CryptoSource pkey_source) override {
    (void)ca;
    (void)ca_source;
    (void)cert;
    (void)cert_source;
    (void)pkey;
    (void)pkey_source;
  }

  // rewrite xxx/yyy.json to xxx/yyy_flavor.json
  bool rewrite(std::string &url, const std::string &pattern) const;

  virtual HttpResponse handle_event(const std::string &url, const Json::Value &data) {
    (void)url;
    (void)data;
    // do something in child instances
    return HttpResponse("", 400, CURLE_OK, "");
  }

  using HttpInterface::get;
  HttpResponse get(const std::string &url, int64_t maxsize, const api::FlowControlToken *flow_control) override;

  HttpResponse post(const std::string &url, const std::string &content_type, const std::string &data) override {
    (void)url;
    (void)content_type;
    (void)data;
    return HttpResponse({}, 200, CURLE_OK, "");
  }

  HttpResponse post(const std::string &url, const Json::Value &data) override;

  HttpResponse put(const std::string &url, const std::string &content_type, const std::string &data) override {
    (void)url;
    (void)content_type;
    (void)data;
    return HttpResponse({}, 200, CURLE_OK, "");
  }

  HttpResponse put(const std::string &url, const Json::Value &data) override {
    last_manifest = data;
    return HttpResponse(url, 200, CURLE_OK, "");
  }

  std::future<HttpResponse> downloadAsync(const std::string &url, curl_write_callback write_cb,
                                          curl_xferinfo_callback progress_cb, void *userp, curl_off_t from,
                                          CurlHandler *easyp) override;

  HttpResponse download(const std::string &url, curl_write_callback write_cb, curl_xferinfo_callback progress_cb,
                        void *userp, curl_off_t from) override {
    return downloadAsync(url, write_cb, progress_cb, userp, from, nullptr).get();
  }

  const std::string tls_server = "https://tlsserver.com";
  Json::Value last_manifest;

 protected:
  boost::filesystem::path test_dir;
  std::string flavor_;
  boost::filesystem::path meta_dir;
  TemporaryDirectory temp_meta_dir;
};

#endif  // HTTPFAKE_H_
