#include "provisioner.h"

#include <string>

#include <openssl/bio.h>
#include <boost/scoped_array.hpp>

#include "bootstrap/bootstrap.h"
#include "crypto/keymanager.h"
#include "logging/logging.h"

using std::map;
using std::move;
using std::shared_ptr;

Provisioner::Provisioner(const ProvisionConfig& config, shared_ptr<INvStorage> storage,
                         shared_ptr<HttpInterface> http_client, shared_ptr<KeyManager> key_manager,
                         const map<Uptane::EcuSerial, shared_ptr<SecondaryInterface>>& secondaries)
    : config_(config),
      storage_(move(storage)),
      http_client_(move(http_client)),
      key_manager_(move(key_manager)),
      secondaries_(secondaries) {}

void Provisioner::SecondariesWereChanged() { current_state_ = State::kUnknown; }

void Provisioner::Prepare() {
  initEcuSerials();
  initSecondaryInfo();
}

bool Provisioner::Attempt() {
  try {
    Prepare();
    try {
      initTlsCreds();
    } catch (const ServerOccupied& e) {
      // if a device with the same ID has already been registered to the server,
      // generate a new one
      storage_->clearDeviceId();
      device_id_.clear();
      LOG_ERROR << "Device name is already registered. Retrying.";
      throw;
    }

    initEcuRegister();

    initEcuReportCounter();

    current_state_ = State::kOk;
    return true;
  } catch (const Provisioner::Error& ex) {
    last_error_ = ex.what();
    current_state_ = State::kTemporaryError;
    return false;
  } catch (const std::exception& ex) {
    LOG_DEBUG << "Provisioner::Attempt() caught an exception not deriving from Provisioner::Error";
    last_error_ = ex.what();
    current_state_ = State::kTemporaryError;
    return false;
  }
}

bool Provisioner::ShouldAttemptAgain() const {
  return current_state_ == State::kUnknown || current_state_ == State::kTemporaryError;
}

Uptane::EcuSerial Provisioner::PrimaryEcuSerial() {
  if (primary_ecu_serial_ != Uptane::EcuSerial::Unknown()) {
    return primary_ecu_serial_;
  }

  std::string key_pair;
  try {
    // If the key pair already exists, this loads it from storage.
    key_pair = key_manager_->generateUptaneKeyPair();
  } catch (const std::exception& e) {
    throw KeyGenerationError(e.what());
  }

  if (key_pair.empty()) {
    throw KeyGenerationError("Unknown error");
  }

  std::string primary_ecu_serial_str = config_.primary_ecu_serial;
  if (primary_ecu_serial_str.empty()) {
    primary_ecu_serial_str = key_manager_->UptanePublicKey().KeyId();
  }
  primary_ecu_serial_ = Uptane::EcuSerial(primary_ecu_serial_str);

  // assert that the new serial is sane
  if (primary_ecu_serial_ == Uptane::EcuSerial::Unknown()) {
    throw std::logic_error("primary_ecu_serial_ is still Unknown");
  }

  return primary_ecu_serial_;
}

Uptane::HardwareIdentifier Provisioner::PrimaryHardwareIdentifier() {
  if (primary_ecu_hardware_id_ != Uptane::HardwareIdentifier::Unknown()) {
    return primary_ecu_hardware_id_;
  }
  std::string primary_ecu_hardware_id_str = config_.primary_ecu_hardware_id;
  if (primary_ecu_hardware_id_str.empty()) {
    primary_ecu_hardware_id_str = Utils::getHostname();
    if (primary_ecu_hardware_id_str.empty()) {
      throw Error("Could not get current host name, please configure an hardware ID explicitly");
    }
  }

  primary_ecu_hardware_id_ = Uptane::HardwareIdentifier(primary_ecu_hardware_id_str);

  // Assert the new value is sane
  if (primary_ecu_hardware_id_ == Uptane::HardwareIdentifier::Unknown()) {
    throw std::logic_error("primary_ecu_hardware_id_ is still Unknown");
  }

  return primary_ecu_hardware_id_;
}

std::string Provisioner::DeviceId() {
  if (device_id_.empty()) {
    // Try loading it
    storage_->loadDeviceId(&device_id_);
  }

  if (!device_id_.empty()) {
    return device_id_;
  }

  LOG_WARNING << "No device ID yet...";
  // If device_id is specified in the config, use that.
  device_id_ = config_.device_id;
  if (device_id_.empty()) {
    LOG_WARNING << "device_id is empty... generating";
    // Otherwise, try to read the device certificate if it is available.
    try {
      device_id_ = key_manager_->getCN();
    } catch (const std::exception& e) {
      // No certificate: for device credential provisioning, abort. For shared
      // credential provisioning, generate a random name.
      if (config_.mode == ProvisionMode::kSharedCred || config_.mode == ProvisionMode::kSharedCredReuse) {
        device_id_ = Utils::genPrettyName();
      } else if (config_.mode == ProvisionMode::kDeviceCred) {
        throw e;
      } else {
        throw Error("Unknown provisioning method");
      }
    }
  }
  if (!device_id_.empty()) {
    storage_->storeDeviceId(device_id_);
  }
  return device_id_;
}

bool Provisioner::loadSetTlsCreds() {
  key_manager_->copyCertsToCurl(*http_client_);
  return key_manager_->isOk();
}

// Postcondition:
//  - TLS credentials are in the storage
//  - This device_id is provisioned on the device gateway
void Provisioner::initTlsCreds() {
  if (loadSetTlsCreds()) {
    return;
  }

  if (config_.mode == ProvisionMode::kDeviceCred) {
    throw StorageError("Device credentials expected but not found");
  }

  // Shared credential provisioning is required and possible => (automatically)
  // provision with shared credentials.

  // Set bootstrap (shared) credentials.
  Bootstrap boot(config_.provision_path, config_.p12_password);
  http_client_->setCerts(boot.getCa(), CryptoSource::kFile, boot.getCert(), CryptoSource::kFile, boot.getPkey(),
                         CryptoSource::kFile);

  Json::Value data;
  data["deviceId"] = DeviceId();
  data["ttl"] = config_.expiry_days;
  HttpResponse response = http_client_->post(config_.server + "/devices", data);
  if (!response.isOk()) {
    Json::Value resp_code;
    try {
      resp_code = response.getJson()["code"];
    } catch (const std::exception& ex) {
      LOG_ERROR << "Unable to parse response code from device registration: " << ex.what();
      throw ServerError(ex.what());
    }
    if (resp_code.isString() && resp_code.asString() == "device_already_registered") {
      LOG_ERROR << "Device ID " << DeviceId() << " is already registered.";
      throw ServerOccupied();
    }
    const auto err = std::string("Shared credential provisioning failed: ") +
                     std::to_string(response.http_status_code) + " " + response.body;
    throw ServerError(err);
  }

  std::string pkey;
  std::string cert;
  std::string ca;
  StructGuard<BIO> device_p12(BIO_new_mem_buf(response.body.c_str(), static_cast<int>(response.body.size())),
                              BIO_vfree);
  if (!Crypto::parseP12(device_p12.get(), "", &pkey, &cert, &ca)) {
    throw ServerError("Received malformed device credentials from the server");
  }
  storage_->storeTlsCreds(ca, cert, pkey);

  // Set provisioned (device) credentials.
  if (!loadSetTlsCreds()) {
    throw Error("Failed to configure HTTP client with device credentials.");
  }

  if (config_.mode != ProvisionMode::kSharedCredReuse) {
    // Remove shared provisioning credentials from the archive; we have no more
    // use for them.
    Utils::removeFileFromArchive(config_.provision_path, "autoprov_credentials.p12");
    // Remove the treehub.json if it's still there. It shouldn't have been put on
    // the device, but it has happened before.
    try {
      Utils::removeFileFromArchive(config_.provision_path, "treehub.json");
    } catch (...) {
    }
  }

  LOG_INFO << "Provisioned successfully on Device Gateway.";
}

// Postcondition [(serial, hw_id)] is in the storage
void Provisioner::initEcuSerials() {
  EcuSerials stored_ecu_serials;
  storage_->loadEcuSerials(&stored_ecu_serials);

  new_ecu_serials_.clear();
  new_ecu_serials_.emplace_back(PrimaryEcuSerial(), PrimaryHardwareIdentifier());
  for (const auto& s : secondaries_) {
    new_ecu_serials_.emplace_back(s.first, s.second->getHwId());
  }

#ifdef BUILD_OFFLINE_UPDATES
  // TODO: Review this idea.
  //
  // Here we are "stashing" the ECU for use by the offline-update logic which
  // requires this information to map hardware IDs into ECU serials; Such
  // information is being taken from the INvStorage class at the moment but
  // in the future we should consider taking it from somewhere else or ensure
  // that the information is actually in the storage.
  //
  // NOTE: The code following this seems to consider `new_ecu_serials_` as
  // the source of truth for the current list of ECUs - confirm this.
  storage_->stashEcuSerialsForHwId(new_ecu_serials_);
#endif

  register_ecus_ = stored_ecu_serials.empty();
  if (!register_ecus_) {
    // We should probably clear the misconfigured_ecus table once we have
    // consent working.
    std::vector<bool> found(stored_ecu_serials.size(), false);

    EcuCompare primary_comp(new_ecu_serials_[0]);
    EcuSerials::const_iterator store_it;
    store_it = std::find_if(stored_ecu_serials.cbegin(), stored_ecu_serials.cend(), primary_comp);
    if (store_it == stored_ecu_serials.cend()) {
      LOG_INFO << "Configured Primary ECU serial " << new_ecu_serials_[0].first << " with hardware ID "
               << new_ecu_serials_[0].second << " not found in storage.";
      register_ecus_ = true;
    } else {
      found[static_cast<size_t>(store_it - stored_ecu_serials.cbegin())] = true;
    }

    // Check all configured Secondaries to see if any are new.
    for (auto it = secondaries_.cbegin(); it != secondaries_.cend(); ++it) {
      EcuCompare secondary_comp(std::make_pair(it->second->getSerial(), it->second->getHwId()));
      store_it = std::find_if(stored_ecu_serials.cbegin(), stored_ecu_serials.cend(), secondary_comp);
      if (store_it == stored_ecu_serials.cend()) {
        LOG_INFO << "Configured Secondary ECU serial " << it->second->getSerial() << " with hardware ID "
                 << it->second->getHwId() << " not found in storage.";
        register_ecus_ = true;
      } else {
        found[static_cast<size_t>(store_it - stored_ecu_serials.cbegin())] = true;
      }
    }

    // Check all stored Secondaries not already matched to see if any have been
    // removed. Store them in a separate table to keep track of them.
    std::vector<bool>::iterator found_it;
    for (found_it = found.begin(); found_it != found.end(); ++found_it) {
      if (!*found_it) {
        auto not_registered = stored_ecu_serials[static_cast<size_t>(found_it - found.begin())];
        LOG_INFO << "ECU serial " << not_registered.first << " with hardware ID " << not_registered.second
                 << " in storage was not found in Secondary configuration.";
        register_ecus_ = true;
        storage_->saveMisconfiguredEcu({not_registered.first, not_registered.second, EcuState::kOld});
      }
    }
  }
}

bool Provisioner::GetEcuSerials(EcuSerials* serials) const {
  // TODO: Prioritizing data from non-volatile storage for now; review this later.
#ifdef BUILD_OFFLINE_UPDATES
  return (storage_->loadEcuSerials(serials) || storage_->getEcuSerialsForHwId(serials));
#else
  return storage_->loadEcuSerials(serials))
#endif
}

void Provisioner::initSecondaryInfo() {
  sec_info_.clear();
  for (const auto& s : secondaries_) {
    const Uptane::EcuSerial serial = s.first;
    SecondaryInterface& sec = *s.second;

    SecondaryInfo info;
    // If upgrading from the older version of the storage without the
    // secondary_ecus table, we need to migrate the data. This should be done
    // regardless of whether we need to (re-)register the ECUs.
    // The ECU serials should be already initialized by this point.
    if (!storage_->loadSecondaryInfo(serial, &info) || info.type.empty() || info.pub_key.Type() == KeyType::kUnknown) {
      info.serial = serial;
      info.hw_id = sec.getHwId();
      info.type = sec.Type();
      const PublicKey& p = sec.getPublicKey();
      if (p.Type() != KeyType::kUnknown) {
        info.pub_key = p;
      }
      // If we don't need to register the ECUs, we still need to store this info
      // to complete the migration.
      if (!register_ecus_) {
        storage_->saveSecondaryInfo(info.serial, info.type, info.pub_key);
      }
    }
    // We will need this info later if the device is not yet provisioned
    sec_info_.push_back(std::move(info));
  }
}

// Postcondition: "ECUs registered" flag set in the storage
void Provisioner::initEcuRegister() {
  // Allow re-registration if the ECUs have changed.
  if (!register_ecus_) {
    LOG_DEBUG << "All ECUs are already registered with the server.";
    return;
  }

  PublicKey uptane_public_key = key_manager_->UptanePublicKey();

  if (uptane_public_key.Type() == KeyType::kUnknown) {
    throw StorageError("Invalid key in storage");
  }

  Json::Value all_ecus;
  all_ecus["primary_ecu_serial"] = new_ecu_serials_[0].first.ToString();
  all_ecus["ecus"] = Json::arrayValue;
  {
    Json::Value primary_ecu;
    primary_ecu["hardware_identifier"] = new_ecu_serials_[0].second.ToString();
    primary_ecu["ecu_serial"] = new_ecu_serials_[0].first.ToString();
    primary_ecu["clientKey"] = key_manager_->UptanePublicKey().ToUptane();
    all_ecus["ecus"].append(primary_ecu);
  }

  for (const auto& info : sec_info_) {
    Json::Value ecu;
    ecu["hardware_identifier"] = info.hw_id.ToString();
    ecu["ecu_serial"] = info.serial.ToString();
    ecu["clientKey"] = info.pub_key.ToUptane();
    all_ecus["ecus"].append(ecu);
  }

  HttpResponse response = http_client_->post(config_.ecu_registration_endpoint, all_ecus);
  if (!response.isOk()) {
    Json::Value resp_code = response.getJson()["code"];
    if (resp_code.isString() &&
        (resp_code.asString() == "ecu_already_registered" || resp_code.asString() == "device_already_registered")) {
      throw ServerError("One or more ECUs are unexpectedly already registered");
    }
    const auto err =
        std::string("Error registering device: ") + std::to_string(response.http_status_code) + " " + response.body;
    throw ServerError(err);
  }

  // TODO: [OFFUPD] Should we remove this block?
  // Only store the changes if we successfully registered the ECUs.
  LOG_DEBUG << "Storing " << new_ecu_serials_.size() << " ECU serials (after registering)";
  storage_->storeEcuSerials(new_ecu_serials_);
  for (const auto& info : sec_info_) {
    storage_->saveSecondaryInfo(info.serial, info.type, info.pub_key);
  }
  // Create a DeviceId if it hasn't been done already. This is necessary
  // because storeDeviceId() resets the is_registered flag and storeEcuRegistered()
  // requires there to be a DeviceID in the device_info table already.
  DeviceId();
  storage_->storeEcuRegistered();

  LOG_INFO << "ECUs have been successfully registered with the server.";
}

void Provisioner::initEcuReportCounter() {
  std::vector<std::pair<Uptane::EcuSerial, int64_t>> ecu_cnt;

  if (storage_->loadEcuReportCounter(&ecu_cnt)) {
    return;
  }

  EcuSerials ecu_serials;

  if (!storage_->loadEcuSerials(&ecu_serials) || ecu_serials.empty()) {
    throw Error("Could not load ECU serials");
  }

  storage_->saveEcuReportCounter(Uptane::EcuSerial(ecu_serials[0].first.ToString()), 0);
}
