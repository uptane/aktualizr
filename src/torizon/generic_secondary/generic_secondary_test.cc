#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>
#include <cstdlib>

#include "torizongenericsecondary.h"
#include "uptane_test_common.h"
#include "utilities/utils.h"

int setenv(const std::string& name, const std::string& value) { return setenv(name.c_str(), value.c_str(), 1); }

std::string getSha256Sum(boost::filesystem::path file) {
  // See https://www.boost.org/doc/libs/1_65_0/doc/html/boost_process/tutorial.html
  std::string sha256sum;
  boost::process::ipstream is;
  boost::process::child chld(boost::process::search_path("sha256sum"), file, boost::process::std_out > is);
  std::string line, hash;
  while (chld.running() && std::getline(is, line) && !line.empty()) {
    std::vector<std::string> parts;
    boost::algorithm::trim(line);
    boost::algorithm::split(parts, line, boost::is_any_of(" \t"));
    if (parts.size() >= 2) {
      sha256sum = boost::algorithm::to_lower_copy(parts[0]);
    }
  }
  chld.wait();
  return sha256sum;
}

void touch(boost::filesystem::path fpath) { std::ofstream output(fpath.string()); }

std::shared_ptr<Primary::TorizonGenericSecondaryConfig> makeTestConfig(const TemporaryDirectory& temp_dir,
                                                                       boost::filesystem::path action_handler_path) {
  auto config = std::make_shared<Primary::TorizonGenericSecondaryConfig>();
  config->partial_verifying = false;
  config->full_client_dir = temp_dir.Path();
  config->ecu_serial = "12345678";
  config->ecu_hardware_id = "secondary_hardware";
  config->ecu_private_key = "sec.priv";
  config->ecu_public_key = "sec.pub";
  config->firmware_path = temp_dir.Path() / "firmware.bin";
  config->target_name_path = temp_dir.Path() / "firmware_name.txt";
  config->metadata_path = temp_dir.Path() / "metadata";
  config->action_handler_path = std::move(action_handler_path);
  return config;
}

namespace Primary {
// These tests must be inside the "Primary" namespace because they are testing a private method.

class TorizonGenericSecondaryTest : public ::testing::Test {
 protected:
  TorizonGenericSecondaryTest() {
    temp_dir_ = std::make_shared<TemporaryDirectory>();

    config_.pacman.type = PACKAGE_MANAGER_NONE;
    config_.pacman.images_path = temp_dir_->Path() / "images";
    config_.storage.path = temp_dir_->Path();

    storage_ = INvStorage::newStorage(config_.storage);
    storage_->storeTlsCreds(ca_, cert_, pkey_);
    storage_->storeRoot(director_root_, Uptane::RepositoryType::Director(), Uptane::Version(1));
    storage_->storeNonRoot(director_targets_, Uptane::RepositoryType::Director(), Uptane::Role::Targets());
    storage_->storeRoot(image_root_, Uptane::RepositoryType::Image(), Uptane::Version(1));
    storage_->storeNonRoot(image_timestamp_, Uptane::RepositoryType::Image(), Uptane::Role::Timestamp());
    storage_->storeNonRoot(image_snapshot_, Uptane::RepositoryType::Image(), Uptane::Role::Snapshot());
    storage_->storeNonRoot(image_targets_, Uptane::RepositoryType::Image(), Uptane::Role::Targets());

    package_manager_ = PackageManagerFactory::makePackageManager(config_.pacman, config_.bootloader, storage_, nullptr);
    secondary_provider_ = SecondaryProviderBuilder::Build(config_, storage_, package_manager_);
  }

  void TearDown() override { temp_dir_.reset(); }

  void makeSecondary(boost::filesystem::path handler_rel_path) {
    auto handler_path = boost::filesystem::current_path() / handler_rel_path;
    sconfig_ = makeTestConfig(*temp_dir_, handler_path);
    secondary_ = std::make_shared<Primary::TorizonGenericSecondary>(*sconfig_);
    secondary_->init(secondary_provider_);
  }

  std::shared_ptr<TemporaryDirectory> temp_dir_;
  std::shared_ptr<Primary::TorizonGenericSecondaryConfig> sconfig_;
  std::shared_ptr<Primary::TorizonGenericSecondary> secondary_;

  Config config_;
  std::shared_ptr<INvStorage> storage_;
  std::shared_ptr<SecondaryProvider> secondary_provider_;
  std::shared_ptr<PackageManagerInterface> package_manager_;

  const std::string ca_{"ca"};
  const std::string cert_{"cert"};
  const std::string pkey_{"pkey"};
  const std::string director_root_{"director-root"};
  const std::string director_root_v2_{"director-root-v2"};
  const std::string director_targets_{"director-targets"};
  const std::string image_root_{"image-root"};
  const std::string image_root_v2_{"image-root-v2"};
  const std::string image_timestamp_{"image-timestamp"};
  const std::string image_snapshot_{"image-snapshot"};
  const std::string image_targets_{"image-targets"};
};

TEST_F(TorizonGenericSecondaryTest, NonExistingHandler) {
  logger_set_threshold(boost::log::trivial::trace);

  TorizonGenericSecondary::ActionHandlerResult handler_result;
  const TorizonGenericSecondary::VarMap vars = {
      {"SECONDARY_COLOR", "BLUE"},
      {"SECONDARY_SIZE", "SMALL"},
  };

  makeSecondary("tests/torizon/non_existing_action.sh");

  // Non-existing action-handler.
  LOG_DEBUG << "Running a non-existing action handler";
  handler_result = secondary_->callActionHandler("dummy-action", vars);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::NotAvailable);
}

TEST_F(TorizonGenericSecondaryTest, HandlerFinishedBySignal) {
  logger_set_threshold(boost::log::trivial::trace);

  TorizonGenericSecondary::ActionHandlerResult handler_result;
  const TorizonGenericSecondary::VarMap vars = {
      {"SECONDARY_COLOR", "BLUE"},
      {"SECONDARY_SIZE", "SMALL"},
  };

  makeSecondary("tests/torizon/test_action_handler.sh");

  LOG_DEBUG << "Running an action-handler terminated by signal TERM";
  handler_result = secondary_->callActionHandler("terminate-with-signal-TERM", vars);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::NotAvailable);

  LOG_DEBUG << "Running an action-handler terminated by signal KILL";
  handler_result = secondary_->callActionHandler("terminate-with-signal-KILL", vars);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::NotAvailable);
}

TEST_F(TorizonGenericSecondaryTest, NoHandlerOutputExpected) {
  logger_set_threshold(boost::log::trivial::trace);

  TorizonGenericSecondary::ActionHandlerResult handler_result;
  const TorizonGenericSecondary::VarMap vars = {
      {"SECONDARY_COLOR", "BLUE"},
      {"SECONDARY_SIZE", "SMALL"},
  };

  makeSecondary("tests/torizon/test_action_handler.sh");

  LOG_DEBUG << "Running an action-handler finished with code 67 (unknown)";
  handler_result = secondary_->callActionHandler("exit-with-code-67", vars);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::ReqErrorProc);

  LOG_DEBUG << "Running an action-handler finished with code 66 (RFU)";
  handler_result = secondary_->callActionHandler("exit-with-code-66", vars);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::ReqErrorProc);

  LOG_DEBUG << "Running an action-handler finished with code 65 (request error processing)";
  handler_result = secondary_->callActionHandler("exit-with-code-65", vars);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::ReqErrorProc);

  LOG_DEBUG << "Running an action-handler finished with code 64 (request normal processing)";
  handler_result = secondary_->callActionHandler("exit-with-code-64", vars);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::ReqNormalProc);
}

TEST_F(TorizonGenericSecondaryTest, HandlerOutputExpected) {
  logger_set_threshold(boost::log::trivial::trace);

  TorizonGenericSecondary::ActionHandlerResult handler_result;
  const TorizonGenericSecondary::VarMap vars = {
      {"SECONDARY_COLOR", "BLUE"},
      {"SECONDARY_SIZE", "SMALL"},
      {"TEST_JSON_OUTPUT", "{\"status\": \"ok\"}"},
  };
  const Primary::TorizonGenericSecondary::VarMap varsBadJson = {
      {"SECONDARY_COLOR", "BLUE"},
      {"SECONDARY_SIZE", "SMALL"},
      {"TEST_JSON_OUTPUT", "{\"value\":}"},
  };

  makeSecondary("tests/torizon/test_action_handler.sh");

  LOG_DEBUG << "Running an action-handler returning exit code 0; no output";
  handler_result = secondary_->callActionHandler("exit-without-json-output-code-0", vars);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::ProcNoOutput);

  LOG_DEBUG << "Running an action-handler returning exit code 0; bad JSON output";
  handler_result = secondary_->callActionHandler("exit-with-json-output-code-0", varsBadJson);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::ProcNoOutput);

  Json::Value jsonOutput;
  LOG_DEBUG << "Running an action-handler returning exit code 0; good JSON output";
  handler_result = secondary_->callActionHandler("exit-with-json-output-code-0", vars, &jsonOutput);
  EXPECT_EQ(handler_result, TorizonGenericSecondary::ActionHandlerResult::ProcOutput);
  EXPECT_EQ(jsonOutput["status"], "ok");
}

TEST_F(TorizonGenericSecondaryTest, GetFirmwareInfoFailure) {
  logger_set_threshold(boost::log::trivial::trace);
  makeSecondary("tests/torizon/test_get_fwinfo.sh");

  LOG_DEBUG << "getFirmwareInfo: action-handler ends due to signal";
  {
    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "terminate-with-signal-TERM");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), false);
  }

  LOG_DEBUG << "getFirmwareInfo: action-handler produces bad output";
  {
    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"value\":}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), false);
  }

  LOG_DEBUG << "getFirmwareInfo: action-handler requests error processing";
  {
    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-65");
    setenv("TEST_JSON_OUTPUT", "{\"value\":\"test\"}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), false);
  }

  LOG_DEBUG << "getFirmwareInfo: action-handler outputs failure status";
  {
    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\":\"failed\"}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), false);
  }

  LOG_DEBUG << "getFirmwareInfo: action-handler outputs bad status";
  {
    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\":\"weird-status\"}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), false);
  }
}

TEST_F(TorizonGenericSecondaryTest, GetFirmwareInfoSuccess) {
  logger_set_threshold(boost::log::trivial::trace);
  makeSecondary("tests/torizon/test_get_fwinfo.sh");

  LOG_DEBUG << "getFirmwareInfo: action-handler provides minimal information - no firmware file";
  {
    // In this test case no firmware file/name is present in the temporary test directory.
    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\": \"ok\", \"message\": \"user message\"}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), true);
    EXPECT_EQ(firmware_info.name, "noimage");
    // Following is the SHA-256 of an empty file.
    EXPECT_EQ(firmware_info.hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(firmware_info.len, 0);
  }

  LOG_DEBUG << "getFirmwareInfo: action-handler provides minimal information - empty firmware file";
  {
    const std::string tgtname{"my-firmware.bin_1.0"};

    // Create a zero-sized firmware binary.
    {
      std::ofstream fwfile(sconfig_->firmware_path.c_str(), std::ios::out | std::ios::binary);
      std::ofstream tgtname_file(sconfig_->target_name_path.c_str());
      tgtname_file << tgtname;
    }

    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\": \"ok\", \"message\": \"user message\"}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), true);
    EXPECT_EQ(firmware_info.name, tgtname);
    // Following is the SHA-256 of an empty file.
    EXPECT_EQ(firmware_info.hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(firmware_info.len, 0);
  }

  LOG_DEBUG << "getFirmwareInfo: action-handler provides minimal information - random firmware file";
  {
    const std::string tgtname{"my-firmware.bin_2.0"};
    const size_t fwsize = 1048583;  // Using a prime number.

    // Create a firmware binary with random data.
    {
      std::ofstream fwfile(sconfig_->firmware_path.c_str(), std::ios::out | std::ios::binary);
      for (size_t index = 0; index < fwsize; index++) {
        fwfile.put(static_cast<char>(std::rand() % 256));
      }
      std::ofstream tgtname_file(sconfig_->target_name_path.c_str());
      tgtname_file << tgtname;
    }

    // Get expected hash by running well-known program.
    const std::string expected_hash = getSha256Sum(sconfig_->firmware_path);

    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\": \"ok\", \"message\": \"user message\"}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), true);
    EXPECT_EQ(firmware_info.name, tgtname);
    EXPECT_EQ(firmware_info.hash, expected_hash);
    EXPECT_EQ(firmware_info.len, fwsize);
  }

  LOG_DEBUG << "getFirmwareInfo: action-handler provides hash only";
  {
    const std::string tgtname{"my-firmware.bin_3.0"};
    const size_t fwsize = 1048583;  // Using a prime number.

    // Create a firmware binary with random data.
    {
      std::ofstream fwfile(sconfig_->firmware_path.c_str(), std::ios::out | std::ios::binary);
      for (size_t index = 0; index < fwsize; index++) {
        fwfile.put(static_cast<char>(std::rand() % 256));
      }
      std::ofstream tgtname_file(sconfig_->target_name_path.c_str());
      tgtname_file << tgtname;
    }

    // Get expected hash by running well-known program.
    const std::string expected_hash = getSha256Sum(sconfig_->firmware_path);

    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT",
           "{\"status\": \"ok\", \"message\": \"user message\", "
           "\"sha256\": \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), true);
    EXPECT_EQ(firmware_info.name, tgtname);
    EXPECT_EQ(firmware_info.hash, expected_hash);
    EXPECT_EQ(firmware_info.len, fwsize);
  }

  LOG_DEBUG << "getFirmwareInfo: action-handler provides length only";
  {
    const std::string tgtname{"my-firmware.bin_4.0"};
    const size_t fwsize = 1048583;  // Using a prime number.

    // Create a firmware binary with random data.
    {
      std::ofstream fwfile(sconfig_->firmware_path.c_str(), std::ios::out | std::ios::binary);
      for (size_t index = 0; index < fwsize; index++) {
        fwfile.put(static_cast<char>(std::rand() % 256));
      }
      std::ofstream tgtname_file(sconfig_->target_name_path.c_str());
      tgtname_file << tgtname;
    }

    // Get expected hash by running well-known program.
    const std::string expected_hash = getSha256Sum(sconfig_->firmware_path);

    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\": \"ok\", \"message\": \"user message\", \"length\": 1234}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), true);
    EXPECT_EQ(firmware_info.name, tgtname);
    EXPECT_EQ(firmware_info.hash, expected_hash);
    EXPECT_EQ(firmware_info.len, fwsize);
  }

  LOG_DEBUG << "getFirmwareInfo: action-handler provides both sha256 and length";
  {
    const std::string tgtname{"my-firmware.bin_5.1"};
    const size_t fwsize = 1048583;  // Using a prime number.

    // Create a firmware binary with random data.
    {
      std::ofstream fwfile(sconfig_->firmware_path.c_str(), std::ios::out | std::ios::binary);
      for (size_t index = 0; index < fwsize; index++) {
        fwfile.put(static_cast<char>(std::rand() % 256));
      }
      std::ofstream tgtname_file(sconfig_->target_name_path.c_str());
      tgtname_file << tgtname;
    }

    // Get expected hash by running well-known program.
    // const std::string correct_hash = getSha256Sum(config_->firmware_path);

    Uptane::InstalledImageInfo firmware_info;
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT",
           "{\"status\": \"ok\", \"message\": \"user message\", "
           "\"sha256\": \"a1b2c3\", \"length\": 1234}");
    EXPECT_EQ(secondary_->getFirmwareInfo(firmware_info), true);
    EXPECT_EQ(firmware_info.name, tgtname);
    EXPECT_EQ(firmware_info.hash, "a1b2c3");
    EXPECT_EQ(firmware_info.len, 1234);
  }
}

TEST_F(TorizonGenericSecondaryTest, InstallFailure) {
  logger_set_threshold(boost::log::trivial::trace);
  makeSecondary("tests/torizon/test_install.sh");

  // Create a target with a known SHA-256.
  const std::string expected_sha256{"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"};
  Json::Value target_json;
  target_json["hashes"]["sha256"] = expected_sha256;
  target_json["custom"]["uri"] = "test-uri";
  target_json["length"] = 1;
  Uptane::Target target("fake_file", target_json);
  {
    auto out = package_manager_->createTargetFile(target);
    out << "a";
  }

  InstallInfo info(UpdateType::kOnline);

  LOG_DEBUG << "install: action-handler ends due to signal";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "terminate-with-signal-TERM");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kGeneralError);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "install: action-handler produces bad output";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"value\":}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kGeneralError);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "install: action-handler requests error processing";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-65");
    setenv("TEST_JSON_OUTPUT", "{this should be ignored}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kInstallFailed);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "install: action-handler generates invalid exit code";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-5");
    setenv("TEST_JSON_OUTPUT", "{this should be ignored}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kInstallFailed);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "install: action-handler output lacks status field";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"message\":\"lacking required field\"}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kGeneralError);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "install: action-handler reports failure";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"message\":\"something failed\", \"status\": \"failed\"}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kInstallFailed);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "install: action-handler reports failure (uppercase status)";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"message\":\"something failed\", \"status\": \"failed\"}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kInstallFailed);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "install: action-handler reports unknown status";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"message\":\"something failed\", \"status\": \"weird-status\"}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kGeneralError);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }
}

TEST_F(TorizonGenericSecondaryTest, InstallSuccess) {
  logger_set_threshold(boost::log::trivial::trace);
  makeSecondary("tests/torizon/test_install.sh");

  // Create a target with a known SHA-256.
  const std::string expected_sha256{"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"};
  Json::Value target_json;
  target_json["hashes"]["sha256"] = expected_sha256;
  target_json["custom"]["uri"] = "test-uri";
  target_json["length"] = 1;
  Uptane::Target target("fake_file", target_json);
  {
    auto out = package_manager_->createTargetFile(target);
    out << "a";
  }

  InstallInfo info(UpdateType::kOnline);

  LOG_DEBUG << "install: action-handler requests normal processing";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-64");
    setenv("TEST_JSON_OUTPUT", "{irrelevant output}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kOk);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "install: action-handler indicates success explicitly";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\": \"ok\", \"message\": \"everything went fine\"}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kOk);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }
}

TEST_F(TorizonGenericSecondaryTest, InstallPending) {
  logger_set_threshold(boost::log::trivial::trace);
  makeSecondary("tests/torizon/test_install.sh");

  // Create a target with a known SHA-256.
  const std::string expected_sha256{"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"};
  Json::Value target_json;
  target_json["hashes"]["sha256"] = expected_sha256;
  target_json["custom"]["uri"] = "test-uri";
  target_json["length"] = 1;
  Uptane::Target target("fake_file", target_json);
  {
    auto out = package_manager_->createTargetFile(target);
    out << "a";
  }

  InstallInfo info(UpdateType::kOnline);

  LOG_DEBUG << "install: action-handler indicates completion is pending";
  {
    touch(sconfig_->firmware_path);
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\": \"need-completion\"}");
    EXPECT_EQ(secondary_->install(target, info, nullptr).result_code, data::ResultCode::Numeric::kNeedCompletion);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path));
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }
}

TEST_F(TorizonGenericSecondaryTest, CompleteInstallFailure) {
  logger_set_threshold(boost::log::trivial::trace);
  makeSecondary("tests/torizon/test_complete_install.sh");

  // Create a target with a known SHA-256.
  const std::string expected_sha256{"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"};
  Json::Value target_json;
  target_json["hashes"]["sha256"] = expected_sha256;
  target_json["custom"]["uri"] = "test-uri";
  target_json["length"] = 1;
  Uptane::Target target("fake_file", target_json);
  {
    auto out = package_manager_->createTargetFile(target);
    out << "a";
  }

  LOG_DEBUG << "completeInstall: action-handler ends due to signal";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    setenv("TEST_COMMAND", "terminate-with-signal-TERM");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kGeneralError);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "completeInstall: action-handler produces bad output";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"value\":}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kGeneralError);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "completeInstall: action-handler requests error processing";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-65");
    setenv("TEST_JSON_OUTPUT", "{this should be ignored}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kInstallFailed);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "completeInstall: action-handler generates invalid exit code";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-5");
    setenv("TEST_JSON_OUTPUT", "{this should be ignored}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kInstallFailed);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "completeInstall: action-handler output lacks status field";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"message\":\"lacking required field\"}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kGeneralError);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "completeInstall: action-handler reports failure";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"message\":\"something failed\", \"status\": \"failed\"}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kInstallFailed);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "completeInstall: action-handler reports unknown status";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"message\":\"something failed\", \"status\": \"weird-status\"}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kGeneralError);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }
}

TEST_F(TorizonGenericSecondaryTest, CompleteInstallSuccess) {
  logger_set_threshold(boost::log::trivial::trace);
  makeSecondary("tests/torizon/test_complete_install.sh");

  // Create a target with a known SHA-256.
  const std::string expected_sha256{"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"};
  Json::Value target_json;
  target_json["hashes"]["sha256"] = expected_sha256;
  target_json["custom"]["uri"] = "test-uri";
  target_json["length"] = 1;
  Uptane::Target target("fake_file", target_json);
  {
    auto out = package_manager_->createTargetFile(target);
    out << "a";
  }

  LOG_DEBUG << "completeInstall: action-handler requests normal processing";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    touch(sconfig_->target_name_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-64");
    setenv("TEST_JSON_OUTPUT", "{irrelevant output}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kOk);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "completeInstall: action-handler indicates success explicitly";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    touch(sconfig_->target_name_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\": \"ok\", \"message\": \"everything went fine\"}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kOk);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "completeInstall: action-handler indicates success explicitly (w/non-existing files)";
  {
    // TODO: This should never happen but currently it's not handled as an error.
    boost::filesystem::remove(sconfig_->firmware_path);
    boost::filesystem::remove(sconfig_->firmware_path.string() + ".new");
    boost::filesystem::remove(sconfig_->target_name_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\": \"ok\", \"message\": \"everything went fine\"}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kOk);
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_FALSE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }

  LOG_DEBUG << "completeInstall: action-handler indicates completion is pending";
  {
    touch(sconfig_->firmware_path);
    touch(sconfig_->firmware_path.string() + ".new");
    touch(sconfig_->target_name_path.string() + ".new");
    setenv("TEST_COMMAND", "exit-with-json-output-code-0");
    setenv("TEST_JSON_OUTPUT", "{\"status\": \"need-completion\"}");
    EXPECT_EQ(secondary_->completeInstall(target).result_code, data::ResultCode::Numeric::kNeedCompletion);
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string()));
    EXPECT_TRUE(boost::filesystem::exists(sconfig_->firmware_path.string() + ".new"));
  }
}

}  // namespace Primary

#ifndef __NO_MAIN__
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  return RUN_ALL_TESTS();
}
#endif
