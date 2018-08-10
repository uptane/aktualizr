#include "isotpsecondary.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>

#define LIBUPTINY_ISOTP_PRIMARY_CANID 0x7D8

enum class IsoTpUptaneMesType{
  kGetSerial = 0x01,
  kGetSerialResp = 0x41,
  kGetHwId = 0x02,
  kGetHwIdResp = 0x42,
  kGetPkey = 0x03,
  kGetPkeyResp = 0x43,
  kGetRootVer = 0x04,
  kGetRootVerResp = 0x44,
  kGetManifest = 0x05,
  kGetManifestResp = 0x45,
  kPutRoot = 0x06,
  kPutTargets = 0x07,
  kPutImageChunk = 0x08,
  kPutImageChunkAckErr = 0x48,
};

namespace Uptane {

IsoTpSecondary::IsoTpSecondary(const SecondaryConfig& sconfig_in) : SecondaryInterface(sconfig_in), conn(sconfig.can_iface, LIBUPTINY_ISOTP_PRIMARY_CANID, sconfig_in.can_id) {}

EcuSerial IsoTpSecondary::getSerial() {
  std::string out;
  std::string in;

  out += static_cast<char>(IsoTpUptaneMesType::kGetSerial);
  if(!conn.SendRecv(out, &in)) {
    return EcuSerial::Unknown();
  }
  
  if(in[0] != static_cast<char>(IsoTpUptaneMesType::kGetSerialResp)) {
    return EcuSerial::Unknown();
  }
  return EcuSerial(in.substr(1));
}

HardwareIdentifier IsoTpSecondary::getHwId() {
  std::string out;
  std::string in;

  out += static_cast<char>(IsoTpUptaneMesType::kGetHwId);
  if(!conn.SendRecv(out, &in)) {
    return HardwareIdentifier::Unknown();
  }
  
  if(in[0] != static_cast<char>(IsoTpUptaneMesType::kGetHwIdResp)) {
    return HardwareIdentifier::Unknown();
  }
  return HardwareIdentifier(in.substr(1));
}

PublicKey IsoTpSecondary::getPublicKey() {
  std::string out;
  std::string in;

  out += static_cast<char>(IsoTpUptaneMesType::kGetPkey);
  if(!conn.SendRecv(out, &in)) {
    return PublicKey("", KeyType::kUnknown);
  }
  
  if(in[0] != static_cast<char>(IsoTpUptaneMesType::kGetPkeyResp)) {
    return PublicKey("", KeyType::kUnknown);
  }
  return PublicKey(in.substr(1), KeyType::kED25519);
}

Json::Value IsoTpSecondary::getManifest() {
  std::string out;
  std::string in;

  out += static_cast<char>(IsoTpUptaneMesType::kGetManifest);
  if(!conn.SendRecv(out, &in)) {
    return Json::nullValue;
  }

  if(in[0] != static_cast<char>(IsoTpUptaneMesType::kGetManifestResp)) {
    return Json::nullValue;
  }
  return Utils::parseJSON(in.substr(1));
}


}
