#include "libaktualizr/results.h"

namespace result {

using result::DownloadStatus;
using result::UpdateStatus;

std::ostream& operator<<(std::ostream& os, UpdateStatus update_status) {
  switch (update_status) {
    case UpdateStatus::kUpdatesAvailable:
      os << "Updates Available";
      break;
    case UpdateStatus::kNoUpdatesAvailable:
      os << "No Updates Available";
      break;
    case UpdateStatus::kError:
      os << "Update Error";
      break;
    default:
      os << "Unknown UpdateStatus(" << static_cast<int>(update_status) << ")";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const DownloadStatus stat) {
  os << "\"";
  switch (stat) {
    case DownloadStatus::kSuccess:
      os << "Success";
      break;
    case DownloadStatus::kPartialSuccess:
      os << "Partial success";
      break;
    case DownloadStatus::kNothingToDownload:
      os << "Nothing to download";
      break;
    case DownloadStatus::kError:
      os << "Error";
      break;
    default:
      os << "unknown";
      break;
  }
  os << "\"";
  return os;
}

}  // namespace result