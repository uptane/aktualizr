#include "httpfake.h"

#include <iostream>
#include <boost/filesystem.hpp>
#include <curl/curl.h>  // curl_easy_unescape
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <thread>
#include <chrono>

#include "logging/logging.h"

#include "metafake.h"

HttpFake::HttpFake(boost::filesystem::path test_dir_in, std::string flavor,
                  boost::filesystem::path meta_dir_in)
    : test_dir(std::move(test_dir_in)), flavor_(std::move(flavor)), meta_dir(std::move(meta_dir_in)) {
  if (meta_dir.empty()) {
    meta_dir = temp_meta_dir.Path();
    CreateFakeRepoMetaData(meta_dir);
  }
}

bool HttpFake::rewrite(std::string &url, const std::string &pattern) const {
  size_t pat_pos = url.find(pattern);
  if (pat_pos == std::string::npos) {
    return false;
  }
  size_t ext_pos = pattern.find(".json");
  if (ext_pos == std::string::npos) {
    LOG_ERROR << "Invalid pattern";
    url = "";
    return false;
  }

  url.replace(pat_pos + ext_pos, std::string(".json").size(), "_" + flavor_ + ".json");
  return true;
}

HttpResponse HttpFake::get(const std::string &url, int64_t maxsize,
                           const api::FlowControlToken *flow_control) {
    (void)maxsize;
    std::cout << "URL requested: " << url << "\n";

    if (flow_control != nullptr && flow_control->hasAborted()) {
      return HttpResponse("", 0, CURLE_ABORTED_BY_CALLBACK, "Canceled by FlowControlToken");
    }

    std::string new_url = url;
    if (!flavor_.empty()) {
      rewrite(new_url, "director/targets.json") || rewrite(new_url, "repo/timestamp.json") ||
          rewrite(new_url, "repo/targets.json") || rewrite(new_url, "snapshot.json");

      if (new_url != url) {
        std::cout << "Rewritten to: " << new_url << "\n";
      }
    }

    const boost::filesystem::path path = meta_dir / new_url.substr(tls_server.size());

    std::cout << "file served: " << path << "\n";

    if (boost::filesystem::exists(path)) {
      return HttpResponse(Utils::readFile(path), 200, CURLE_OK, "");
    } else {
      std::cout << "not found: " << path << "\n";
      return HttpResponse({}, 404, CURLE_OK, "");
    }
  }


HttpResponse HttpFake::post(const std::string &url, const Json::Value &data) {
  if (url.find("/devices") != std::string::npos || url.find("/director/ecus") != std::string::npos || url.empty()) {
    Utils::writeFile((test_dir / "post.json").string(), data);
    return HttpResponse(Utils::readFile("tests/test_data/cred.p12"), 200, CURLE_OK, "");
  } else if (url.find("/events") != std::string::npos) {
    return handle_event(url, data);
  }
  return HttpResponse("", 400, CURLE_OK, "");
}

std::future<HttpResponse> HttpFake::downloadAsync(const std::string &url, curl_write_callback write_cb,
                                        curl_xferinfo_callback progress_cb, void *userp, curl_off_t from,
                                        CurlHandler *easyp) {
  (void)userp;
  (void)from;
  (void)easyp;
  (void)progress_cb;

  std::cout << "URL requested: " << url << "\n";
  std::string path_segment = url.substr(tls_server.size());

  int fn_length;
  char *fn = curl_easy_unescape(nullptr, path_segment.data(), static_cast<int>(path_segment.size()), &fn_length);
  if (fn == nullptr) {
    std::cout << "Could not decode url. Trying it un-decoded\n";
  } else {
    path_segment.clear();
    path_segment.append(fn, static_cast<size_t>(fn_length));
    curl_free(fn);
  }
  const boost::filesystem::path path = meta_dir / path_segment;
  std::cout << "file served: " << path << "\n";

  std::promise<HttpResponse> resp_promise;
  auto resp_future = resp_promise.get_future();
  std::thread(
      [path, write_cb, progress_cb, userp, url](std::promise<HttpResponse> promise) {
        if (!boost::filesystem::exists(path)) {
          std::cout << "File not found on disk!\n";
          promise.set_value(HttpResponse("", 404, CURLE_OK, ""));
          return;
        }
        const std::string content = Utils::readFile(path.string());
        for (unsigned int i = 0; i < content.size(); ++i) {
          write_cb(const_cast<char *>(&content[i]), 1, 1, userp);
          progress_cb(userp, 0, 0, 0, 0);
          if (url.find("downloads/repo/targets/primary_firmware.txt") != std::string::npos) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Simulate big file
          }
        }
        promise.set_value(HttpResponse(content, 200, CURLE_OK, ""));
      },
      std::move(resp_promise))
      .detach();

  return resp_future;
}
