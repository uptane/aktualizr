#include <array>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "libaktualizr/types.h"
#include "utilities/utils.h"

std::ostream &operator<<(std::ostream &os, const StorageType stype) {
  std::string stype_str;
  switch (stype) {
    case StorageType::kFileSystem:
      stype_str = "filesystem";
      break;
    case StorageType::kSqlite:
      stype_str = "sqlite";
      break;
    default:
      stype_str = "unknown";
      break;
  }
  os << '"' << stype_str << '"';
  return os;
}

std::ostream &Uptane::operator<<(std::ostream &os, const HardwareIdentifier &hwid) {
  os << hwid.ToString();
  return os;
}

std::ostream &Uptane::operator<<(std::ostream &os, const EcuSerial &ecu_serial) {
  os << ecu_serial.ToString();
  return os;
}

std::ostream &operator<<(std::ostream &os, const BootedType btype) {
  std::string btype_str;
  switch (btype) {
    case BootedType::kStaged:
      btype_str = "staged";
      break;
    default:
      btype_str = "booted";
      break;
  }
  os << '"' << btype_str << '"';
  return os;
}

std::ostream &operator<<(std::ostream &os, VerificationType vtype) {
  const std::string type_s = Uptane::VerificationTypeToString(vtype);
  os << '"' << type_s << '"';
  return os;
}

std::string TimeToString(struct tm time) {
  std::array<char, 22> formatted{};
  strftime(formatted.data(), 22, "%Y-%m-%dT%H:%M:%SZ", &time);
  return std::string(formatted.data());
}

TimeStamp TimeStamp::Now() { return TimeStamp(CurrentTime()); }

struct tm TimeStamp::CurrentTime() {
  time_t raw_time;
  struct tm time_struct{};
  time(&raw_time);
  gmtime_r(&raw_time, &time_struct);

  return time_struct;
}

TimeStamp::TimeStamp(std::string rfc3339) {
  if (rfc3339.length() != 20 || rfc3339[19] != 'Z') {
    throw TimeStamp::InvalidTimeStamp();
  }
  time_ = rfc3339;
}

TimeStamp::TimeStamp(struct tm time) : TimeStamp(TimeToString(time)) {}

bool TimeStamp::IsValid() const { return time_.length() != 0; }

bool TimeStamp::IsExpiredAt(const TimeStamp &now) const {
  if (!IsValid()) {
    return true;
  }
  if (!now.IsValid()) {
    return true;
  }
  return *this < now;
}

bool TimeStamp::operator<(const TimeStamp &other) const { return IsValid() && other.IsValid() && time_ < other.time_; }

bool TimeStamp::operator>(const TimeStamp &other) const { return (other < *this); }

std::ostream &operator<<(std::ostream &os, const TimeStamp &t) {
  os << t.time_;
  return os;
}

namespace data {

const std::map<data::ResultCode::Numeric, const char *> data::ResultCode::string_repr{
    {ResultCode::Numeric::kOk, "OK"},
    {ResultCode::Numeric::kAlreadyProcessed, "ALREADY_PROCESSED"},
    {ResultCode::Numeric::kVerificationFailed, "VERIFICATION_FAILED"},
    {ResultCode::Numeric::kInstallFailed, "INSTALL_FAILED"},
    {ResultCode::Numeric::kDownloadFailed, "DOWNLOAD_FAILED"},
    {ResultCode::Numeric::kInternalError, "INTERNAL_ERROR"},
    {ResultCode::Numeric::kGeneralError, "GENERAL_ERROR"},
    {ResultCode::Numeric::kNeedCompletion, "NEED_COMPLETION"},
    {ResultCode::Numeric::kCustomError, "CUSTOM_ERROR"},
    {ResultCode::Numeric::kOperationCancelled, "OPERATION_CANCELLED"},
    {ResultCode::Numeric::kUnknown, "UNKNOWN"},
};

std::string data::ResultCode::toRepr() const {
  std::string s = ToString();

  if (s.find('\"') != std::string::npos) {
    throw std::runtime_error("Result code cannot contain double quotes");
  }

  return "\"" + s + "\"" + ":" + std::to_string(static_cast<int>(num_code));
}

ResultCode data::ResultCode::fromRepr(const std::string &repr) {
  size_t quote_n = repr.find('"');
  std::string s;
  size_t col_n;

  if (quote_n < repr.size() - 1) {
    size_t end_quote_n = repr.find('"', quote_n + 1);
    col_n = repr.find(':', end_quote_n + 1);
    s = repr.substr(quote_n + 1, end_quote_n - quote_n - 1);
  } else {
    // legacy format
    col_n = repr.find(':');
    s = repr.substr(0, col_n);
  }

  if (col_n >= repr.size() - 1) {
    return ResultCode(Numeric::kUnknown, s);
  }

  int num = std::stoi(repr.substr(col_n + 1));

  return ResultCode(static_cast<Numeric>(num), s);
}

Json::Value InstallationResult::toJson() const {
  Json::Value json;
  json["success"] = success;
  json["code"] = result_code.ToString();
  json["description"] = description;
  return json;
}

std::ostream &operator<<(std::ostream &os, const ResultCode &result_code) {
  os << result_code.toRepr();
  return os;
}

}  // namespace data

boost::filesystem::path utils::BasedPath::get(const boost::filesystem::path &base) const {
  // note: BasedPath(bp.get() == bp)
  return Utils::absolutePath(base, p_);
}

// vim: set tabstop=2 shiftwidth=2 expandtab:
