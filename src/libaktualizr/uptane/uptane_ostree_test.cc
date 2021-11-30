#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include "httpfake.h"
#include "libaktualizr/config.h"
#include "package_manager/ostreemanager.h"
#include "storage/invstorage.h"
#include "uptane_test_common.h"
#include "utilities/utils.h"

boost::filesystem::path test_sysroot;

TEST(UptaneOstree, InitialManifest) {
  TemporaryDirectory temp_dir;
  auto http = std::make_shared<HttpFake>(temp_dir.Path());
  Config config("tests/config/basic.toml");
  config.pacman.type = PACKAGE_MANAGER_OSTREE;
  config.pacman.sysroot = test_sysroot;
  config.storage.path = temp_dir.Path();
  config.pacman.booted = BootedType::kStaged;
  config.uptane.director_server = http->tls_server + "director";
  config.uptane.repo_server = http->tls_server + "repo";
  config.provision.primary_ecu_serial = "CA:FE:A6:D2:84:9D";
  config.provision.primary_ecu_hardware_id = "primary_hw";

  auto storage = INvStorage::newStorage(config.storage);
  auto sota_client = std_::make_unique<UptaneTestCommon::TestUptaneClient>(config, storage, http);
  EXPECT_NO_THROW(sota_client->initialize());
  auto manifest = sota_client->AssembleManifest();
  // Fish the sha256 hash out of the manifest
  auto installed_image =
      manifest["ecu_version_manifests"][config.provision.primary_ecu_serial]["signed"]["installed_image"];
  std::string hash = installed_image["fileinfo"]["hashes"]["sha256"].asString();

  // e3b0c442... is the sha256 hash of the empty string (i.e. echo -n | sha256sum)
  EXPECT_NE(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
      << "should not be the hash of the empty string";

  OstreeManager ostree(config.pacman, config.bootloader, storage, nullptr);
  EXPECT_EQ(hash, ostree.getCurrentHash());
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  if (argc != 2) {
    std::cerr << "Error: " << argv[0] << " requires the path to an OSTree sysroot as an input argument.\n";
    return EXIT_FAILURE;
  }

  TemporaryDirectory temp_sysroot;
  test_sysroot = temp_sysroot / "sysroot";
  // uses cp, as boost doesn't like to copy bad symlinks
  int r = system((std::string("cp -r ") + argv[1] + std::string(" ") + test_sysroot.string()).c_str());
  if (r != 0) {
    return EXIT_FAILURE;
  }

  return RUN_ALL_TESTS();
}
#endif