#include "sqlstorage.h"

#include <iostream>

#include <boost/move/make_unique.hpp>
#include <boost/scoped_array.hpp>

#include "logger.h"
#include "utils.h"

SQLStorage::SQLStorage(const StorageConfig& config) : config_(config) {
  // FSStorage
  boost::filesystem::create_directories(config_.path);
  boost::filesystem::create_directories(config_.path / config_.uptane_metadata_path / "repo");
  boost::filesystem::create_directories(config_.path / config_.uptane_metadata_path / "director");

  // SQLStorage
  if (!dbMigrate()) {
    LOGGER_LOG(LVL_error, "SQLite database migration failed");
    // Continue to run anyway, it can't be worse
  } else if (!dbCheck()) {
    LOGGER_LOG(LVL_error, "SQLite database doesn't match its schema");
  } else if (!dbInit()) {
    LOGGER_LOG(LVL_error, "Couldn't initialize database");
  }
}

SQLStorage::~SQLStorage() {
  // TODO: clear director_files, image_files
}

void SQLStorage::storePrimaryKeys(const std::string& public_key, const std::string& private_key) {
  storePrimaryPublic(public_key);
  storePrimaryPrivate(private_key);
}

void SQLStorage::storePrimaryPublic(const std::string& public_key) {
  boost::filesystem::path public_key_path = config_.path / config_.uptane_public_key_path;
  boost::filesystem::remove(public_key_path);
  Utils::writeFile(public_key_path.string(), public_key);

  sync();
}

void SQLStorage::storePrimaryPrivate(const std::string& private_key) {
  boost::filesystem::path private_key_path = config_.path / config_.uptane_private_key_path;
  boost::filesystem::remove(private_key_path);
  Utils::writeFile(private_key_path.string(), private_key);

  sync();
}

bool SQLStorage::loadPrimaryKeys(std::string* public_key, std::string* private_key) {
  return loadPrimaryPublic(public_key) && loadPrimaryPrivate(private_key);
}

bool SQLStorage::loadPrimaryPublic(std::string* public_key) {
  boost::filesystem::path public_key_path = config_.path / config_.uptane_public_key_path;

  if (!boost::filesystem::exists(public_key_path)) {
    return false;
  }

  if (public_key) *public_key = Utils::readFile(public_key_path.string());
  return true;
}

bool SQLStorage::loadPrimaryPrivate(std::string* private_key) {
  boost::filesystem::path private_key_path = config_.path / config_.uptane_private_key_path;

  if (!boost::filesystem::exists(private_key_path)) {
    return false;
  }

  if (private_key) *private_key = Utils::readFile(private_key_path.string());
  return true;
}

void SQLStorage::clearPrimaryKeys() {
  boost::filesystem::remove(config_.path / config_.uptane_public_key_path);
  boost::filesystem::remove(config_.path / config_.uptane_private_key_path);
}

void SQLStorage::storeTlsCreds(const std::string& ca, const std::string& cert, const std::string& pkey) {
  storeTlsCa(ca);
  storeTlsCert(cert);
  storeTlsPkey(pkey);
}

void SQLStorage::storeTlsCa(const std::string& ca) {
  boost::filesystem::path ca_path(config_.path / config_.tls_cacert_path);
  boost::filesystem::remove(ca_path);
  Utils::writeFile(ca_path.string(), ca);
  sync();
}

void SQLStorage::storeTlsCert(const std::string& cert) {
  boost::filesystem::path cert_path(config_.path / config_.tls_clientcert_path);
  boost::filesystem::remove(cert_path);
  Utils::writeFile(cert_path.string(), cert);
  sync();
}

void SQLStorage::storeTlsPkey(const std::string& pkey) {
  boost::filesystem::path pkey_path(config_.path / config_.tls_pkey_path);
  boost::filesystem::remove(pkey_path);
  Utils::writeFile(pkey_path.string(), pkey);
  sync();
}

bool SQLStorage::loadTlsCreds(std::string* ca, std::string* cert, std::string* pkey) {
  boost::filesystem::path ca_path(config_.path / config_.tls_cacert_path);
  boost::filesystem::path cert_path(config_.path / config_.tls_clientcert_path);
  boost::filesystem::path pkey_path(config_.path / config_.tls_pkey_path);
  if (!boost::filesystem::exists(ca_path) || boost::filesystem::is_directory(ca_path) ||
      !boost::filesystem::exists(cert_path) || boost::filesystem::is_directory(cert_path) ||
      !boost::filesystem::exists(pkey_path) || boost::filesystem::is_directory(pkey_path)) {
    return false;
  }
  if (ca) {
    *ca = Utils::readFile(ca_path.string());
  }
  if (cert) {
    *cert = Utils::readFile(cert_path.string());
  }
  if (pkey) {
    *pkey = Utils::readFile(pkey_path.string());
  }
  return true;
}

void SQLStorage::clearTlsCreds() {
  boost::filesystem::remove(config_.path / config_.tls_cacert_path);
  boost::filesystem::remove(config_.path / config_.tls_clientcert_path);
  boost::filesystem::remove(config_.path / config_.tls_pkey_path);
}

bool SQLStorage::loadTlsCommon(std::string* data, const boost::filesystem::path& path_in) {
  boost::filesystem::path path(config_.path / path_in);
  if (!boost::filesystem::exists(path)) return false;

  if (data) *data = Utils::readFile(path.string());

  return true;
}

bool SQLStorage::loadTlsCa(std::string* ca) { return loadTlsCommon(ca, config_.tls_cacert_path); }

bool SQLStorage::loadTlsCert(std::string* cert) { return loadTlsCommon(cert, config_.tls_clientcert_path); }

bool SQLStorage::loadTlsPkey(std::string* pkey) { return loadTlsCommon(pkey, config_.tls_pkey_path); }

#ifdef BUILD_OSTREE
void SQLStorage::storeMetadata(const Uptane::MetaPack& metadata) {
  boost::filesystem::path image_path = config_.path / config_.uptane_metadata_path / "repo";
  boost::filesystem::path director_path = config_.path / config_.uptane_metadata_path / "director";

  Utils::writeFile((director_path / "root.json").string(), metadata.director_root.toJson());
  Utils::writeFile((director_path / "targets.json").string(), metadata.director_targets.toJson());
  Utils::writeFile((image_path / "root.json").string(), metadata.image_root.toJson());
  Utils::writeFile((image_path / "targets.json").string(), metadata.image_targets.toJson());
  Utils::writeFile((image_path / "timestamp.json").string(), metadata.image_timestamp.toJson());
  Utils::writeFile((image_path / "snapshot.json").string(), metadata.image_snapshot.toJson());
  sync();
}

bool SQLStorage::loadMetadata(Uptane::MetaPack* metadata) {
  boost::filesystem::path image_path = config_.path / config_.uptane_metadata_path / "repo";
  boost::filesystem::path director_path = config_.path / config_.uptane_metadata_path / "director";

  if (!boost::filesystem::exists(director_path / "root.json") ||
      !boost::filesystem::exists(director_path / "targets.json") ||
      !boost::filesystem::exists(image_path / "root.json") || !boost::filesystem::exists(image_path / "targets.json") ||
      !boost::filesystem::exists(image_path / "timestamp.json") ||
      !boost::filesystem::exists(image_path / "snapshot.json"))
    return false;

  Json::Value json = Utils::parseJSONFile(director_path / "root.json");
  // compatibility with old clients, which store the whole metadata, not just the signed part
  if (json.isMember("signed") && json.isMember("signatures")) json = json["signed"];
  metadata->director_root = Uptane::Root("director", json);

  json = Utils::parseJSONFile(director_path / "targets.json");
  // compatibility with old clients, which store the whole metadata, not just the signed part
  if (json.isMember("signed") && json.isMember("signatures")) json = json["signed"];
  metadata->director_targets = Uptane::Targets(json);

  json = Utils::parseJSONFile(image_path / "root.json");
  // compatibility with old clients, which store the whole metadata, not just the signed part
  if (json.isMember("signed") && json.isMember("signatures")) json = json["signed"];
  metadata->image_root = Uptane::Root("image", json);

  json = Utils::parseJSONFile(image_path / "targets.json");
  // compatibility with old clients, which store the whole metadata, not just the signed part
  if (json.isMember("signed") && json.isMember("signatures")) json = json["signed"];
  metadata->image_targets = Uptane::Targets(json);

  json = Utils::parseJSONFile(image_path / "timestamp.json");
  // compatibility with old clients, which store the whole metadata, not just the signed part
  if (json.isMember("signed") && json.isMember("signatures")) json = json["signed"];
  metadata->image_timestamp = Uptane::TimestampMeta(json);

  json = Utils::parseJSONFile(image_path / "snapshot.json");
  // compatibility with old clients, which store the whole metadata, not just the signed part
  if (json.isMember("signed") && json.isMember("signatures")) json = json["signed"];
  metadata->image_snapshot = Uptane::Snapshot(json);

  return true;
}
#endif  // BUILD_OSTREE

void SQLStorage::storeDeviceId(const std::string& device_id) {
  SQLite3Guard db(config_.sqldb_path.string().c_str());

  if (db.get_rc() != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't open database: " << sqlite3_errmsg(db.get()));
    return;
  }

  std::string req = "UPDATE OR REPLACE device_info SET device_id = '";
  req += device_id + "';";
  if (sqlite3_exec(db.get(), req.c_str(), NULL, NULL, NULL) != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't set device ID: " << sqlite3_errmsg(db.get()));
    return;
  }
}

bool SQLStorage::loadDeviceId(std::string* device_id) {
  SQLite3Guard db(config_.sqldb_path.string().c_str());

  if (db.get_rc() != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't open database: " << sqlite3_errmsg(db.get()));
    return "";
  }

  request = kSqlGetSimple;
  req_response.clear();
  if (sqlite3_exec(db.get(), "SELECT device_id FROM device_info LIMIT 1;", callback, this, NULL) != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't get device ID: " << sqlite3_errmsg(db.get()));
    return false;
  }

  if (req_response.find("result") == req_response.end()) return false;

  if (device_id) *device_id = req_response["result"];

  return true;
}

void SQLStorage::clearDeviceId() {
  SQLite3Guard db(config_.sqldb_path.string().c_str());

  if (db.get_rc() != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't open database: " << sqlite3_errmsg(db.get()));
    return;
  }

  if (sqlite3_exec(db.get(), "UPDATE OR REPLACE device_info SET device_id = NULL;", NULL, NULL, NULL) != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't clear device ID: " << sqlite3_errmsg(db.get()));
    return;
  }
}

void SQLStorage::storeEcuRegistered() { Utils::writeFile((config_.path / "is_registered").string(), std::string("1")); }

bool SQLStorage::loadEcuRegistered() { return boost::filesystem::exists((config_.path / "is_registered").string()); }

void SQLStorage::clearEcuRegistered() { boost::filesystem::remove(config_.path / "is_registered"); }

void SQLStorage::storeEcuSerials(const std::vector<std::pair<std::string, std::string> >& serials) {
  if (serials.size() >= 1) {
    Utils::writeFile((config_.path / "primary_ecu_serial").string(), serials[0].first);
    Utils::writeFile((config_.path / "primary_ecu_hardware_id").string(), serials[0].second);

    boost::filesystem::remove_all((config_.path / "secondaries_list"));
    std::vector<std::pair<std::string, std::string> >::const_iterator it;
    std::ofstream file((config_.path / "secondaries_list").string().c_str());
    for (it = serials.begin() + 1; it != serials.end(); it++)
      // Assuming that there are no tabs and linebreaks in serials and hardware ids
      file << it->first << "\t" << it->second << "\n";
    file.close();
  }
}

bool SQLStorage::loadEcuSerials(std::vector<std::pair<std::string, std::string> >* serials) {
  std::string buf;
  std::string serial;
  std::string hw_id;
  if (!boost::filesystem::exists((config_.path / "primary_ecu_serial"))) return false;
  serial = Utils::readFile((config_.path / "primary_ecu_serial").string());
  // use default hardware ID for backwards compatibility
  if (!boost::filesystem::exists((config_.path / "primary_ecu_hardware_id")))
    hw_id = Utils::getHostname();
  else
    hw_id = Utils::readFile((config_.path / "primary_ecu_hardware_id").string());

  if (serials) serials->push_back(std::pair<std::string, std::string>(serial, hw_id));

  // return true for backwards compatibility
  if (!boost::filesystem::exists((config_.path / "secondaries_list"))) {
    return true;
  }
  std::ifstream file((config_.path / "secondaries_list").string().c_str());
  while (std::getline(file, buf)) {
    size_t tab = buf.find('\t');
    serial = buf.substr(0, tab);
    try {
      hw_id = buf.substr(tab + 1);
    } catch (const std::out_of_range& e) {
      if (serials) serials->clear();
      file.close();
      return false;
    }
    if (serials) serials->push_back(std::pair<std::string, std::string>(serial, hw_id));
  }
  file.close();
  return true;
}

void SQLStorage::clearEcuSerials() {
  boost::filesystem::remove(config_.path / "primary_ecu_serial");
  boost::filesystem::remove(config_.path / "primary_hardware_id");
  boost::filesystem::remove(config_.path / "secondaries_list");
}

void SQLStorage::storeInstalledVersions(const std::string& content) {
  Utils::writeFile((config_.path / "installed_versions").string(), content);
}

bool SQLStorage::loadInstalledVersions(std::string* content) {
  if (!boost::filesystem::exists(config_.path / "installed_versions")) return false;
  *content = Utils::readFile((config_.path / "installed_versions").string());
  return true;
}

bool SQLStorage::tableSchemasEqual(const std::string& left, const std::string& right) {
  boost::char_separator<char> sep(" \"\t\r\n", "(),;");
  sql_tokenizer tokl(left, sep);
  sql_tokenizer tokr(right, sep);

  sql_tokenizer::iterator it_l;
  sql_tokenizer::iterator it_r;
  for (it_l = tokl.begin(), it_r = tokr.begin(); it_l != tokl.end() && it_r != tokr.end(); ++it_l, ++it_r) {
    if (*it_l != *it_r) return false;
  }
  return (it_l == tokl.end()) && (it_r == tokr.end());
}

boost::movelib::unique_ptr<std::map<std::string, std::string> > SQLStorage::parseSchema(int version) {
  boost::filesystem::path schema_file =
      config_.schemas_path / (std::string("schema.") + boost::lexical_cast<std::string>(version) + ".sql");
  std::string schema = Utils::readFile(schema_file.string());
  boost::movelib::unique_ptr<std::map<std::string, std::string> > result =
      boost::movelib::make_unique<std::map<std::string, std::string> >();
  std::vector<std::string> tokens;

  enum { STATE_INIT, STATE_CREATE, STATE_TABLE, STATE_NAME };
  boost::char_separator<char> sep(" \"\t\r\n", "(),;");
  sql_tokenizer tok(schema, sep);
  int parsing_state = STATE_INIT;

  std::string key;
  std::string value;
  for (sql_tokenizer::iterator it = tok.begin(); it != tok.end(); ++it) {
    std::string token = *it;
    if (value.empty())
      value = token;
    else
      value = value + " " + token;
    switch (parsing_state) {
      case STATE_INIT:
        if (token != "CREATE") {
          return boost::movelib::make_unique<std::map<std::string, std::string> >();
        }
        parsing_state = STATE_CREATE;
        break;
      case STATE_CREATE:
        if (token != "TABLE") {
          return boost::movelib::make_unique<std::map<std::string, std::string> >();
        }
        parsing_state = STATE_TABLE;
        break;
      case STATE_TABLE:
        if (token == "(" || token == ")" || token == "," || token == ";") {
          return boost::movelib::make_unique<std::map<std::string, std::string> >();
        }
        key = token;
        parsing_state = STATE_NAME;
        break;
      case STATE_NAME:
        if (token == ";") {
          (*result)[key] = value;
          key.clear();
          value.clear();
          parsing_state = STATE_INIT;
        }
        break;
    }
  }
  return boost::move(result);
}

std::string SQLStorage::getTableSchemaFromDb(const std::string& tablename) {
  SQLite3Guard db(config_.sqldb_path.string().c_str());

  if (db.get_rc() != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't open database: " << sqlite3_errmsg(db.get()));
    return "";
  }

  request = kSqlGetSimple;
  req_response.clear();
  std::string req =
      std::string("SELECT sql FROM sqlite_master WHERE type='table' AND tbl_name='") + tablename + "' LIMIT 1;";
  if (sqlite3_exec(db.get(), req.c_str(), callback, this, NULL) != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't get schema of " << tablename << ": " << sqlite3_errmsg(db.get()));
    return "";
  }

  return req_response["result"] + ";";
}

bool SQLStorage::dbMigrate() {
  SQLite3Guard db(config_.sqldb_path.string().c_str());

  if (db.get_rc() != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't open database: " << sqlite3_errmsg(db.get()));
    return false;
  }

  int schema_version = getVersion();
  if (schema_version == kSqlSchemaVersion) {
    return true;
  }

  for (; schema_version <= kSqlSchemaVersion; schema_version++) {
    boost::filesystem::path migrate_script_path =
        config_.schemas_path / (std::string("migrate.") + boost::lexical_cast<std::string>(schema_version) + ".sql");
    std::string req = Utils::readFile(migrate_script_path.string());

    if (sqlite3_exec(db.get(), req.c_str(), NULL, NULL, NULL) != SQLITE_OK) {
      LOGGER_LOG(LVL_error, "Can't migrate db from version" << (schema_version - 1) << " to version " << schema_version
                                                            << ": " << sqlite3_errmsg(db.get()));
      return false;
    }
  }

  return true;
}

bool SQLStorage::dbCheck() {
  boost::movelib::unique_ptr<std::map<std::string, std::string> > tables = parseSchema(kSqlSchemaVersion);

  for (std::map<std::string, std::string>::iterator it = tables->begin(); it != tables->end(); ++it) {
    std::string schema_from_db = getTableSchemaFromDb(it->first);
    if (!tableSchemasEqual(schema_from_db, it->second)) {
      LOGGER_LOG(LVL_error, "Schemas don't match for " << it->first);
      LOGGER_LOG(LVL_error, "Expected " << it->second);
      LOGGER_LOG(LVL_error, "Found " << schema_from_db);
      return false;
    }
  }
  return true;
}

bool SQLStorage::dbInit() {
  SQLite3Guard db(config_.sqldb_path.string().c_str());

  if (sqlite3_exec(db.get(), "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't begin transaction: " << sqlite3_errmsg(db.get()));
    return false;
  }

  request = kSqlGetSimple;
  req_response.clear();
  if (sqlite3_exec(db.get(), "SELECT count(*) FROM device_info;", callback, this, NULL) != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't get number of rows in device_info: " << sqlite3_errmsg(db.get()));
    return false;
  }

  if (boost::lexical_cast<int>(req_response["result"]) < 1) {
    if (sqlite3_exec(db.get(), "INSERT INTO device_info DEFAULT VALUES;", NULL, NULL, NULL) != SQLITE_OK) {
      LOGGER_LOG(LVL_error, "Can't set default values to device_info: " << sqlite3_errmsg(db.get()));
      return false;
    }
  }

  if (sqlite3_exec(db.get(), "COMMIT TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't commit transaction: " << sqlite3_errmsg(db.get()));
    return false;
  }
  return true;
}

int SQLStorage::getVersion() {
  SQLite3Guard db(config_.sqldb_path.string().c_str());

  if (db.get_rc() != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't open database: " << sqlite3_errmsg(db.get()));
    return -1;
  }

  request = kSqlGetSimple;
  req_response.clear();
  std::string req = std::string("SELECT version FROM version LIMIT 1;");
  if (sqlite3_exec(db.get(), req.c_str(), callback, this, NULL) != SQLITE_OK) {
    LOGGER_LOG(LVL_error, "Can't get database version: " << sqlite3_errmsg(db.get()));
    return -1;
  }

  try {
    return boost::lexical_cast<int>(req_response["result"]);
  } catch (const boost::bad_lexical_cast&) {
    return -1;
  }
}

int SQLStorage::callback(void* instance_, int numcolumns, char** values, char** columns) {
  SQLStorage* instance = static_cast<SQLStorage*>(instance_);

  switch (instance->request) {
    case kSqlGetSimple: {
      (void)numcolumns;
      (void)columns;
      if (values[0]) instance->req_response["result"] = values[0];
      break;
    }
    default:
      return -1;
  }
  return 0;
}