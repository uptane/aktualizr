#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include "httpfake.h"
#include "libaktualizr/config.h"
#include "package_manager/ostreemanager.h"
#include "storage/invstorage.h"
#include "uptane_test_common.h"
#include "utilities/utils.h"

TEST(UptaneCancellation, Simple) {
  Config conf("tests/config/basic.toml");
  TemporaryDirectory temp_dir;
  auto http = std::make_shared<HttpFake>(temp_dir.Path(), "hasupdates");
  conf.uptane.director_server = http->tls_server + "director";
  conf.uptane.repo_server = http->tls_server + "repo";
  conf.pacman.type = PACKAGE_MANAGER_NONE;
  conf.pacman.images_path = temp_dir.Path() / "images";
  conf.provision.primary_ecu_serial = "CA:FE:A6:D2:84:9D";
  conf.provision.primary_ecu_hardware_id = "primary_hw";
  conf.storage.path = temp_dir.Path();
  conf.tls.server = http->tls_server;
  UptaneTestCommon::addDefaultSecondary(conf, temp_dir, "secondary_ecu_serial", "secondary_hw");

  conf.postUpdateValues();

  auto storage = INvStorage::newStorage(conf.storage);
  auto events_channel = std::make_shared<event::Channel>();
  auto dut = std_::make_unique<UptaneTestCommon::TestUptaneClient>(conf, storage, http, events_channel);
  EXPECT_NO_THROW(dut->initialize());

  // Given the flow control is cancelled...
  auto flow_control = dut->FlowControlToken();
  flow_control->setAbort();

  // ...checking for updates should abort
  auto result = dut->fetchMeta();
  EXPECT_EQ(result.status, result::UpdateStatus::kError);

  // But trying again (with flow control reset) succeeds
  flow_control->reset();

  auto result2 = dut->fetchMeta();
  EXPECT_EQ(result2.status, result::UpdateStatus::kUpdatesAvailable);
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  logger_init();
  logger_set_threshold(boost::log::trivial::trace);

  return RUN_ALL_TESTS();
}
#endif