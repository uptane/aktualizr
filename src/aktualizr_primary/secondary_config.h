#ifndef SECONDARY_CONFIG_H_
#define SECONDARY_CONFIG_H_

#include <string>
#include <unordered_map>

#include <json/json.h>
#include <boost/filesystem.hpp>

#include "libaktualizr/types.h"
#include "primary/secondary_config.h"
#include "virtualsecondary.h"

#ifdef BUILD_GENERIC_SECONDARY
#include "torizongenericsecondary.h"
#endif

namespace Primary {

class IPSecondaryConfig {
 public:
  static constexpr const char* const AddrField{"addr"};
  static constexpr const char* const VerificationField{"verification_type"};

  IPSecondaryConfig(std::string addr_ip, uint16_t addr_port, VerificationType verification_type_in)
      : ip(std::move(addr_ip)), port(addr_port), verification_type(verification_type_in) {}

  friend std::ostream& operator<<(std::ostream& os, const IPSecondaryConfig& cfg) {
    os << "(addr: " << cfg.ip << ":" << cfg.port << " verification_type: " << cfg.verification_type << ")";
    return os;
  }

  const std::string ip;
  const uint16_t port;
  const VerificationType verification_type;
};

class IPSecondariesConfig : public SecondaryConfig {
 public:
  static constexpr const char* const Type{"IP"};
  static constexpr const char* const PortField{"secondaries_wait_port"};
  static constexpr const char* const TimeoutField{"secondaries_wait_timeout"};
  static constexpr const char* const SecondariesField{"secondaries"};

  IPSecondariesConfig(const uint16_t wait_port, const int timeout_s)
      : SecondaryConfig(Type), secondaries_wait_port{wait_port}, secondaries_timeout_s{timeout_s} {}

  friend std::ostream& operator<<(std::ostream& os, const IPSecondariesConfig& cfg) {
    os << "(wait_port: " << cfg.secondaries_wait_port << " timeout_s: " << cfg.secondaries_timeout_s << ")";
    return os;
  }

  const uint16_t secondaries_wait_port;
  const int secondaries_timeout_s;
  std::vector<IPSecondaryConfig> secondaries_cfg;
};

class SecondaryConfigParser {
 public:
  using Configs = std::vector<std::shared_ptr<SecondaryConfig>>;

  static Configs parse_config_file(const boost::filesystem::path& config_file);
  SecondaryConfigParser() = default;
  virtual ~SecondaryConfigParser() = default;
  SecondaryConfigParser(const SecondaryConfigParser&) = default;
  SecondaryConfigParser(SecondaryConfigParser&&) = default;
  SecondaryConfigParser& operator=(const SecondaryConfigParser&) = default;
  SecondaryConfigParser& operator=(SecondaryConfigParser&&) = default;

  virtual Configs parse() = 0;
};

class JsonConfigParser : public SecondaryConfigParser {
 public:
  explicit JsonConfigParser(const boost::filesystem::path& config_file);

  Configs parse() override;

 private:
  static void createIPSecondariesCfg(Configs& configs, const Json::Value& json_ip_sec_cfg);
  static void createVirtualSecondariesCfg(Configs& configs, const Json::Value& json_virtual_sec_cfg);
#ifdef BUILD_GENERIC_SECONDARY
  static void createTorizonGenericSecondariesCfg(Configs& configs, const Json::Value& json_torgen_sec_cfg);
#endif
  // add here a factory method for another type of secondary config

  using SecondaryConfigFactoryRegistry = std::unordered_map<std::string, std::function<void(Configs&, Json::Value&)>>;

  SecondaryConfigFactoryRegistry sec_cfg_factory_registry_ = {
      {IPSecondariesConfig::Type, createIPSecondariesCfg},
      {VirtualSecondaryConfig::Type, createVirtualSecondariesCfg},
#ifdef BUILD_GENERIC_SECONDARY
      {TorizonGenericSecondaryConfig::Type, createTorizonGenericSecondariesCfg},
#endif
      // add here factory method for another type of secondary config
  };

  Json::Value root_;
};

}  // namespace Primary

#endif  // SECONDARY_CONFIG_H_
