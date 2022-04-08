#ifndef INITIALIZER_H_
#define INITIALIZER_H_

#include <string>

#include "libaktualizr/secondaryinterface.h"

#include "crypto/keymanager.h"
#include "http/httpinterface.h"
#include "libaktualizr/config.h"
#include "storage/invstorage.h"
#include "uptane/tuf.h"

class Provisioner {
 public:
  enum class State {
    kUnknown = 0,
    kOk,
    kTemporaryError,
    // Note there is no 'Permanent' error here, because all the failure modes we have so far may recover
  };

  /**
   * Provisioner gets the local system, represented by config, storage, key_manager and secondaries properly registered
   * on the server (represented by http_client). The constructor doesn't do any work. Calling bool Attempt() will make
   * one provisioning attempt (if necessary) and return true if provisioning is done.
   */
  Provisioner(const ProvisionConfig& config, std::shared_ptr<INvStorage> storage,
              std::shared_ptr<HttpInterface> http_client, std::shared_ptr<KeyManager> key_manager,
              const std::map<Uptane::EcuSerial, std::shared_ptr<SecondaryInterface> >& secondaries);

  /**
   * Notify Provisioner that the secondaries passed in via the constructor have
   * changed.
   * This will revert the provisioning state, so that Attempt() will cause
   * provisioning to be attempted again.
   */
  void SecondariesWereChanged();

  /**
   * Perform as much of provisioning as is possible without contacting a
   * remote server. Secondaries are still contacted over the local networking.
   * This is safe to call redundantly.
   */
  void Prepare();

  /**
   * Make one attempt at provisioning, if the provisioning hasn't already completed.
   * If provisioning is already successful this is a no-op.
   * use like:
   * if (!provisioner_.Attempt()) {
   *    return error;
   * }
   * // Provisioned. Carry on as normal
   * @returns whether the device is provisioned
   */
  bool Attempt();

  State CurrentState() const { return current_state_; }

  /**
   * A textual description of the last cause for provisioning to fail.
   */
  std::string LastError() const { return last_error_; };

  /**
   * Is is CurrentState() either kUnknown or kTemporaryError?
   * To keep trying until provisioning succeeds or the retry count is hit, do:
   * <code>while(provisioner.ShouldAttemptAgain()) { provisioner.MakeAttempt(); }</code>
   * @return
   */
  bool ShouldAttemptAgain() const;

  /**
   * Get the ECU Serial for the Primary, lazily creating and storing it if necessary
   */
  Uptane::EcuSerial PrimaryEcuSerial();

  /**
   * Get the Hardware Identifier for the Primary, lazily creating and storing it if necessary
   */
  Uptane::HardwareIdentifier PrimaryHardwareIdentifier();

  /**
   * Get the Device ID for this vehicle, lazily creating and storing it if necessary.
   * One Device ID covers a set of ECUs.
   * @return The Device ID
   */
  std::string DeviceId();

  /**
   * Get ECU serials and corresponding hardware IDs; this prioritizes the data stored
   * into non-volatile storage and falls back to returning the current (volatile) list
   * of ECU serials.
   * @return Array of (ECU serial, hardware ID) pairs
   */
  bool GetEcuSerials(EcuSerials* serials) const;

 private:
  class Error : public std::runtime_error {
   public:
    explicit Error(const std::string& what) : std::runtime_error(std::string("Initializer error: ") + what) {}
  };

  class KeyGenerationError : public Error {
   public:
    explicit KeyGenerationError(const std::string& what)
        : Error(std::string("Could not generate Uptane key pair: ") + what) {}
  };

  class StorageError : public Error {
   public:
    explicit StorageError(const std::string& what) : Error(std::string("Storage error: ") + what) {}
  };

  class ServerError : public Error {
   public:
    explicit ServerError(const std::string& what) : Error(std::string("Server error: ") + what) {}
  };

  class ServerOccupied : public Error {
   public:
    ServerOccupied() : Error("device ID is already registered") {}
  };

  class EcuCompare {
   public:
    explicit EcuCompare(std::pair<Uptane::EcuSerial, Uptane::HardwareIdentifier> ecu_in)
        : serial(std::move(ecu_in.first)), hardware_id(std::move(ecu_in.second)) {}
    bool operator()(const std::pair<Uptane::EcuSerial, Uptane::HardwareIdentifier>& in) const {
      return (in.first == serial && in.second == hardware_id);
    }

   private:
    const Uptane::EcuSerial serial;
    const Uptane::HardwareIdentifier hardware_id;
  };

  /**
   * Requires an Uptane key pair
   * Failure modes:
   *  - Can't contact secondaries
   */
  void initEcuSerials();

  /**
   * Failure modes:
   *  -  Can't contact secondaries
   */
  void initSecondaryInfo();

  /**
   * Failure modes:
   *  - Can't contact server / offline
   */
  void initTlsCreds();

  /**
   * Update http_client_ with the TLS certs from key_manager_
   * @return Whether the keys were available and loaded
   */
  bool loadSetTlsCreds();

  /**
   * Registers the ECUs with the server.
   * Stores ECU information locally
   *
   * Failure modes:
   *   - Can't contact server / offline
   */
  void initEcuRegister();

  /**
   * Initializes the 'ecu_report_counter' table to zero
   * Requires the ECU serials are setup
   * Requires the ECUs are registered on the server
   */
  void initEcuReportCounter();

  const ProvisionConfig& config_;
  std::shared_ptr<INvStorage> storage_;
  std::shared_ptr<HttpInterface> http_client_;
  std::shared_ptr<KeyManager> key_manager_;
  // Lazily initialized by DeviceId()
  std::string device_id_;
  // Lazily initialized by PrimaryEcuSerial()
  Uptane::EcuSerial primary_ecu_serial_{Uptane::EcuSerial::Unknown()};
  // Lazily initialized by PrimaryHardwareIdentifier()
  Uptane::HardwareIdentifier primary_ecu_hardware_id_{Uptane::HardwareIdentifier::Unknown()};
  const std::map<Uptane::EcuSerial, SecondaryInterface::Ptr>& secondaries_;
  std::vector<SecondaryInfo> sec_info_;
  EcuSerials new_ecu_serials_;
  bool register_ecus_{false};
  State current_state_{State::kUnknown};
  std::string last_error_;
};

#endif  // INITIALIZER_H_
