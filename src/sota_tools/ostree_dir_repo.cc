#include "ostree_dir_repo.h"

#include <string>

#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "logging/logging.h"

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

bool OSTreeDirRepo::LooksValid() const {
  fs::path objects_dir(root_ / "/objects");
  fs::path refs_dir(root_ / "/refs");
  fs::path config_file(root_ / "/config");
  // On Boost 1.85.0 function is_regular (that was already deprecated), was finally replaced
  // by is_regular_file. To keep compatibility with older versions of Boost (such as the one
  // used by Yocto Kirkstone), we do this BOOST_VERSION check.
#if BOOST_VERSION >= 108500
  bool is_config_file_regular_file = fs::is_regular_file(config_file);
#else
  bool is_config_file_regular_file = fs::is_regular(config_file);
#endif
  if (fs::is_directory(objects_dir) && fs::is_directory(refs_dir) && is_config_file_regular_file) {
    pt::ptree config;
    try {
      pt::read_ini(config_file.string(), config);
      if (config.get<std::string>("core.mode") != "archive-z2") {
        LOG_WARNING << "OSTree repo is not in archive-z2 format";
        return false;
      }
      return true;

    } catch (const pt::ini_parser_error &error) {
      LOG_WARNING << "Couldn't parse OSTree config file: " << config_file;
      return false;
    } catch (const pt::ptree_error &error) {
      LOG_WARNING << "Could not find core.mode in OSTree config file";
      return false;
    }
  } else {
    return false;
  }
}

OSTreeRef OSTreeDirRepo::GetRef(const std::string &refname) const { return OSTreeRef(*this, refname); }

bool OSTreeDirRepo::FetchObject(const boost::filesystem::path &path) const {
  return fs::is_regular_file((root_ / path).string());
}

// vim: set tabstop=2 shiftwidth=2 expandtab:
