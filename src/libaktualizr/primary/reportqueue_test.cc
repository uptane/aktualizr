#include <gtest/gtest.h>

#include <unistd.h>
#include <future>
#include <memory>
#include <string>

#include <json/json.h>

#include "httpfake.h"
#include "libaktualizr/config.h"
#include "reportqueue.h"
#include "storage/invstorage.h"
#include "storage/sqlstorage.h"
#include "utilities/utils.h"

class HttpFakeRq : public HttpFake {
 public:
  HttpFakeRq(const boost::filesystem::path &test_dir_in, size_t expected_events, int event_numb_limit = -1)
      : HttpFake(test_dir_in, ""),
        expected_events_(expected_events),
        event_numb_limit_{event_numb_limit},
        last_request_expected_events_{expected_events_} {
    if (event_numb_limit_ > 0) {
      last_request_expected_events_ = expected_events_ % static_cast<uint>(event_numb_limit_);
      if (last_request_expected_events_ == 0) {
        last_request_expected_events_ = static_cast<uint>(event_numb_limit_);
      }
    }
  }

  HttpResponse handle_event(const std::string &url, const Json::Value &data) override {
    (void)data;
    if (url == "reportqueue/SingleEvent/events") {
      EXPECT_EQ(data[0]["eventType"]["id"], "EcuDownloadCompleted");
      EXPECT_EQ(data[0]["event"]["ecu"], "SingleEvent");
      ++events_seen;
      if (events_seen == expected_events_) {
        expected_events_received.set_value(true);
      }
      return HttpResponse("", 200, CURLE_OK, "");
    } else if (url.find("reportqueue/MultipleEvents") == 0) {
      for (int i = 0; i < static_cast<int>(data.size()); ++i) {
        EXPECT_EQ(data[i]["eventType"]["id"], "EcuDownloadCompleted");
        EXPECT_EQ(data[i]["event"]["ecu"], "MultipleEvents" + std::to_string(events_seen++));
      }
      if (events_seen == expected_events_) {
        expected_events_received.set_value(true);
      }
      return HttpResponse("", 200, CURLE_OK, "");
    } else if (url.find("reportqueue/FailureRecovery") == 0) {
      if (data.size() < 10) {
        return HttpResponse("", 400, CURLE_OK, "");
      } else {
        for (int i = 0; i < static_cast<int>(data.size()); ++i) {
          EXPECT_EQ(data[i]["eventType"]["id"], "EcuDownloadCompleted");
          EXPECT_EQ(data[i]["event"]["ecu"], "FailureRecovery" + std::to_string(i));
        }
        events_seen = data.size();
        if (events_seen == expected_events_) {
          expected_events_received.set_value(true);
        }
        return HttpResponse("", 200, CURLE_OK, "");
      }
    } else if (url.find("reportqueue/StoreEvents") == 0) {
      for (int i = 0; i < static_cast<int>(data.size()); ++i) {
        EXPECT_EQ(data[i]["eventType"]["id"], "EcuDownloadCompleted");
        EXPECT_EQ(data[i]["event"]["ecu"], "StoreEvents" + std::to_string(events_seen++));
      }
      if (events_seen == expected_events_) {
        expected_events_received.set_value(true);
      }
      return HttpResponse("", 200, CURLE_OK, "");
    } else if (url.find("reportqueue/EventNumberLimit") == 0) {
      const auto recv_event_numb{data.size()};
      EXPECT_GT(recv_event_numb, 0);
      events_seen += recv_event_numb;
      EXPECT_LE(events_seen, expected_events_);

      if (events_seen < expected_events_) {
        EXPECT_EQ(recv_event_numb, event_numb_limit_);
      } else {
        EXPECT_EQ(recv_event_numb, last_request_expected_events_);
        expected_events_received.set_value(true);
      }

      return HttpResponse("", 200, CURLE_OK, "");
    } else if (url.find("reportqueue/PayloadTooLarge") == 0) {
      EXPECT_GT(data.size(), 0);
      for (uint ii = 0; ii < data.size(); ++ii) {
        if (data[ii]["id"] == "413") {
          return HttpResponse("", 413, CURLE_OK, "Payload Too Large");
        }
        if (data[ii]["id"] == "500" && bad_gateway_counter_ < data[ii]["err_numb"].asInt()) {
          ++bad_gateway_counter_;
          return HttpResponse("", 500, CURLE_OK, "Bad Gateway");
        }
      }
      events_seen += data.size();
      if (events_seen == expected_events_) {
        expected_events_received.set_value(true);
      }
      return HttpResponse("", 200, CURLE_OK, "");
    }
    LOG_ERROR << "Unexpected event: " << data;
    return HttpResponse("", 400, CURLE_OK, "");
  }

  size_t events_seen{0};
  size_t expected_events_;
  std::promise<bool> expected_events_received{};
  int event_numb_limit_;
  size_t last_request_expected_events_;
  int bad_gateway_counter_{0};
};

/* Test one event. */
TEST(ReportQueue, SingleEvent) {
  TemporaryDirectory temp_dir;
  Config config;
  config.storage.path = temp_dir.Path();
  config.tls.server = "reportqueue/SingleEvent";

  size_t num_events = 1;
  auto http = std::make_shared<HttpFakeRq>(temp_dir.Path(), num_events);
  auto sql_storage = std::make_shared<SQLStorage>(config.storage, false);
  ReportQueue report_queue(config, http, sql_storage);

  report_queue.enqueue(std_::make_unique<EcuDownloadCompletedReport>(Uptane::EcuSerial("SingleEvent"), "", true));

  // Wait at most 30 seconds for the message to get processed.
  http->expected_events_received.get_future().wait_for(std::chrono::seconds(20));
  EXPECT_EQ(http->events_seen, num_events);
}

/* Test ten events. */
TEST(ReportQueue, MultipleEvents) {
  TemporaryDirectory temp_dir;
  Config config;
  config.storage.path = temp_dir.Path();
  config.tls.server = "reportqueue/MultipleEvents";

  size_t num_events = 10;
  auto http = std::make_shared<HttpFakeRq>(temp_dir.Path(), num_events);
  auto sql_storage = std::make_shared<SQLStorage>(config.storage, false);
  ReportQueue report_queue(config, http, sql_storage);

  for (int i = 0; i < 10; ++i) {
    report_queue.enqueue(std_::make_unique<EcuDownloadCompletedReport>(
        Uptane::EcuSerial("MultipleEvents" + std::to_string(i)), "", true));
  }

  // Wait at most 30 seconds for the messages to get processed.
  http->expected_events_received.get_future().wait_for(std::chrono::seconds(20));
  EXPECT_EQ(http->events_seen, num_events);
}

/* Test ten events, but the "server" returns an error the first nine times. The
 * tenth time should succeed with an array of all ten events. */
TEST(ReportQueue, FailureRecovery) {
  TemporaryDirectory temp_dir;
  Config config;
  config.storage.path = temp_dir.Path();
  config.tls.server = "reportqueue/FailureRecovery";

  size_t num_events = 10;
  auto http = std::make_shared<HttpFakeRq>(temp_dir.Path(), num_events);
  auto sql_storage = std::make_shared<SQLStorage>(config.storage, false);
  ReportQueue report_queue(config, http, sql_storage);

  for (size_t i = 0; i < num_events; ++i) {
    report_queue.enqueue(std_::make_unique<EcuDownloadCompletedReport>(
        Uptane::EcuSerial("FailureRecovery" + std::to_string(i)), "", true));
  }

  // Wait at most 20 seconds for the messages to get processed.
  http->expected_events_received.get_future().wait_for(std::chrono::seconds(20));
  EXPECT_EQ(http->events_seen, num_events);
}

/* Test persistent storage of unsent events in the database across
 * ReportQueue instantiations. */
TEST(ReportQueue, StoreEvents) {
  TemporaryDirectory temp_dir;
  Config config;
  config.storage.path = temp_dir.Path();
  config.tls.server = "";

  auto sql_storage = std::make_shared<SQLStorage>(config.storage, false);
  size_t num_events = 10;
  auto check_sql = [sql_storage](size_t count) {
    int64_t max_id = 0;
    Json::Value report_array{Json::arrayValue};
    sql_storage->loadReportEvents(&report_array, &max_id, -1);
    EXPECT_EQ(max_id, count);
  };

  {
    auto http = std::make_shared<HttpFakeRq>(temp_dir.Path(), num_events);
    ReportQueue report_queue(config, http, sql_storage);
    for (size_t i = 0; i < num_events; ++i) {
      report_queue.enqueue(std_::make_unique<EcuDownloadCompletedReport>(
          Uptane::EcuSerial("StoreEvents" + std::to_string(i)), "", true));
    }
    check_sql(num_events);
  }

  config.tls.server = "reportqueue/StoreEvents";
  auto http = std::make_shared<HttpFakeRq>(temp_dir.Path(), num_events);
  ReportQueue report_queue(config, http, sql_storage);
  // Wait at most 20 seconds for the messages to get processed.
  http->expected_events_received.get_future().wait_for(std::chrono::seconds(20));
  EXPECT_EQ(http->events_seen, num_events);
  sleep(1);
  check_sql(0);
}

TEST(ReportQueue, LimitEventNumber) {
  TemporaryDirectory temp_dir;
  Config config;
  config.storage.path = temp_dir.Path();
  config.tls.server = "";
  auto sql_storage = std::make_shared<SQLStorage>(config.storage, false);

  const std::vector<std::tuple<uint, int>> test_cases{
      {1, -1}, {1, 1}, {1, 2}, {10, -1}, {10, 1}, {10, 2}, {10, 3}, {10, 9}, {10, 10}, {10, 11},
  };
  for (const auto &tc : test_cases) {
    const auto event_numb{std::get<0>(tc)};
    const auto event_numb_limit{std::get<1>(tc)};

    Json::Value report_array{Json::arrayValue};
    for (uint ii = 0; ii < event_numb; ++ii) {
      sql_storage->saveReportEvent(Utils::parseJSON(R"({"id": "some ID", "eventType": "some Event"})"));
    }

    config.tls.server = "reportqueue/EventNumberLimit";
    auto http = std::make_shared<HttpFakeRq>(temp_dir.Path(), event_numb, event_numb_limit);
    ReportQueue report_queue(config, http, sql_storage, 0, event_numb_limit);
    // Wait at most 20 seconds for the messages to get processed.
    http->expected_events_received.get_future().wait_for(std::chrono::seconds(20));
    EXPECT_EQ(http->events_seen, event_numb);
  }
}

TEST(ReportQueue, PayloadTooLarge) {
  TemporaryDirectory temp_dir;
  Config config;
  config.storage.path = temp_dir.Path();
  config.tls.server = "";
  auto sql_storage = std::make_shared<SQLStorage>(config.storage, false);

  const std::vector<std::tuple<uint, int>> test_cases{
      {1, -1}, {1, 1}, {1, 2}, {13, -1}, {13, 1}, {13, 2}, {13, 3}, {13, 12}, {13, 13}, {13, 14},
  };
  for (const auto &tc : test_cases) {
    const auto valid_event_numb{std::get<0>(tc)};
    const auto event_numb_limit{std::get<1>(tc)};

    // inject "Too Big Event" at the beginning, middle, and the end of update event queues
    sql_storage->saveReportEvent(Utils::parseJSON(R"({"id": "413", "eventType": "some Event"})"));
    for (uint ii = 0; ii < valid_event_numb - 1; ++ii) {
      sql_storage->saveReportEvent(Utils::parseJSON(R"({"id": "some ID", "eventType": "some Event"})"));
      if (ii == valid_event_numb / 2) {
        sql_storage->saveReportEvent(Utils::parseJSON(R"({"id": "413", "eventType": "some Event"})"));
      }
    }
    // inject one "Bad Gateway" event, the server returns 500 twice and eventually it succeeds
    sql_storage->saveReportEvent(Utils::parseJSON(R"({"id": "500", "err_numb": 2})"));
    sql_storage->saveReportEvent(Utils::parseJSON(R"({"id": "413", "eventType": "some Event"})"));

    config.tls.server = "reportqueue/PayloadTooLarge";
    auto http = std::make_shared<HttpFakeRq>(temp_dir.Path(), valid_event_numb, event_numb_limit);
    ReportQueue report_queue(config, http, sql_storage, 0, event_numb_limit);
    http->expected_events_received.get_future().wait_for(std::chrono::seconds(20));
    EXPECT_EQ(http->events_seen, valid_event_numb);
  }
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  logger_set_threshold(boost::log::trivial::trace);
  return RUN_ALL_TESTS();
}
#endif
