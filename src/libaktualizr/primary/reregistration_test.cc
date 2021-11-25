#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "httpfake.h"
#include "libaktualizr/config.h"
#include "uptane_test_common.h"
#include "virtualsecondary.h"

boost::filesystem::path fake_meta_dir;

class HttpFakeRegistration : public HttpFake {
 public:
  HttpFakeRegistration(const boost::filesystem::path& test_dir_in, const boost::filesystem::path& meta_dir_in)
      : HttpFake(test_dir_in, "noupdates", meta_dir_in) {}

  HttpResponse post(const std::string& url, const Json::Value& data) override {
    if (url.find("/devices") != std::string::npos) {
      device_registration_count++;
      auto this_device_id = data["deviceId"].asString();
      if (ecu_registration_count <= 1) {
        device_id = this_device_id;
      } else {
        EXPECT_EQ(device_id, this_device_id) << "deviceId should change during provisioning";
      }
    }
    if (url.find("/director/ecus") != std::string::npos) {
      ecu_registration_count++;
      EXPECT_EQ(data["primary_ecu_serial"].asString(), "CA:FE:A6:D2:84:9D");
      EXPECT_EQ(data["ecus"][0]["ecu_serial"].asString(), "CA:FE:A6:D2:84:9D");
      EXPECT_EQ(data["ecus"][0]["hardware_identifier"].asString(), "primary_hw");
      if (ecu_registration_count == 1) {
        primary_ecu_info = data["ecus"][0];
      } else {
        EXPECT_EQ(primary_ecu_info, data["ecus"][0]) << "Information about primary ECU shouldn't change";
      }
    }

    return HttpFake::post(url, data);
  }

  unsigned int ecu_registration_count{0};
  unsigned int device_registration_count{0};
  Json::Value primary_ecu_info;
  std::string device_id;
};

/*
 * Add a Secondary via API, register the ECUs, then add another, and re-register.
 */
TEST(Aktualizr, AddSecondary) {
  TemporaryDirectory temp_dir;
  auto http = std::make_shared<HttpFakeRegistration>(temp_dir.Path(), fake_meta_dir);
  Config conf = UptaneTestCommon::makeTestConfig(temp_dir, http->tls_server);
  auto storage = INvStorage::newStorage(conf.storage);

  UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
  Primary::VirtualSecondaryConfig ecu_config = UptaneTestCommon::altVirtualConfiguration(temp_dir.Path());
  aktualizr.AddSecondary(std::make_shared<Primary::VirtualSecondary>(ecu_config));
  aktualizr.Initialize();

  std::vector<std::string> expected_ecus = {"CA:FE:A6:D2:84:9D", "ecuserial3", "secondary_ecu_serial"};
  UptaneTestCommon::verifyEcus(temp_dir, expected_ecus);
  EXPECT_EQ(http->device_registration_count, 1);
  EXPECT_EQ(http->ecu_registration_count, 1);

  ecu_config.ecu_serial = "ecuserial4";
  aktualizr.AddSecondary(std::make_shared<Primary::VirtualSecondary>(ecu_config));
  aktualizr.Initialize();
  expected_ecus.push_back(ecu_config.ecu_serial);
  UptaneTestCommon::verifyEcus(temp_dir, expected_ecus);
  EXPECT_EQ(http->device_registration_count, 1);
  EXPECT_EQ(http->ecu_registration_count, 2);
}

/*
 * Add a Secondary via API, register the ECUs, remove one, and re-register.
 */
TEST(Aktualizr, RemoveSecondary) {
  TemporaryDirectory temp_dir;
  auto http = std::make_shared<HttpFakeRegistration>(temp_dir.Path(), fake_meta_dir);
  Config conf = UptaneTestCommon::makeTestConfig(temp_dir, http->tls_server);
  auto storage = INvStorage::newStorage(conf.storage);

  {
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    Primary::VirtualSecondaryConfig ecu_config = UptaneTestCommon::altVirtualConfiguration(temp_dir.Path());
    aktualizr.AddSecondary(std::make_shared<Primary::VirtualSecondary>(ecu_config));
    aktualizr.Initialize();

    std::vector<std::string> expected_ecus = {"CA:FE:A6:D2:84:9D", "ecuserial3", "secondary_ecu_serial"};
    UptaneTestCommon::verifyEcus(temp_dir, expected_ecus);
    EXPECT_EQ(http->device_registration_count, 1);
    EXPECT_EQ(http->ecu_registration_count, 1);
  }

  {
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    aktualizr.Initialize();

    std::vector<std::string> expected_ecus = {"CA:FE:A6:D2:84:9D", "secondary_ecu_serial"};
    UptaneTestCommon::verifyEcus(temp_dir, expected_ecus);
    EXPECT_EQ(http->device_registration_count, 1);
    EXPECT_EQ(http->ecu_registration_count, 2);
  }
}

/*
 * Add a Secondary via API, register the ECUs, replace one, and re-register.
 */
TEST(Aktualizr, ReplaceSecondary) {
  TemporaryDirectory temp_dir;
  auto http = std::make_shared<HttpFakeRegistration>(temp_dir.Path(), fake_meta_dir);
  Config conf = UptaneTestCommon::makeTestConfig(temp_dir, http->tls_server);
  auto storage = INvStorage::newStorage(conf.storage);

  {
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    Primary::VirtualSecondaryConfig ecu_config = UptaneTestCommon::altVirtualConfiguration(temp_dir.Path());
    aktualizr.AddSecondary(std::make_shared<Primary::VirtualSecondary>(ecu_config));
    aktualizr.Initialize();

    std::vector<std::string> expected_ecus = {"CA:FE:A6:D2:84:9D", "ecuserial3", "secondary_ecu_serial"};
    UptaneTestCommon::verifyEcus(temp_dir, expected_ecus);
    EXPECT_EQ(http->device_registration_count, 1);
    EXPECT_EQ(http->ecu_registration_count, 1);
  }

  {
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    Primary::VirtualSecondaryConfig ecu_config = UptaneTestCommon::altVirtualConfiguration(temp_dir.Path());
    ecu_config.ecu_serial = "ecuserial4";
    aktualizr.AddSecondary(std::make_shared<Primary::VirtualSecondary>(ecu_config));
    aktualizr.Initialize();

    std::vector<std::string> expected_ecus = {"CA:FE:A6:D2:84:9D", "ecuserial4", "secondary_ecu_serial"};
    UptaneTestCommon::verifyEcus(temp_dir, expected_ecus);
    EXPECT_EQ(http->device_registration_count, 1);
    EXPECT_EQ(http->ecu_registration_count, 2);
  }
}

/**
 * Restarting Aktualizr without changing the secondaries should not result in it getting re-registered
 */
TEST(Aktualizr, RestartNoRegisterSecondaries) {
  TemporaryDirectory temp_dir;
  auto http = std::make_shared<HttpFakeRegistration>(temp_dir.Path(), fake_meta_dir);
  Config conf = UptaneTestCommon::makeTestConfig(temp_dir, http->tls_server);
  auto storage = INvStorage::newStorage(conf.storage);

  {
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    Primary::VirtualSecondaryConfig ecu_config = UptaneTestCommon::altVirtualConfiguration(temp_dir.Path());
    aktualizr.AddSecondary(std::make_shared<Primary::VirtualSecondary>(ecu_config));
    aktualizr.Initialize();
    EXPECT_EQ(http->device_registration_count, 1);
    EXPECT_EQ(http->ecu_registration_count, 1);
  }

  {
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    Primary::VirtualSecondaryConfig ecu_config = UptaneTestCommon::altVirtualConfiguration(temp_dir.Path());
    aktualizr.AddSecondary(std::make_shared<Primary::VirtualSecondary>(ecu_config));
    aktualizr.Initialize();
    EXPECT_EQ(http->device_registration_count, 1);
    EXPECT_EQ(http->ecu_registration_count, 1);
  }
}

/**
 * Restarting Aktualizr should not result in it getting re-registered if it has no secondaries.
 * This is similar to RestartNoRegisterSecondaries, but with zero secondaries.
 */
TEST(Aktualizr, RestartNoRegisterPrimaryOnly) {
  TemporaryDirectory temp_dir;
  auto http = std::make_shared<HttpFakeRegistration>(temp_dir.Path(), fake_meta_dir);
  Config conf = UptaneTestCommon::makeTestConfig(temp_dir, http->tls_server);

  {
    auto storage = INvStorage::newStorage(conf.storage);
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    aktualizr.Initialize();
    EXPECT_EQ(http->device_registration_count, 1);
    EXPECT_EQ(http->ecu_registration_count, 1);
  }

  {
    auto storage = INvStorage::newStorage(conf.storage);
    UptaneTestCommon::TestAktualizr aktualizr(conf, storage, http);
    aktualizr.Initialize();
    EXPECT_EQ(http->device_registration_count, 1);
    EXPECT_EQ(http->ecu_registration_count, 1);
  }
}

#ifndef __NO_MAIN__
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  TemporaryDirectory tmp_dir;
  fake_meta_dir = tmp_dir.Path();
  MetaFake meta_fake(fake_meta_dir);

  return RUN_ALL_TESTS();
}
#endif

// vim: set tabstop=2 shiftwidth=2 expandtab:
