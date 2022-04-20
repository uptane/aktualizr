#ifndef AKTUALIZR_SECONDARY_CONFIG_H_
#define AKTUALIZR_SECONDARY_CONFIG_H_

#include <netinet/in.h>                             // for in_port_t
#include <boost/filesystem/path.hpp>                // for path
#include <boost/program_options/variables_map.hpp>  // for variables_map
#include <boost/property_tree/ptree_fwd.hpp>        // for ptree
#include <iosfwd>                                   // for ostream
#include <string>                                   // for string

#include "libaktualizr/config.h"  // for BaseConfig, Bootl...
#include "libaktualizr/types.h"   // for CryptoSource, Key...

// Try to keep the order of config options the same as in
// AktualizrSecondaryConfig::writeToStream() and
// AktualizrSecondaryConfig::updateFromPropertyTree().

struct AktualizrSecondaryNetConfig {
  in_port_t port{9030};
  std::string primary_ip;
  in_port_t primary_port{9030};

  void updateFromPropertyTree(const boost::property_tree::ptree& pt);
  void writeToStream(std::ostream& out_stream) const;
};

struct AktualizrSecondaryUptaneConfig {
  std::string ecu_serial;
  std::string ecu_hardware_id;
  CryptoSource key_source{CryptoSource::kFile};
  KeyType key_type{KeyType::kRSA2048};
  bool force_install_completion{false};
  VerificationType verification_type{VerificationType::kFull};

  void updateFromPropertyTree(const boost::property_tree::ptree& pt);
  void writeToStream(std::ostream& out_stream) const;
};

class AktualizrSecondaryConfig : public BaseConfig {
 public:
  AktualizrSecondaryConfig() = default;
  explicit AktualizrSecondaryConfig(const boost::program_options::variables_map& cmd);
  explicit AktualizrSecondaryConfig(const boost::filesystem::path& filename);

  KeyManagerConfig keymanagerConfig() const;

  void postUpdateValues();
  void writeToStream(std::ostream& sink) const;

  // from Primary config
  LoggerConfig logger;

  AktualizrSecondaryNetConfig network;
  AktualizrSecondaryUptaneConfig uptane;

  // from Primary config
  P11Config p11;
  PackageConfig pacman;
  BootloaderConfig bootloader;
  StorageConfig storage;
  ImportConfig import;

 private:
  void updateFromCommandLine(const boost::program_options::variables_map& cmd);
  void updateFromPropertyTree(const boost::property_tree::ptree& pt) override;
};
std::ostream& operator<<(std::ostream& os, const AktualizrSecondaryConfig& cfg);

#endif  // AKTUALIZR_SECONDARY_CONFIG_H_
