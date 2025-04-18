#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <boost/filesystem.hpp>

#include "crypto/crypto.h"
#include "libaktualizr/types.h"
#include "repo.h"
#include "storage/sqlstorage.h"
#include "utilities/utils.h"

namespace fs = boost::filesystem;

std::unique_ptr<INvStorage> Storage(const fs::path &dir) {
  StorageConfig storage_config;
  storage_config.type = StorageType::kSqlite;
  storage_config.path = dir;
  return std::unique_ptr<INvStorage>(new SQLStorage(storage_config, false));
}

StorageConfig MakeConfig(StorageType type, const fs::path &storage_dir) {
  StorageConfig config;

  config.type = type;
  if (config.type == StorageType::kSqlite) {
    config.sqldb_path = storage_dir / "test.db";
  } else {
    throw std::runtime_error("Invalid config type");
  }
  return config;
}

/* Load and store Primary keys. */
TEST(StorageCommon, LoadStorePrimaryKeys) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  storage->storePrimaryKeys("", "");
  storage->storePrimaryKeys("pr_public", "pr_private");

  std::string pubkey;
  std::string privkey;

  EXPECT_TRUE(storage->loadPrimaryKeys(&pubkey, &privkey));
  EXPECT_EQ(pubkey, "pr_public");
  EXPECT_EQ(privkey, "pr_private");
  storage->clearPrimaryKeys();
  EXPECT_FALSE(storage->loadPrimaryKeys(nullptr, nullptr));
}

/* Load and store TLS credentials. */
TEST(StorageCommon, LoadStoreTls) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  storage->storeTlsCreds("", "", "");
  storage->storeTlsCreds("ca", "cert", "priv");
  std::string ca;
  std::string cert;
  std::string priv;

  EXPECT_TRUE(storage->loadTlsCreds(&ca, &cert, &priv));

  EXPECT_EQ(ca, "ca");
  EXPECT_EQ(cert, "cert");
  EXPECT_EQ(priv, "priv");
  storage->clearTlsCreds();
  EXPECT_FALSE(storage->loadTlsCreds(nullptr, nullptr, nullptr));
}

/* Load and store Uptane metadata. */
TEST(StorageCommon, LoadStoreMetadata) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  Json::Value root_json;
  root_json["_type"] = "Root";
  root_json["consistent_snapshot"] = false;
  root_json["expires"] = "2038-01-19T03:14:06Z";
  root_json["keys"]["firstid"]["keytype"] = "ed25519";
  root_json["keys"]["firstid"]["keyval"]["public"] = "firstval";
  root_json["keys"]["secondid"]["keytype"] = "ed25519";
  root_json["keys"]["secondid"]["keyval"]["public"] = "secondval";

  root_json["roles"]["root"]["threshold"] = 1;
  root_json["roles"]["root"]["keyids"][0] = "firstid";
  root_json["roles"]["snapshot"]["threshold"] = 1;
  root_json["roles"]["snapshot"]["keyids"][0] = "firstid";
  root_json["roles"]["targets"]["threshold"] = 1;
  root_json["roles"]["targets"]["keyids"][0] = "firstid";
  root_json["roles"]["timestamp"]["threshold"] = 1;
  root_json["roles"]["timestamp"]["keyids"][0] = "firstid";

  Json::Value meta_root;
  meta_root["signed"] = root_json;
  std::string director_root = Utils::jsonToStr(meta_root);
  std::string image_root = Utils::jsonToStr(meta_root);

  Json::Value targets_json;
  targets_json["_type"] = "Targets";
  targets_json["expires"] = "2038-01-19T03:14:06Z";
  targets_json["targets"]["file1"]["custom"]["ecu_identifier"] = "ecu1";
  targets_json["targets"]["file1"]["custom"]["hardware_identifier"] = "hw1";
  targets_json["targets"]["file1"]["hashes"]["sha256"] = "12ab";
  targets_json["targets"]["file1"]["length"] = 1;
  targets_json["targets"]["file2"]["custom"]["ecu_identifier"] = "ecu2";
  targets_json["targets"]["file2"]["custom"]["hardware_identifier"] = "hw2";
  targets_json["targets"]["file2"]["hashes"]["sha512"] = "12ab";
  targets_json["targets"]["file2"]["length"] = 11;

  Json::Value meta_targets;
  meta_targets["signed"] = targets_json;
  std::string director_targets = Utils::jsonToStr(meta_targets);
  std::string image_targets = Utils::jsonToStr(meta_targets);

  Json::Value timestamp_json;
  timestamp_json["signed"]["_type"] = "Timestamp";
  timestamp_json["signed"]["expires"] = "2038-01-19T03:14:06Z";
  std::string image_timestamp = Utils::jsonToStr(timestamp_json);

  Json::Value snapshot_json;
  snapshot_json["_type"] = "Snapshot";
  snapshot_json["expires"] = "2038-01-19T03:14:06Z";
  snapshot_json["meta"]["root.json"]["version"] = 1;
  snapshot_json["meta"]["targets.json"]["version"] = 2;
  snapshot_json["meta"]["timestamp.json"]["version"] = 3;
  snapshot_json["meta"]["snapshot.json"]["version"] = 4;

  Json::Value meta_snapshot;
  meta_snapshot["signed"] = snapshot_json;
  std::string image_snapshot = Utils::jsonToStr(meta_snapshot);

  storage->storeRoot(director_root, Uptane::RepositoryType::Director(), Uptane::Version(1));
  storage->storeNonRoot(director_targets, Uptane::RepositoryType::Director(), Uptane::Role::Targets());
  storage->storeRoot(image_root, Uptane::RepositoryType::Image(), Uptane::Version(1));
  storage->storeNonRoot(image_targets, Uptane::RepositoryType::Image(), Uptane::Role::Targets());
  storage->storeNonRoot(image_timestamp, Uptane::RepositoryType::Image(), Uptane::Role::Timestamp());
  storage->storeNonRoot(image_snapshot, Uptane::RepositoryType::Image(), Uptane::Role::Snapshot());

  std::string loaded_director_root;
  std::string loaded_director_targets;
  std::string loaded_image_root;
  std::string loaded_image_targets;
  std::string loaded_image_timestamp;
  std::string loaded_image_snapshot;

  EXPECT_TRUE(storage->loadLatestRoot(&loaded_director_root, Uptane::RepositoryType::Director()));
  EXPECT_TRUE(
      storage->loadNonRoot(&loaded_director_targets, Uptane::RepositoryType::Director(), Uptane::Role::Targets()));
  EXPECT_TRUE(storage->loadLatestRoot(&loaded_image_root, Uptane::RepositoryType::Image()));
  EXPECT_TRUE(storage->loadNonRoot(&loaded_image_targets, Uptane::RepositoryType::Image(), Uptane::Role::Targets()));
  EXPECT_TRUE(
      storage->loadNonRoot(&loaded_image_timestamp, Uptane::RepositoryType::Image(), Uptane::Role::Timestamp()));
  EXPECT_TRUE(storage->loadNonRoot(&loaded_image_snapshot, Uptane::RepositoryType::Image(), Uptane::Role::Snapshot()));
  EXPECT_EQ(director_root, loaded_director_root);
  EXPECT_EQ(director_targets, loaded_director_targets);
  EXPECT_EQ(image_root, loaded_image_root);
  EXPECT_EQ(image_targets, loaded_image_targets);
  EXPECT_EQ(image_timestamp, loaded_image_timestamp);
  EXPECT_EQ(image_snapshot, loaded_image_snapshot);

  storage->clearNonRootMeta(Uptane::RepositoryType::Director());
  storage->clearNonRootMeta(Uptane::RepositoryType::Image());
  EXPECT_FALSE(
      storage->loadNonRoot(&loaded_director_targets, Uptane::RepositoryType::Director(), Uptane::Role::Targets()));
  EXPECT_FALSE(
      storage->loadNonRoot(&loaded_image_timestamp, Uptane::RepositoryType::Image(), Uptane::Role::Timestamp()));
}

/* Load and store Uptane roots. */
TEST(StorageCommon, LoadStoreRoot) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  Json::Value root_json;
  root_json["_type"] = "Root";
  root_json["consistent_snapshot"] = false;
  root_json["expires"] = "2038-01-19T03:14:06Z";
  root_json["keys"]["firstid"]["keytype"] = "ed25519";
  root_json["keys"]["firstid"]["keyval"]["public"] = "firstval";
  root_json["keys"]["secondid"]["keytype"] = "ed25519";
  root_json["keys"]["secondid"]["keyval"]["public"] = "secondval";

  root_json["roles"]["root"]["threshold"] = 1;
  root_json["roles"]["root"]["keyids"][0] = "firstid";
  root_json["roles"]["snapshot"]["threshold"] = 1;
  root_json["roles"]["snapshot"]["keyids"][0] = "firstid";
  root_json["roles"]["targets"]["threshold"] = 1;
  root_json["roles"]["targets"]["keyids"][0] = "firstid";
  root_json["roles"]["timestamp"]["threshold"] = 1;
  root_json["roles"]["timestamp"]["keyids"][0] = "firstid";

  Json::Value meta_root;
  meta_root["signed"] = root_json;

  std::string loaded_root;

  storage->storeRoot(Utils::jsonToStr(meta_root), Uptane::RepositoryType::Director(), Uptane::Version(2));
  EXPECT_TRUE(storage->loadRoot(&loaded_root, Uptane::RepositoryType::Director(), Uptane::Version(2)));
  EXPECT_EQ(Utils::jsonToStr(meta_root), loaded_root);

  EXPECT_TRUE(storage->loadLatestRoot(&loaded_root, Uptane::RepositoryType::Director()));
  EXPECT_EQ(Utils::jsonToStr(meta_root), loaded_root);
}

/* Load and store the device ID. */
TEST(StorageCommon, LoadStoreDeviceId) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  storage->storeDeviceId("");
  storage->storeDeviceId("device_id");

  std::string device_id;

  EXPECT_TRUE(storage->loadDeviceId(&device_id));

  EXPECT_EQ(device_id, "device_id");
  storage->clearDeviceId();
  EXPECT_FALSE(storage->loadDeviceId(nullptr));
}

/* Load and store ECU serials.
 * Preserve ECU ordering between store and load calls.
 */
TEST(StorageCommon, LoadStoreEcuSerials) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  storage->storeEcuSerials({{Uptane::EcuSerial("a"), Uptane::HardwareIdentifier("")}});
  EcuSerials serials{{Uptane::EcuSerial("primary"), Uptane::HardwareIdentifier("primary_hw")},
                     {Uptane::EcuSerial("secondary_1"), Uptane::HardwareIdentifier("secondary_hw")},
                     {Uptane::EcuSerial("secondary_2"), Uptane::HardwareIdentifier("secondary_hw")}};
  storage->storeEcuSerials(serials);

  EcuSerials serials_out;

  EXPECT_TRUE(storage->loadEcuSerials(&serials_out));

  EXPECT_EQ(serials, serials_out);
  storage->clearEcuSerials();
  EXPECT_FALSE(storage->loadEcuSerials(nullptr));
}

/* Load and store a list of misconfigured ECUs. */
TEST(StorageCommon, LoadStoreMisconfiguredEcus) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  storage->saveMisconfiguredEcu(
      {Uptane::EcuSerial("primary"), Uptane::HardwareIdentifier("primary_hw"), EcuState::kOld});

  std::vector<MisconfiguredEcu> ecus_out;

  EXPECT_TRUE(storage->loadMisconfiguredEcus(&ecus_out));

  EXPECT_EQ(ecus_out.size(), 1);
  EXPECT_EQ(ecus_out[0].serial, Uptane::EcuSerial("primary"));
  EXPECT_EQ(ecus_out[0].hardware_id, Uptane::HardwareIdentifier("primary_hw"));
  EXPECT_EQ(ecus_out[0].state, EcuState::kOld);

  storage->clearMisconfiguredEcus();
  ecus_out.clear();
  EXPECT_FALSE(storage->loadMisconfiguredEcus(&ecus_out));
}

/* Load and store a flag indicating successful registration. */
TEST(StorageCommon, LoadStoreEcuRegistered) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  EXPECT_THROW(storage->storeEcuRegistered(), std::runtime_error);
  storage->storeDeviceId("test");
  storage->storeEcuRegistered();
  storage->storeEcuRegistered();

  EXPECT_TRUE(storage->loadEcuRegistered());

  storage->clearEcuRegistered();
  EXPECT_FALSE(storage->loadEcuRegistered());
}

/* Load and store installed versions. */
TEST(StorageCommon, LoadStoreInstalledVersions) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  // Test lazy Primary installed version: Primary ECU serial is not defined yet
  const std::vector<Hash> hashes = {
      Hash{Hash::Type::kSha256, "2561"},
      Hash{Hash::Type::kSha512, "5121"},
  };
  Uptane::EcuMap primary_ecu{{Uptane::EcuSerial("primary"), Uptane::HardwareIdentifier("primary_hw")}};
  Uptane::Target t1{"update.bin", primary_ecu, hashes, 1};
  Json::Value custom;
  custom["version"] = 42;
  custom["foo"] = "bar";
  t1.updateCustom(custom);
  storage->savePrimaryInstalledVersion(t1, InstalledVersionUpdateMode::kCurrent, "corrid");
  {
    std::vector<Uptane::Target> log;
    storage->loadPrimaryInstallationLog(&log, true);
    EXPECT_EQ(log.size(), 1);
    EXPECT_EQ(log[0].filename(), "update.bin");
  }

  EcuSerials serials{{Uptane::EcuSerial("primary"), Uptane::HardwareIdentifier("primary_hw")},
                     {Uptane::EcuSerial("secondary_1"), Uptane::HardwareIdentifier("secondary_hw")},
                     {Uptane::EcuSerial("secondary_2"), Uptane::HardwareIdentifier("secondary_hw")}};
  storage->storeEcuSerials(serials);

  {
    boost::optional<Uptane::Target> current;
    Uptane::CorrelationId correlation_id;
    EXPECT_TRUE(storage->loadInstalledVersions("primary", &current, nullptr, &correlation_id));
    EXPECT_FALSE(storage->hasPendingInstall());
    EXPECT_TRUE(!!current);
    EXPECT_EQ(current->filename(), "update.bin");
    EXPECT_EQ(current->sha256Hash(), "2561");
    EXPECT_EQ(current->hashes(), hashes);
    EXPECT_EQ(current->ecus(), primary_ecu);
    EXPECT_EQ(correlation_id, "corrid");
    EXPECT_EQ(current->length(), 1);
    EXPECT_EQ(current->custom_data()["foo"], "bar");
    EXPECT_EQ(current->custom_data()["version"], 42);
  }

  // Set t2 as a pending version
  Uptane::Target t2{"update2.bin", primary_ecu, {Hash{Hash::Type::kSha256, "2562"}}, 2};
  storage->savePrimaryInstalledVersion(t2, InstalledVersionUpdateMode::kPending, "");

  {
    boost::optional<Uptane::Target> pending;
    EXPECT_TRUE(storage->loadInstalledVersions("primary", nullptr, &pending));
    EXPECT_TRUE(!!pending);
    EXPECT_TRUE(storage->hasPendingInstall());
    EXPECT_EQ(pending->filename(), "update2.bin");
  }

  // Set t3 as the new pending
  Uptane::Target t3{"update3.bin", primary_ecu, {Hash{Hash::Type::kSha256, "2563"}}, 3};
  storage->savePrimaryInstalledVersion(t3, InstalledVersionUpdateMode::kPending, "");

  {
    boost::optional<Uptane::Target> pending;
    EXPECT_TRUE(storage->loadInstalledVersions("primary", nullptr, &pending));
    EXPECT_TRUE(!!pending);
    EXPECT_TRUE(storage->hasPendingInstall());
    EXPECT_EQ(pending->filename(), "update3.bin");
  }

  // Set t3 as current: should replace the pending flag but not create a new
  // version
  storage->savePrimaryInstalledVersion(t3, InstalledVersionUpdateMode::kCurrent, "");
  {
    boost::optional<Uptane::Target> current;
    boost::optional<Uptane::Target> pending;
    EXPECT_TRUE(storage->loadInstalledVersions("primary", &current, &pending));
    EXPECT_TRUE(!!current);
    EXPECT_EQ(current->filename(), "update3.bin");
    EXPECT_FALSE(!!pending);
    EXPECT_FALSE(storage->hasPendingInstall());

    std::vector<Uptane::Target> log;
    storage->loadInstallationLog("primary", &log, true);
    EXPECT_EQ(log.size(), 2);
    EXPECT_EQ(log.back().filename(), "update3.bin");
  }

  // Set t1 as current: the log should have grown even though we rolled back
  {
    storage->savePrimaryInstalledVersion(t1, InstalledVersionUpdateMode::kCurrent, "");
    std::vector<Uptane::Target> log;
    storage->loadInstallationLog("primary", &log, true);
    EXPECT_EQ(log.size(), 3);
    EXPECT_EQ(log.back().filename(), "update.bin");
    EXPECT_FALSE(storage->hasPendingInstall());
  }

  // Set t2 as the new pending and t3 as current afterwards: the pending flag
  // should disappear
  storage->savePrimaryInstalledVersion(t2, InstalledVersionUpdateMode::kPending, "");
  storage->savePrimaryInstalledVersion(t3, InstalledVersionUpdateMode::kCurrent, "");

  {
    boost::optional<Uptane::Target> current;
    boost::optional<Uptane::Target> pending;
    EXPECT_TRUE(storage->loadInstalledVersions("primary", &current, &pending));
    EXPECT_TRUE(!!current);
    EXPECT_EQ(current->filename(), "update3.bin");
    EXPECT_FALSE(!!pending);
    EXPECT_FALSE(storage->hasPendingInstall());

    std::vector<Uptane::Target> log;
    storage->loadInstallationLog("primary", &log, true);
    EXPECT_EQ(log.size(), 4);
    EXPECT_EQ(log.back().filename(), "update3.bin");
    EXPECT_EQ(log[0].custom_data()["foo"], "bar");
  }

  // Add a Secondary installed version
  Uptane::EcuMap secondary_ecu{{Uptane::EcuSerial("secondary1"), Uptane::HardwareIdentifier("secondary_hw")}};
  Uptane::Target tsec{"secondary.bin", secondary_ecu, {Hash{Hash::Type::kSha256, "256s"}}, 4};
  storage->saveInstalledVersion("secondary_1", tsec, InstalledVersionUpdateMode::kCurrent, "");

  {
    EXPECT_TRUE(storage->loadInstalledVersions("primary", nullptr, nullptr));
    EXPECT_TRUE(storage->loadInstalledVersions("secondary_1", nullptr, nullptr));

    std::vector<Uptane::Target> log;
    storage->loadInstallationLog("secondary_1", &log, true);
    EXPECT_EQ(log.size(), 1);
    EXPECT_EQ(log.back().filename(), "secondary.bin");
  }
}

/*
 * Load and store an ECU installation result in an SQL database.
 * Load and store a device installation result in an SQL database.
 */
TEST(StorageCommon, LoadStoreInstallationResults) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  EcuSerials serials{{Uptane::EcuSerial("primary"), Uptane::HardwareIdentifier("primary_hw")},
                     {Uptane::EcuSerial("secondary_1"), Uptane::HardwareIdentifier("secondary_hw")},
                     {Uptane::EcuSerial("secondary_2"), Uptane::HardwareIdentifier("secondary_hw")}};
  storage->storeEcuSerials(serials);

  storage->saveEcuInstallationResult(Uptane::EcuSerial("secondary_2"), data::InstallationResult());
  storage->saveEcuInstallationResult(Uptane::EcuSerial("primary"), data::InstallationResult());
  storage->saveEcuInstallationResult(Uptane::EcuSerial("primary"),
                                     data::InstallationResult(data::ResultCode::Numeric::kGeneralError, ""));

  std::vector<std::pair<Uptane::EcuSerial, data::InstallationResult>> res;
  EXPECT_TRUE(storage->loadEcuInstallationResults(&res));
  EXPECT_EQ(res.size(), 2);
  EXPECT_EQ(res.at(0).first.ToString(), "primary");
  EXPECT_EQ(res.at(0).second.result_code.num_code, data::ResultCode::Numeric::kGeneralError);
  EXPECT_EQ(res.at(1).first.ToString(), "secondary_2");
  EXPECT_EQ(res.at(1).second.result_code.num_code, data::ResultCode::Numeric::kOk);

  storage->storeDeviceInstallationResult(data::InstallationResult(data::ResultCode::Numeric::kGeneralError, ""), "raw",
                                         "corrid");

  data::InstallationResult dev_res;
  std::string report;
  std::string correlation_id;
  EXPECT_TRUE(storage->loadDeviceInstallationResult(&dev_res, &report, &correlation_id));
  EXPECT_EQ(dev_res.result_code.num_code, data::ResultCode::Numeric::kGeneralError);
  EXPECT_EQ(report, "raw");
  EXPECT_EQ(correlation_id, "corrid");
  EXPECT_TRUE(storage->storeDeviceInstallationRawReport("user's raw report"));

  storage->clearInstallationResults();
  res.clear();
  EXPECT_FALSE(storage->loadEcuInstallationResults(&res));
  EXPECT_EQ(res.size(), 0);
  EXPECT_FALSE(storage->loadDeviceInstallationResult(&dev_res, &report, &correlation_id));
  EXPECT_FALSE(storage->storeDeviceInstallationRawReport(
      "This call will return a negative value since the installation report was cleaned!"));
}

TEST(StorageCommon, DownloadedFilesInfo) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  storage->storeTargetFilename("target1", "file1");
  storage->storeTargetFilename("target2", "file2");
  ASSERT_EQ(storage->getTargetFilename("target1"), "file1");
  ASSERT_EQ(storage->getTargetFilename("target2"), "file2");

  auto names = storage->getAllTargetNames();
  ASSERT_EQ(names.size(), 2);
  ASSERT_EQ(names.at(0), "target1");
  ASSERT_EQ(names.at(1), "target2");

  storage->deleteTargetInfo("target1");
  names = storage->getAllTargetNames();
  ASSERT_EQ(names.size(), 1);
  ASSERT_EQ(names.at(0), "target2");
}

TEST(StorageCommon, LoadStoreSecondaryInfo) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());

  // note: this can be done before the ECU is known
  storage->saveSecondaryData(Uptane::EcuSerial("secondary_2"), "data2");

  EcuSerials serials{{Uptane::EcuSerial("primary"), Uptane::HardwareIdentifier("primary_hw")},
                     {Uptane::EcuSerial("secondary_1"), Uptane::HardwareIdentifier("secondary_hw")},
                     {Uptane::EcuSerial("secondary_2"), Uptane::HardwareIdentifier("secondary_hw")}};
  storage->storeEcuSerials(serials);

  storage->saveSecondaryInfo(Uptane::EcuSerial("secondary_1"), "ip", PublicKey("key1", KeyType::kED25519));

  EXPECT_THROW(storage->saveSecondaryInfo(Uptane::EcuSerial("primary"), "ip", PublicKey("key0", KeyType::kRSA2048)),
               std::logic_error);

  std::vector<SecondaryInfo> sec_infos;
  EXPECT_TRUE(storage->loadSecondariesInfo(&sec_infos));

  ASSERT_EQ(sec_infos.size(), 2);
  EXPECT_EQ(sec_infos[0].serial.ToString(), "secondary_1");
  EXPECT_EQ(sec_infos[0].hw_id.ToString(), "secondary_hw");
  EXPECT_EQ(sec_infos[0].type, "ip");
  EXPECT_EQ(sec_infos[0].pub_key.Value(), "key1");
  EXPECT_EQ(sec_infos[0].pub_key.Type(), KeyType::kED25519);
  EXPECT_EQ(sec_infos[1].pub_key.Type(), KeyType::kUnknown);
  EXPECT_EQ(sec_infos[1].type, "");
  EXPECT_EQ(sec_infos[1].extra, "data2");

  // test update of data
  storage->saveSecondaryInfo(Uptane::EcuSerial("secondary_1"), "ip", PublicKey("key2", KeyType::kED25519));
  storage->saveSecondaryData(Uptane::EcuSerial("secondary_1"), "data1");
  EXPECT_TRUE(storage->loadSecondariesInfo(&sec_infos));

  ASSERT_EQ(sec_infos.size(), 2);
  EXPECT_EQ(sec_infos[0].pub_key.Value(), "key2");
  EXPECT_EQ(sec_infos[0].extra, "data1");
}

/* Import keys and credentials from file into storage.
 * Re-import updated credentials from file into storage.
 * Reject new certificate with a different device ID. */
TEST(StorageImport, ImportData) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());
  fs::create_directories(temp_dir / "import");

  ImportConfig import_config;
  import_config.base_path = temp_dir.Path() / "import";
  import_config.uptane_private_key_path = utils::BasedPath("private");
  import_config.uptane_public_key_path = utils::BasedPath("public");
  import_config.tls_cacert_path = utils::BasedPath("ca");
  import_config.tls_clientcert_path = utils::BasedPath("cert");
  import_config.tls_pkey_path = utils::BasedPath("pkey");

  std::string tls_cert_in1;
  std::string tls_pkey_in1;
  const std::string device_id1 = "test_id1";
  StructGuard<X509> certificate1 = Crypto::generateCert(1024, 365, "", "", "", device_id1, true);
  Crypto::serializeCert(&tls_pkey_in1, &tls_cert_in1, certificate1.get());

  Utils::writeFile(import_config.uptane_private_key_path.get(import_config.base_path).string(),
                   std::string("uptane_private_1"));
  Utils::writeFile(import_config.uptane_public_key_path.get(import_config.base_path).string(),
                   std::string("uptane_public_1"));
  Utils::writeFile(import_config.tls_cacert_path.get(import_config.base_path).string(), std::string("tls_cacert_1"));
  Utils::writeFile(import_config.tls_clientcert_path.get(import_config.base_path).string(), tls_cert_in1);
  Utils::writeFile(import_config.tls_pkey_path.get(import_config.base_path).string(), tls_pkey_in1);

  // Initially the storage is empty.
  EXPECT_FALSE(storage->loadPrimaryPublic(nullptr));
  EXPECT_FALSE(storage->loadPrimaryPrivate(nullptr));
  EXPECT_FALSE(storage->loadTlsCa(nullptr));
  EXPECT_FALSE(storage->loadTlsCert(nullptr));
  EXPECT_FALSE(storage->loadTlsPkey(nullptr));

  storage->importData(import_config);
  // Set the device ID to simulate initialization with the given certificate.
  storage->storeDeviceId(device_id1);

  std::string primary_public;
  std::string primary_private;
  std::string tls_ca;
  std::string tls_cert;
  std::string tls_pkey;

  // Verify that the data has been imported.
  EXPECT_TRUE(storage->loadPrimaryPublic(&primary_public));
  EXPECT_TRUE(storage->loadPrimaryPrivate(&primary_private));
  EXPECT_TRUE(storage->loadTlsCa(&tls_ca));
  EXPECT_TRUE(storage->loadTlsCert(&tls_cert));
  EXPECT_TRUE(storage->loadTlsPkey(&tls_pkey));

  EXPECT_EQ(primary_private, "uptane_private_1");
  EXPECT_EQ(primary_public, "uptane_public_1");
  EXPECT_EQ(tls_ca, "tls_cacert_1");
  EXPECT_EQ(tls_cert, tls_cert_in1);
  EXPECT_EQ(tls_pkey, tls_pkey_in1);

  // Create second TLS cert/key (with a different device ID) and other dummy
  // files.
  std::string tls_cert_in2;
  std::string tls_pkey_in2;
  const std::string device_id2 = "test_id2";
  StructGuard<X509> certificate2 = Crypto::generateCert(1024, 365, "", "", "", device_id2, true);
  Crypto::serializeCert(&tls_pkey_in2, &tls_cert_in2, certificate2.get());
  EXPECT_NE(tls_cert_in1, tls_cert_in2);
  EXPECT_NE(tls_pkey_in1, tls_pkey_in2);

  Utils::writeFile(import_config.uptane_private_key_path.get(import_config.base_path).string(),
                   std::string("uptane_private_2"));
  Utils::writeFile(import_config.uptane_public_key_path.get(import_config.base_path).string(),
                   std::string("uptane_public_2"));
  Utils::writeFile(import_config.tls_cacert_path.get(import_config.base_path).string(), std::string("tls_cacert_2"));
  Utils::writeFile(import_config.tls_clientcert_path.get(import_config.base_path).string(), tls_cert_in2);
  Utils::writeFile(import_config.tls_pkey_path.get(import_config.base_path).string(), tls_pkey_in2);

  // Attempt to re-import, TLS cert's device ID  changed. It allow reimport but keeps old device ID in the storage.
  EXPECT_NO_THROW(storage->importData(import_config));

  EXPECT_TRUE(storage->loadPrimaryPublic(&primary_public));
  EXPECT_TRUE(storage->loadPrimaryPrivate(&primary_private));
  EXPECT_TRUE(storage->loadTlsCa(&tls_ca));
  EXPECT_TRUE(storage->loadTlsCert(&tls_cert));
  EXPECT_TRUE(storage->loadTlsPkey(&tls_pkey));

  // Allow import but do not update primary keys.
  EXPECT_EQ(primary_private, "uptane_private_1");
  EXPECT_EQ(primary_public, "uptane_public_1");
  EXPECT_EQ(tls_ca, "tls_cacert_2");
  EXPECT_EQ(tls_cert, tls_cert_in2);
  EXPECT_EQ(tls_pkey, tls_pkey_in2);

  // Create third TLS cert/key (with the same device ID as the first) and other
  // dummy files.
  std::string tls_cert_in3;
  std::string tls_pkey_in3;
  StructGuard<X509> certificate3 = Crypto::generateCert(1024, 365, "", "", "", device_id1, true);
  Crypto::serializeCert(&tls_pkey_in3, &tls_cert_in3, certificate3.get());
  EXPECT_NE(tls_cert_in1, tls_cert_in3);
  EXPECT_NE(tls_pkey_in1, tls_pkey_in3);

  Utils::writeFile(import_config.tls_clientcert_path.get(import_config.base_path).string(), tls_cert_in3);
  Utils::writeFile(import_config.tls_pkey_path.get(import_config.base_path).string(), tls_pkey_in3);

  storage->importData(import_config);

  EXPECT_TRUE(storage->loadPrimaryPublic(&primary_public));
  EXPECT_TRUE(storage->loadPrimaryPrivate(&primary_private));
  EXPECT_TRUE(storage->loadTlsCa(&tls_ca));
  EXPECT_TRUE(storage->loadTlsCert(&tls_cert));
  EXPECT_TRUE(storage->loadTlsPkey(&tls_pkey));

  // All TLS objects should be updated exept primary keys.
  EXPECT_EQ(primary_private, "uptane_private_1");
  EXPECT_EQ(primary_public, "uptane_public_1");
  EXPECT_EQ(tls_ca, "tls_cacert_2");
  EXPECT_EQ(tls_cert, tls_cert_in3);
  EXPECT_EQ(tls_pkey, tls_pkey_in3);
}

TEST(StorageImport, ImportInitialRoot) {
  TemporaryDirectory temp_dir;
  std::unique_ptr<INvStorage> storage = Storage(temp_dir.Path());
  fs::create_directories(temp_dir / "import");

  ImportConfig import_config;
  import_config.base_path = temp_dir.Path() / "import";

  // Generate a set of valid Uptane root keys
  auto repo_path = temp_dir.Path() / "repo";
  Repo image_repo{Uptane::RepositoryType::Image(), repo_path, "", ""};
  image_repo.generateRepo();
  Repo director_repo{Uptane::RepositoryType::Director(), repo_path, "", ""};
  director_repo.generateRepo();
  director_repo.rotate(Uptane::Role::Root());

  EXPECT_FALSE(storage->loadLatestRoot(nullptr, Uptane::RepositoryType::Image()));
  EXPECT_FALSE(storage->loadLatestRoot(nullptr, Uptane::RepositoryType::Director()));

  fs::create_directories(import_config.base_path / "repo");
  fs::create_directories(import_config.base_path / "director");

  fs::copy(repo_path / "repo/repo/root.json", import_config.base_path / "repo/root.json");
  Utils::writeFile(import_config.base_path / "director/root.json", std::string("invalid"));

  storage->importData(import_config);
  EXPECT_TRUE(storage->loadLatestRoot(nullptr, Uptane::RepositoryType::Image()));
  EXPECT_FALSE(storage->loadLatestRoot(nullptr, Uptane::RepositoryType::Director()))
      << "Director root.json was invalid. It shouldn't have been imported";

// On Boost 1.85.0 enum copy_option (which was already deprecated) was replaced by
// copy_options, and the enumarator overwrite_if_exists was renamed as overwrite_existing.
// To keep compatibility with older versions of Boost (such as the one used by Yocto Kirkstone),
// we do this BOOST_VERSION check.
#if BOOST_VERSION >= 108500
  fs::copy_options overwrite_existing = fs::copy_options::overwrite_existing;
#else
  fs::copy_option overwrite_existing = fs::copy_option::overwrite_if_exists;
#endif

  // Copy the real director root.json over
  fs::copy_file(repo_path / "repo/director/root.json", import_config.base_path / "director/root.json",
                overwrite_existing);
  storage->importData(import_config);
  EXPECT_TRUE(storage->loadLatestRoot(nullptr, Uptane::RepositoryType::Director()));
}

#ifndef __NO_MAIN__
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  logger_init();
  logger_set_threshold(boost::log::trivial::trace);
  return RUN_ALL_TESTS();
}
#endif
