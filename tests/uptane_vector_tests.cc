#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "http/httpclient.h"
#include "libaktualizr/config.h"
#include "logging/logging.h"
#include "primary/sotauptaneclient.h"
#include "storage/invstorage.h"
#include "utilities/utils.h"

using std::string;

string address;     // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
string tests_path;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

class VectorWrapper {
 public:
  explicit VectorWrapper(Json::Value vector) : vector_(std::move(vector)) {}

  bool matchError(const Uptane::Exception& e) {
    auto me = [this, &e](const string r) {
      if (vector_[r]["update"]["err_msg"].asString() == e.what()) {
        return true;
      }
      const Json::Value& targets = vector_[r]["targets"];
      for (Json::Value::const_iterator it = targets.begin(); it != targets.end(); it++) {
        if ((*it)["err_msg"].asString() == e.what()) {
          return true;
        }
      }
      return false;
    };
    if (me("director") || me("image_repo")) {
      return true;
    }
    std::cout << "aktualizr failed with unmatched exception " << typeid(e).name() << ": " << e.what() << "\n";
    std::cout << "Expected error: " << vector_ << "\n";
    return false;
  }

  bool shouldFail() {
    bool should_fail = false;
    if (!vector_["director"]["update"]["is_success"].asBool() ||
        !vector_["image_repo"]["update"]["is_success"].asBool()) {
      should_fail = true;
    } else {
      for (const auto& t : vector_["director"]["targets"]) {
        if (!t["is_success"].asBool()) {
          should_fail = true;
          break;
        }
      }
      for (const auto& t : vector_["image_repo"]["targets"]) {
        if (!t["is_success"].asBool()) {
          should_fail = true;
          break;
        }
      }
    }
    return should_fail;
  }

  void printExpectedFailure() {
    std::cout << "No exceptions occurred, but expected ";
    if (!vector_["director"]["update"]["is_success"].asBool()) {
      std::cout << "exception from director: '" << vector_["director"]["update"]["err"]
                << " with message: " << vector_["director"]["update"]["err_msg"] << "\n";
    } else if (!vector_["image_repo"]["update"]["is_success"].asBool()) {
      std::cout << "exception from image_repo: '" << vector_["image_repo"]["update"]["err"]
                << " with message: " << vector_["image_repo"]["update"]["err_msg"] << "\n";
    } else {
      std::cout << "an exception while fetching Targets metadata.\n";
    }
  }

 private:
  Json::Value vector_;
};

class UptaneVector : public ::testing::TestWithParam<string> {};

class HttpWrapper : public HttpClient {
 public:
  HttpResponse post(const string& url, const string& content_type, const string& data) override {
    if (url.find("/devices") != string::npos) {
      LOG_TRACE << " HttpWrapper intercepting device registration";
      return {Utils::readFile(tests_path + "/test_data/cred.p12"), 200, CURLE_OK, ""};
    }

    if (url.find("/director/ecus") != string::npos) {
      LOG_TRACE << " HttpWrapper intercepting Uptane ECU registration";
      return {"", 200, CURLE_OK, ""};
    }

    LOG_TRACE << "HttpWrapper letting " << url << " pass";
    return HttpClient::post(url, content_type, data);
  }
  HttpResponse post(const string& url, const Json::Value& data) override { return HttpClient::post(url, data); }
};

/**
 * Check that aktualizr fails on expired metadata.
 * RecordProperty("zephyr_key", "REQ-150,TST-49");
 * Check that aktualizr fails on bad threshold.
 * RecordProperty("zephyr_key", "REQ-153,TST-52");
 */
TEST_P(UptaneVector, Test) {
  const string test_name = GetParam();
  std::cout << "Running test vector " << test_name << "\n";

  TemporaryDirectory temp_dir;
  Config config;
  config.provision.primary_ecu_serial = "test_primary_ecu_serial";
  config.provision.primary_ecu_hardware_id = "test_primary_hardware_id";
  config.provision.provision_path = tests_path + "/test_data/cred.zip";
  config.provision.mode = ProvisionMode::kSharedCredReuse;
  config.uptane.director_server = address + test_name + "/director";
  config.uptane.repo_server = address + test_name + "/image_repo";
  config.storage.path = temp_dir.Path();
  config.storage.uptane_metadata_path = utils::BasedPath(temp_dir.Path() / "metadata");
  config.pacman.images_path = temp_dir.Path() / "images";
  config.pacman.type = PACKAGE_MANAGER_NONE;
  config.postUpdateValues();
  logger_set_threshold(boost::log::trivial::trace);

  auto storage = INvStorage::newStorage(config.storage);
  auto http_client = std::make_shared<HttpWrapper>();
  auto uptane_client = std_::make_unique<SotaUptaneClient>(config, storage, http_client, nullptr);
  auto ecu_serial = uptane_client->provisioner_.PrimaryEcuSerial();
  auto hw_id = uptane_client->provisioner_.PrimaryHardwareIdentifier();
  EXPECT_EQ(ecu_serial.ToString(), config.provision.primary_ecu_serial);
  EXPECT_EQ(hw_id.ToString(), config.provision.primary_ecu_hardware_id);
  Uptane::EcuMap ecu_map{{ecu_serial, hw_id}};
  Uptane::Target target("test_filename", ecu_map, {{Hash::Type::kSha256, "sha256"}}, 1, "");
  storage->saveInstalledVersion(ecu_serial.ToString(), target, InstalledVersionUpdateMode::kCurrent);

  uptane_client->initialize();
  ASSERT_TRUE(uptane_client->attemptProvision()) << "Provisioning Failed. Can't continue test";
  while (true) {
    HttpResponse response = http_client->post(address + test_name + "/step", Json::Value());
    if (response.http_status_code == 204) {
      return;
    }
    const auto vector_json(response.getJson());
    std::cout << "VECTOR: " << vector_json;
    VectorWrapper vector(vector_json);

    bool should_fail = vector.shouldFail();

    try {
      /* Fetch metadata from the Director.
       * Check metadata from the Director.
       * Identify targets for known ECUs.
       * Fetch metadata from the Image repo.
       * Check metadata from the Image repo.
       *
       * It would be simpler to just call fetchMeta() here, but that calls
       * putManifestSimple(), which will fail here. */
      uptane_client->uptaneIteration(nullptr, nullptr);

      result::UpdateCheck updates = uptane_client->checkUpdates();
      if (updates.status == result::UpdateStatus::kError) {
        ASSERT_TRUE(should_fail) << "checkUpdates unexpectedly failed.";
        if (uptane_client->getLastException() != nullptr) {
          std::rethrow_exception(uptane_client->getLastException());
        }
      }
      if (updates.ecus_count > 0) {
        /* Download a binary package.
         * Verify a binary package. */
        result::Download result = uptane_client->downloadImages(updates.updates);
        if (result.status != result::DownloadStatus::kSuccess) {
          ASSERT_TRUE(should_fail) << "downloadImages unexpectedly failed.";
          if (uptane_client->getLastException() != nullptr) {
            std::rethrow_exception(uptane_client->getLastException());
          }
        }
      }

    } catch (const Uptane::Exception& e) {
      ASSERT_TRUE(vector.matchError(e)) << "libaktualizr threw a different exception than expected!";
      continue;
    } catch (const std::exception& e) {
      FAIL() << "libaktualizr failed with unrecognized exception " << typeid(e).name() << ": " << e.what();
    }

    if (should_fail) {
      vector.printExpectedFailure();
      FAIL();
    }
  }
  FAIL() << "Step sequence unexpectedly aborted.";
}

std::vector<string> GetVectors() {
  HttpClient http_client;
  const Json::Value json_vectors = http_client.get(address, HttpInterface::kNoLimit).getJson();
  std::vector<string> vectors;
  for (Json::ValueConstIterator it = json_vectors.begin(); it != json_vectors.end(); it++) {
    vectors.emplace_back((*it).asString());
  }
  return vectors;
}

INSTANTIATE_TEST_SUITE_P(UptaneVectorSuite, UptaneVector, ::testing::ValuesIn(GetVectors()));

int main(int argc, char* argv[]) {
  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  if (argc < 3) {
    std::cerr << "This program is intended to be run from run_vector_tests.sh!\n";
    return 1;
  }

  /* Use ports to distinguish both the server connection and local storage so
   * that parallel runs of this code don't cause problems that are difficult to
   * debug. */
  const string port = argv[1];
  address = "http://localhost:" + port + "/";

  tests_path = argv[2];

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
