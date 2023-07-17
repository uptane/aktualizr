#include "dockerofflineloader.h"
#include "crypto/crypto.h"
#include "dockertarballloader.h"
#include "logging/logging.h"
#include "utilities/utils.h"

#include <sys/utsname.h>
#include <algorithm>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <regex>
#include <vector>

#include <fcntl.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

static const std::string SHA256_PREFIX = "sha256:";
static const std::string JSON_EXT = ".json";
static const std::string TAR_EXT = ".tar";

// Maximum size of a manifest file.
static constexpr std::size_t MAX_MANIFEST_FILE_SIZE_BYTES = 256 * 1024;

// Limits on the compose file.
static constexpr std::size_t MAX_COMPOSE_LINE_SIZE_BYTES = 4096;
static constexpr std::size_t MAX_COMPOSE_FILE_SIZE_BYTES = 4 * 1024 * 1024;

// ---
// Global definitions
// ---

/**
 * Assert-like function with a message.
 * TODO: Make into a macro or function template.
 */
static void ensure(bool cond, const std::string &message) {
  if (!cond) {
    throw std::runtime_error(message.c_str());
  }
}

static void ensure(bool cond, const char *message) {
  if (!cond) {
    throw std::runtime_error(message);
  }
}

bool loadManifest(const std::string &req_digest, const boost::filesystem::path &manifests_dir, Json::Value &target) {
  // Open manifest file and check its size.
  const boost::filesystem::path fname(manifests_dir / (req_digest + JSON_EXT));
  std::ifstream input(fname.string(), std::ios::binary);
  if (!input) {
    LOG_WARNING << "Could not open manifest " << fname;
    return false;
  }

  boost::system::error_code errcode;
  const uintmax_t orglen = boost::filesystem::file_size(fname, errcode);
  // Get file size should always succeed at this point.
  ensure(errcode == boost::system::errc::success, "Cannot determine manifest size");
  if (orglen > MAX_MANIFEST_FILE_SIZE_BYTES) {
    LOG_WARNING << "Manifest file " << fname << " is too big";
    return false;
  }

  // Load manifest file into memory.
  using ManifestBufferType = std::vector<uint8_t>;
  auto buffer = std_::make_unique<ManifestBufferType>(orglen + 1);

  input.read(reinterpret_cast<char *>(buffer->data()), static_cast<std::streamsize>(buffer->size()));
  const auto len = static_cast<uintmax_t>(input.gcount());
  ensure(len == orglen, "Manifest file changed size");

  // Determine the file's digest and make sure it's correct.
  MultiPartSHA256Hasher hasher;
  hasher.update(buffer->data(), static_cast<uint64_t>(len));
  std::string real_digest = boost::algorithm::to_lower_copy(hasher.getHexDigest());

  if (req_digest != real_digest) {
    LOG_WARNING << "Wrong digest of manifest " << fname;
    return false;
  }

  // Encapsulate data into stream.
  std::string _source(reinterpret_cast<char *>(buffer->data()), len);
  std::istringstream source(_source);
  // Parse contents.
  Json::CharReaderBuilder builder;
  Json::String errs;
  bool status = Json::parseFromStream(builder, source, &target, &errs);
  if (!status) {
    LOG_WARNING << "Parsing failed for manifest " << fname;
    return false;
  }

  return true;
}

bool platformMatches(const std::string &plat1, const std::string &plat2, unsigned *grade) {
  // TODO: Determine if there are defined rules for how to compare platforms.
  //       e.g. can we say that linux/arm/v7 encompasses linux/arm/v6?

  std::string _plat1 = plat1;
  std::string _plat2 = plat2;

  // Remove slash at the end (if any).
  if (boost::ends_with(_plat1, "/")) {
    _plat1.erase(_plat1.size() - 1);
  }
  if (boost::ends_with(_plat2, "/")) {
    _plat2.erase(_plat2.size() - 1);
  }

  std::list<std::string> plat1_lst;
  std::list<std::string> plat2_lst;
  boost::split(plat1_lst, _plat1, std::bind1st(std::equal_to<char>(), '/'));
  boost::split(plat2_lst, _plat2, std::bind1st(std::equal_to<char>(), '/'));

  bool match = true;
  unsigned _grade = 0;

  for (auto it1 = plat1_lst.begin(), it2 = plat2_lst.begin();; it1++, it2++) {
    if (it1 == plat1_lst.end() || it2 == plat2_lst.end()) {
      break;
    }
    if (*it1 != *it2) {
      match = false;
      break;
    }
    _grade++;
  }

  if (grade != nullptr) {
    *grade = _grade;
  }

  // LOG_INFO << "platformMatches(\"" << plat1 << "\", \"" << plat2 << "\")"
  //          << " = " << match << ", grade: " << _grade;

  return match;
}

std::string getDockerPlatform() {
  const char *envplat = getenv("DOCKER_DEFAULT_PLATFORM");
  if (envplat != nullptr) {
    return std::string(envplat);
  }

  struct utsname uinfo {};
  ensure(uname(&uinfo) == 0, "Cannot get system information");
  std::string sysname(uinfo.sysname);
  ensure(sysname == "Linux", "Only Linux is supported");

  std::string machine(uinfo.machine);
  std::string platform;
  // See https://stackoverflow.com/questions/45125516/possible-values-for-uname-m.
  if (boost::starts_with(machine, "armv7")) {
    platform = "linux/arm/v7";
  } else if (boost::starts_with(machine, "aarch64") || boost::starts_with(machine, "armv8")) {
    platform = "linux/arm64";
  } else if (machine == "x86_64") {
    platform = "linux/amd64";
  } else {
    ensure(false, "Unknown machine '" + machine + "' in getDockerPlatform()");
  }

  return platform;
}

void splitDigestFromName(const std::string &name, std::string *name_nodigest, std::string *digest, bool removePrefix) {
  // Make sure prefix is present.
  std::size_t pos = name.find(SHA256_PREFIX);
  ensure(pos != std::string::npos, "Image name '" + name + "' not specified by digest");

  // Get image name without digest.
  if (name_nodigest != nullptr) {
    ensure(pos > 1, "Bad format of image name '" + name + "'");
    *name_nodigest = name.substr(0, pos - 1);
  }

  // Get the digest (with or without prefix).
  if (digest != nullptr) {
    if (removePrefix) {
      *digest = name.substr(pos + SHA256_PREFIX.length());
    } else {
      *digest = name.substr(pos);
    }
  }
}

std::string removeDigestPrefix(const std::string &digest) {
  std::string _digest = digest;
  if (boost::starts_with(_digest, SHA256_PREFIX)) {
    _digest = _digest.substr(SHA256_PREFIX.length());
  }
  return _digest;
}

static const std::string &na_if_empty(const std::string &str) {
  static const std::string na_str = "N/A";
  if (str.empty()) {
    return na_str;
  }
  return str;
}

// ---
// LargeTemporaryDirectory (replacement for TemporaryDirectory).
// ---

/**
 * Replacement for `TemporaryDirectory` in libaktualizr that uses /var/tmp/ as
 * preferred directory for storing temporary files.
 *
 * TODO: In TorizonCore using /var/tmp/ does not solve the problem since
 *       even that directory is a tmpfs.
 * TODO: Move to a specific module.
 */
class LargeTemporaryDirectory {
 public:
  explicit LargeTemporaryDirectory(const std::string &hint = "dir");
  LargeTemporaryDirectory(const LargeTemporaryDirectory &) = delete;
  LargeTemporaryDirectory(LargeTemporaryDirectory &&) = delete;
  LargeTemporaryDirectory &operator=(const LargeTemporaryDirectory &) = delete;
  LargeTemporaryDirectory &operator=(LargeTemporaryDirectory &&) = delete;
  ~LargeTemporaryDirectory();
  boost::filesystem::path Path() const { return tmp_name_; }
  std::string PathString() const { return Path().string(); }
  boost::filesystem::path operator/(const boost::filesystem::path &subdir) const { return (tmp_name_ / subdir); }

 private:
  boost::filesystem::path tmp_name_;
};

boost::filesystem::path get_large_tmp_dir() {
  static const boost::filesystem::path large_tmp_dir{"/var/tmp/"};
  if (boost::filesystem::exists(large_tmp_dir) && boost::filesystem::is_directory(large_tmp_dir)) {
    LOG_TRACE << "Temporary directory set to " << large_tmp_dir;
    return large_tmp_dir;
  }
  // Fall back to boost's selected temporary directory.
  return boost::filesystem::temp_directory_path();
}

LargeTemporaryDirectory::LargeTemporaryDirectory(const std::string &hint)
    : tmp_name_(get_large_tmp_dir() / boost::filesystem::unique_path(std::string("%%%%-%%%%-").append(hint))) {
  Utils::createDirectories(tmp_name_, S_IRWXU);
}

LargeTemporaryDirectory::~LargeTemporaryDirectory() {
  LOG_TRACE << "Removing directory " << tmp_name_;
  boost::filesystem::remove_all(tmp_name_);
}

// ---
// DockerManifestWrapper class
// ---

const std::string DockerManifestWrapper::MEDIA_TYPE::SINGLE_PLAT =
    "application/vnd.docker.distribution.manifest.v2+json";
const std::string DockerManifestWrapper::MEDIA_TYPE::MULTI_PLAT =
    "application/vnd.docker.distribution.manifest.list.v2+json";

void DockerManifestWrapper::findBestPlatform(const std::string &req_platform, std::string *sel_platform,
                                             std::string *sel_digest) const {
  ensure(isMultiPlatform(), "findBestPlatform: multi-platform manifest expected");

  struct ManInfo {
    unsigned grade_;
    std::string digest_;
    std::string platform_;
    ManInfo(unsigned grade, std::string digest, std::string platform)
        : grade_(grade), digest_(std::move(digest)), platform_(std::move(platform)) {}
  };

  // Go over all manifests in the manifest list.
  std::vector<ManInfo> grade_digest_pairs;
  for (auto man : manifest_["manifests"]) {
    std::string man_platform = platformString(man["platform"]);
    unsigned grade;
    if (platformMatches(req_platform, man_platform, &grade)) {
      const ManInfo info(grade, man["digest"].asString(), man_platform);
      grade_digest_pairs.push_back(info);
    }
  }

  ensure(!grade_digest_pairs.empty(), "There are no images appropriate for platform " + req_platform);

  if (grade_digest_pairs.size() >= 2) {
    // Sort in decreasing order of grades.
    // LOG_DEBUG << "Sorting " << grade_digest_pairs.size() << " elements";
    std::sort(grade_digest_pairs.begin(), grade_digest_pairs.end(),
              [](const ManInfo &a, const ManInfo &b) { return a.grade_ > b.grade_; });
    ensure(grade_digest_pairs[0].grade_ > grade_digest_pairs[1].grade_,
           "There are multiple images appropriate for platform " + req_platform);
  }

  if (sel_platform != nullptr) {
    *sel_platform = grade_digest_pairs[0].platform_;
  }
  if (sel_digest != nullptr) {
    *sel_digest = grade_digest_pairs[0].digest_;
  }
}

std::string DockerManifestWrapper::getConfigDigest(bool removePrefix) const {
  ensure(isSinglePlatform(), "getConfigDigest: single-platform manifest expected");
  std::string digest = manifest_["config"]["digest"].asString();
  if (removePrefix) {
    digest = removeDigestPrefix(digest);
  }
  return digest;
}

std::string DockerManifestWrapper::platformString(const Json::Value &plat) {
  ensure(plat.isMember("os") && plat.isMember("architecture"), "Bad platform spec in manifest");

  std::string platform = plat["os"].asString();
  platform += "/";
  platform += plat["architecture"].asString();

  if (plat.isMember("variant")) {
    platform += "/";
    platform += plat["variant"].asString();
  }

  if (plat.isMember("os.version")) {
    platform += "/";
    platform += plat["os.version"].asString();
  }

  return platform;
}

std::string DockerManifestWrapper::getMediaType() const {
  ensure(manifest_.isMember("mediaType"), "Undefined manifest type");
  return manifest_["mediaType"].asString();
}

// ---
// OCIManifestWrapper class
// ---

const std::string OCIManifestWrapper::MEDIA_TYPE::SINGLE_PLAT = "application/vnd.oci.image.manifest.v1+json";
const std::string OCIManifestWrapper::MEDIA_TYPE::MULTI_PLAT = "application/vnd.oci.image.index.v1+json";

// ---
// makeManifestWrapper: factory for objects of class DockerManifestWrapper and derived.
// ---

DockerManifestWrapper *makeManifestWrapper(Json::Value manifest) {
  ensure(manifest.isMember("mediaType"), "Manifest does not have required 'mediaType' field");

  const std::string mediaType = manifest["mediaType"].asString();
  if ((mediaType == DockerManifestWrapper::MEDIA_TYPE::SINGLE_PLAT) ||
      (mediaType == DockerManifestWrapper::MEDIA_TYPE::MULTI_PLAT)) {
    // LOG_DEBUG << "Creating DockerManifestWrapper";
    return new DockerManifestWrapper(manifest);
  }
  if ((mediaType == OCIManifestWrapper::MEDIA_TYPE::SINGLE_PLAT) ||
      (mediaType == OCIManifestWrapper::MEDIA_TYPE::MULTI_PLAT)) {
    // LOG_DEBUG << "Creating OCIManifestWrapper";
    return new OCIManifestWrapper(manifest);
  }

  LOG_WARNING << "Manifest has unknown 'mediaType' of '" << mediaType << "'";
  ensure(false, "Manifest has unknown 'mediaType'");

  return nullptr;
}

// ---
// DockerManifestsCache class
// ---

DockerManifestsCache::ManifestSharedPtr DockerManifestsCache::loadByDigest(const std::string &digest) {
  // Get digest without the sha256 prefix.
  std::string digest_nopref = removeDigestPrefix(digest);
  ensure(digest_nopref.length() == 64, "Bad digest format");

  // Try to find manifest in cache first.
  auto dit = manifests_cache_.find(digest_nopref);
  if (dit != manifests_cache_.end()) {
    LOG_TRACE << "cache: hit for manifest with digest " << digest_nopref;
    // Update access index and return it.
    ManifestCacheElem &cache_elem = dit->second;
    cache_elem.first = ++access_counter_;
    return cache_elem.second;
  }

  // Not in cache: try to load it.
  Json::Value manifest_json;
  ensure(loadManifest(digest_nopref, manifests_dir_, manifest_json),
         "Cannot load manifest with digest " + digest_nopref);

  // Store into cache.
  auto manifest_ptr = ManifestSharedPtr(makeManifestWrapper(manifest_json));
  ManifestCacheElem manifest_elem{++access_counter_, manifest_ptr};
  LOG_TRACE << "cache: load manifest with digest " << digest_nopref;
  manifests_cache_.insert({digest_nopref, manifest_elem});

  // Remove elements if desired size exceeded (next loop should run 0 or 1 time).
  while (manifests_cache_.size() > max_manifests_) {
    // Find LRU entry.
    auto delit = manifests_cache_.end();
    size_t delit_counter = std::numeric_limits<size_t>::max();
    for (auto it = manifests_cache_.begin(); it != manifests_cache_.end(); it++) {
      ManifestCacheElem &cache_elem = it->second;
      if (cache_elem.first < delit_counter) {
        delit = it;
        delit_counter = cache_elem.first;
      }
    }
    if (delit == manifests_cache_.end()) {
      break;
    }
    // Remove LRU entry.
    LOG_TRACE << "cache: discard entry with digest " << delit->first;
    manifests_cache_.erase(delit);
  }

  return manifest_ptr;
}

// ---
// DockerComposeFile class
// ---

// Strings and regexes for basic parsing of a docker-compose file.
const std::string DockerComposeFile::services_section_name{"services"};
const std::string DockerComposeFile::offline_mode_header{"# mode=offline"};
const std::string DockerComposeFile::image_tag{"image"};
const std::string DockerComposeFile::image_tag_old{"x-old-image"};

// NOLINTNEXTLINE(modernize-raw-string-literal)
const std::regex DockerComposeFile::offline_mode_header_re{"^#.*\\bmode=offline\\b.*\\s*$"};
// NOLINTNEXTLINE(modernize-raw-string-literal)
const std::regex DockerComposeFile::level1_key_re{"^([-._a-zA-Z0-9]+):\\s*$"};
// NOLINTNEXTLINE(modernize-raw-string-literal)
const std::regex DockerComposeFile::level2_key_re{"^  ([-._a-zA-Z0-9]+):\\s*$"};
// NOLINTNEXTLINE(modernize-raw-string-literal)
const std::regex DockerComposeFile::image_name_re{"^    (image):\\s*(\"?)(\\S+)(\\2)\\s*$"};
// NOLINTNEXTLINE(modernize-raw-string-literal)
const std::regex DockerComposeFile::image_name_old_re{"^    (x-old-image):\\s*(\"?)(\\S+)(\\2)\\s*$"};
// NOLINTNEXTLINE(modernize-raw-string-literal)
const std::regex DockerComposeFile::plat_name_re{"^    (?:platform):\\s*(\"?)(\\S+)(\\1)\\s*$"};

/**
 * Special version of getline() that reads text from input including the
 * newline character.
 */
static bool raw_getline(std::istream &input, std::string &line, std::size_t maxcnt, char delim = '\n') {
  line.clear();
  if (!input.good()) {
    return false;
  }

  char ch;
  std::size_t cnt = 0;
  while (input.get(ch)) {
    cnt++;
    if (cnt > maxcnt) {
      throw std::runtime_error("Line too long");
    }
    line += ch;
    if (ch == delim) {
      break;
    }
  }

  return true;
}

DockerComposeFile::DockerComposeFile(const boost::filesystem::path &compose_path) { read(compose_path); }

bool DockerComposeFile::read(const boost::filesystem::path &compose_path) {
  compose_lines_.clear();

  // Open file in binary mode so that line breaks are preserved.
  std::ifstream input(compose_path.string(), std::ios::binary);
  if (!input) {
    LOG_WARNING << "Could not open compose-file " << compose_path;
    return false;
  }

  ComposeLinesType compose_lines_new;
  std::size_t total_len = 0;
  std::string line;
  line.reserve(MAX_COMPOSE_LINE_SIZE_BYTES);

  // Read all lines of the input file.
  try {
    while (raw_getline(input, line, MAX_COMPOSE_LINE_SIZE_BYTES)) {
      total_len += line.size();
      if (total_len > MAX_COMPOSE_FILE_SIZE_BYTES) {
        throw std::runtime_error("File too big");
      }
      compose_lines_new.push_back(line);
    }

  } catch (std::runtime_error &exc) {
    LOG_WARNING << "Error reading compose-file " << compose_path << ": " << exc.what();
    return false;
  }

  LOG_DEBUG << "Read compose-file: " << total_len << " chars";
  compose_lines_ = std::move(compose_lines_new);

  return true;
}

void DockerComposeFile::dumpLines() {
  for (auto &line : compose_lines_) {
    LOG_DEBUG << line;
  }
}

bool DockerComposeFile::getServices(StringToImagePlatformPair &dest, bool verbose) {
  // Start with clean destination.
  dest.clear();

  bool in_svc_section = false;

  std::string curr_service;
  std::string curr_image;
  std::string curr_platform;
  auto store_current = [&]() {
    if ((!curr_service.empty()) && (!curr_image.empty())) {
      // LOG_INFO << "Storing: " << curr_service << " => " << curr_image
      //          << " [" << na_if_empty(curr_platform) << "]";
      dest.insert({curr_service, ImagePlatformPair(curr_image, curr_platform)});
    }
  };

  std::smatch mres;
  for (const auto &line : compose_lines_) {
    // LOG_INFO << "LINE: " << line;
    // Check if we are entering a new top-level (L1) section.
    if (std::regex_match(line, mres, level1_key_re)) {
      in_svc_section = (mres[1] == services_section_name);
      if (in_svc_section) {
        // Entering the services section: clean up so that the last one
        // wins in case there is more than one (this should never happen
        // in a file in canonical format).
        dest.clear();
        curr_service.clear();
        curr_platform.clear();
        curr_image.clear();

      } else {
        // Leaving the services section.
        store_current();
      }
      continue;
    }

    if (!in_svc_section) {
      // LOG_INFO << "Ignore [not in services section]" << std::endl;
      continue;
    }

    // In the service section the level-2 key is the service name.
    if (std::regex_match(line, mres, level2_key_re)) {
      store_current();
      curr_service = mres[1];
      curr_platform.clear();
      curr_image.clear();

    } else if (std::regex_match(line, mres, image_name_re)) {
      curr_image = mres[3];

    } else if (std::regex_match(line, mres, plat_name_re)) {
      curr_platform = mres[2];
    }
  }

  store_current();

  if (verbose) {
    LOG_DEBUG << "Services in docker-compose:";
    for (auto &mapping : dest) {
      LOG_DEBUG << "* " << mapping.first << ": " << mapping.second.getImage() << " ["
                << na_if_empty(mapping.second.getPlatform()) << "]";
    }
  }

  // NOTE: Currently there are no error conditions.
  return true;
}

void DockerComposeFile::forwardTransform(const ServiceToImageMapping &service_image_mapping) {
  bool in_svc_section = false;

  ComposeLinesType new_compose_lines;

  auto save = [&](const std::string &new_line) {
    // LOG_INFO << "SAVE: " << new_line;
    new_compose_lines.push_back(new_line);
  };

  std::string curr_service;
  std::smatch mres;
  for (const auto &line : compose_lines_) {
    // LOG_INFO << "LINE: " << line;
    // Check if we are entering a new top-level (L1) section.
    if (std::regex_match(line, mres, level1_key_re)) {
      in_svc_section = (mres[1] == services_section_name);
      if (in_svc_section) {
        // Entering the services section.
        curr_service.clear();
      }
      save(line);
      continue;
    }

    if (!in_svc_section) {
      save(line);
      continue;
    }

    // In the service section the level-2 key is the service name.
    if (std::regex_match(line, mres, level2_key_re)) {
      curr_service = mres[1];
      save(line);

    } else if (std::regex_match(line, mres, image_name_re)) {
      // Handle the image name tag.
      auto it = service_image_mapping.find(curr_service);
      if (it != service_image_mapping.end()) {
        std::string new_line1 = line;
        std::string new_line2 = line;
        // Create modified versions of the line: one with the old image and
        // another with the new one (in this order) and we rely on that order
        // in backwardTransform().
        new_line1.replace(static_cast<std::string::size_type>(mres.position(1)),
                          static_cast<std::string::size_type>(mres.length(1)), image_tag_old);
        new_line2.replace(static_cast<std::string::size_type>(mres.position(3)),
                          static_cast<std::string::size_type>(mres.length(3)), it->second);
        save(new_line1);
        save(new_line2);
      } else {
        save(line);
      }

    } else {
      // Not a relevant line: just copy it.
      save(line);
    }
  }

  // Add a marker to indicate this file is in "offline-mode".
  if (!new_compose_lines.empty()) {
    // Use the first line as a template (so newline ending is kept).
    std::regex non_spaces{"^[^\\r\\n]*"};
    std::string first_line = std::regex_replace(new_compose_lines.front(), non_spaces, offline_mode_header);
    new_compose_lines.push_front(first_line);
  }

  compose_lines_ = std::move(new_compose_lines);
}

void DockerComposeFile::backwardTransform() {
  bool in_svc_section = false;

  // Check marker at first line.
  if (!compose_lines_.empty()) {
    const std::string first_line = compose_lines_.front();
    if (!std::regex_match(first_line, offline_mode_header_re)) {
      LOG_DEBUG << "Offline-mode header not found: skipping backward transform";
      return;
    }
  }

  ComposeLinesType new_compose_lines;

  auto save = [&](const std::string &new_line) {
    // LOG_INFO << "SAVE: " << new_line;
    new_compose_lines.push_back(new_line);
  };

  std::string curr_service;
  std::string curr_image;
  std::smatch mres;
  assert(compose_lines_.begin() != compose_lines_.end());
  for (auto it = std::next(compose_lines_.begin()); it != compose_lines_.end(); it++) {
    const auto &line = *it;
    // LOG_INFO << "LINE: " << line;
    // Check if we are entering a new top-level (L1) section.
    if (std::regex_match(line, mres, level1_key_re)) {
      in_svc_section = (mres[1] == services_section_name);
      if (in_svc_section) {
        // Entering the services section.
        curr_service.clear();
        curr_image.clear();
      }
      save(line);
      continue;
    }

    if (!in_svc_section) {
      save(line);
      continue;
    }

    // In the service section the level-2 key is the service name.
    if (std::regex_match(line, mres, level2_key_re)) {
      curr_service = mres[1];
      curr_image.clear();
      save(line);

    } else if (std::regex_match(line, mres, image_name_old_re)) {
      curr_image = mres[3];
      // Save a modified version of the line.
      std::string new_line1 = line;
      new_line1.replace(static_cast<std::string::size_type>(mres.position(1)),
                        static_cast<std::string::size_type>(mres.length(1)), image_tag);
      save(new_line1);

    } else if (std::regex_match(line, mres, image_name_re)) {
      if (curr_image.empty()) {
        // This deals with the case where there was not "old" image in this
        // service section which is something that shouldn't happen in practice.
        save(line);
      }

    } else {
      // Not a relevant line: just copy it.
      save(line);
    }
  }

  compose_lines_ = std::move(new_compose_lines);
}

bool DockerComposeFile::write(const boost::filesystem::path &compose_path) {
  std::ofstream output(compose_path.string(), std::ios::binary);
  if (!output) {
    LOG_WARNING << "Could not open compose-file " << compose_path << " for writing";
    return false;
  }
  for (auto &line : compose_lines_) {
    output << line;
  }
  return !!output;
}

std::string DockerComposeFile::toString() {
  std::string result;
  for (const auto &line : compose_lines_) {
    result += line;
  }
  return result;
}

std::string DockerComposeFile::getSHA256() {
  MultiPartSHA256Hasher hasher;
  for (auto &line : compose_lines_) {
    hasher.update(reinterpret_cast<const unsigned char *>(line.data()), static_cast<uint64_t>(line.size()));
  }

  std::string sha256 = boost::algorithm::to_lower_copy(hasher.getHexDigest());
  // LOG_INFO << "docker-compose sha256: " << sha256;

  return sha256;
}

// ---
// DockerComposeOfflineLoader class
// ---

DockerComposeOfflineLoader::DockerComposeOfflineLoader() : default_platform_(getDockerPlatform()) {}

DockerComposeOfflineLoader::DockerComposeOfflineLoader(boost::filesystem::path images_dir,
                                                       std::shared_ptr<DockerManifestsCache> manifests_cache)
    : default_platform_(getDockerPlatform()),
      images_dir_(std::move(images_dir)),
      manifests_cache_(std::move(manifests_cache)) {}

void DockerComposeOfflineLoader::setUp(boost::filesystem::path images_dir,
                                       const std::shared_ptr<DockerManifestsCache> &manifests_cache) {
  images_dir_ = std::move(images_dir);
  manifests_cache_ = manifests_cache;
}

void DockerComposeOfflineLoader::updateReferencedImages() {
  assert(!!compose_file_);
  compose_file_->getServices(referenced_images_);
}

void DockerComposeOfflineLoader::dumpReferencedImages() {
  LOG_DEBUG << "Images referenced in docker-compose:";
  for (auto &ri : referenced_images_) {
    LOG_DEBUG << "* " << ri.first << ":";
    const auto &platform = ri.second.getPlatform();
    LOG_DEBUG << "  " << ri.second.getImage() << " [" << na_if_empty(platform) << "]";
  }
}

void DockerComposeOfflineLoader::updateImageMapping() {
  per_service_image_mapping_.clear();

  // Translate (image, platform) pairs into appropriate image names with tags.
  for (auto &ref_image : referenced_images_) {
    const std::string &svc_name = ref_image.first;
    const std::string &req_image = ref_image.second.getImage();
    const std::string &req_platform = ref_image.second.getPlatform();

    // Determine digest and load corresponding manifest.
    std::string req_image_nodigest;
    std::string req_digest;
    splitDigestFromName(req_image, &req_image_nodigest, &req_digest, false);
    auto main_manifest = manifests_cache_->loadByDigest(req_digest);

    auto best_manifest = main_manifest;
    std::string best_digest = req_digest;
    std::string best_platform;

    if (main_manifest->hasChildren()) {
      // Multi-platform image: load the most appropriate manifest.
      main_manifest->findBestPlatform(req_platform.empty() ? default_platform_ : req_platform, &best_platform,
                                      &best_digest);
      best_manifest = manifests_cache_->loadByDigest(best_digest);
    }

    // Map names such as image@sha256:1234 to image:digest_sha256_1234.
    std::string best_digest_modif = best_digest;
    std::replace(best_digest_modif.begin(), best_digest_modif.end(), ':', '_');
    std::string sel_image = req_image_nodigest;
    sel_image += ":digest_";
    sel_image += best_digest_modif;

    ImageMappingEntry imentry(req_image, req_platform, sel_image, best_platform, best_digest,
                              best_manifest->getConfigDigest(false));

    per_service_image_mapping_.insert({svc_name, imentry});
  }
}

void DockerComposeOfflineLoader::dumpImageMapping() {
  LOG_DEBUG << "Image mapping:";
  for (auto &im : per_service_image_mapping_) {
    const std::string &svc_name = im.first;
    const ImageMappingEntry &mapping = im.second;
    LOG_DEBUG << "* " << svc_name << ":";
    LOG_DEBUG << "  from: " << mapping.getOrgImage() << " [" << na_if_empty(mapping.getOrgPlatform()) << "]";
    LOG_DEBUG << "    to: " << mapping.getSelImage() << " [" << na_if_empty(mapping.getSelPlatform()) << "]";
    LOG_DEBUG << "        manifest digest: " << mapping.getSelManDigest();
    LOG_DEBUG << "        config digest (ID): " << mapping.getSelCfgDigest();
  }
}

void DockerComposeOfflineLoader::loadCompose(const boost::filesystem::path &compose_name,
                                             const std::string &compose_sha256) {
  compose_file_ = std::make_shared<DockerComposeFile>();
  ensure(compose_file_->read(compose_name), "Could not load docker-compose file");

  if (!compose_sha256.empty()) {
    const std::string actual_sha256 = compose_file_->getSHA256();
    ensure(actual_sha256 == compose_sha256, "Compose file's digest does not match expected value, actual=\"" +
                                                actual_sha256 + "\"" + ", expect=\"" + compose_sha256 + "\"");
    LOG_INFO << "docker-compose file matches expected digest";
  } else {
    LOG_WARNING << "Skipping check of docker-compose digest";
  }

  updateReferencedImages();
  updateImageMapping();
}

static void doInstallImage(const boost::filesystem::path &tarball,
                           DockerTarballLoader::StringToStringSet expected_contents) {
  // LOG_INFO << "Preparing to install " << tarball;
  // Run actual tarball loader.
  DockerTarballLoader tbloader(tarball);
  if (!tbloader.loadMetadata() || !tbloader.validateMetadata(&expected_contents) || !tbloader.loadImages()) {
    LOG_WARNING << "Loading of tarballs aborted!";
    throw std::runtime_error("Failed to load docker tarball " + tarball.filename().string());
  }
  // LOG_INFO << "Finished installing " << tarball;
}

void DockerComposeOfflineLoader::installImages(bool make_copy) {
  std::list<std::string> loaded_digests;

  for (const auto &im : per_service_image_mapping_) {
    // const std::string &svc_name = im.first;
    const ImageMappingEntry &mapping = im.second;

    const std::string man_digest = removeDigestPrefix(mapping.getSelManDigest());
    const std::string cfg_digest = removeDigestPrefix(mapping.getSelCfgDigest());

    // Avoid loading same image more than once.
    if (std::find(loaded_digests.begin(), loaded_digests.end(), man_digest) != loaded_digests.end()) {
      LOG_INFO << "Tarball for manifest '" << man_digest << "' already loaded";
      continue;
    }

    // Define expected contents of tarball.
    DockerTarballLoader::StringToStringSet expected;
    expected[cfg_digest].insert(mapping.getSelImage());

    boost::filesystem::path org_tarball = images_dir_ / (man_digest + TAR_EXT);

    if (make_copy) {
      // Copy tarball to a secure place.
      LargeTemporaryDirectory tmpdir;
      boost::system::error_code errcode;
      boost::filesystem::path tarball = tmpdir / org_tarball.filename();
      LOG_DEBUG << "Copying " << org_tarball << " to " << tarball;
      boost::filesystem::copy_file(org_tarball, tarball, errcode);
      if (errcode != boost::system::errc::success) {
        LOG_WARNING << "Could not copy Docker tarball to secure location: aborting";
        throw std::runtime_error("Failed to copy docker tarball " + tarball.filename().string());
      }
      doInstallImage(tarball, expected);
    } else {
      doInstallImage(org_tarball, expected);
    }

    loaded_digests.push_back(man_digest);
  }
}

void DockerComposeOfflineLoader::writeOfflineComposeFile(const boost::filesystem::path &compose_name, bool verbose) {
  DockerComposeFile::ServiceToImageMapping compose_mapping;
  for (const auto &im : per_service_image_mapping_) {
    const std::string &svc_name = im.first;
    const ImageMappingEntry &mapping = im.second;
    compose_mapping.insert({svc_name, mapping.getSelImage()});
  }

  // Here we are destroying the compose_file_ object: this shouldn't be a
  // problem since all data was loaded already but it would be nicer to keep
  // the internal state.
  compose_file_->forwardTransform(compose_mapping);
  if (!compose_file_->write(compose_name)) {
    throw std::runtime_error("Failed to write " + compose_name.string());
  }

  if (verbose) {
    LOG_DEBUG << ("Offline-mode image mapping:");
    for (const auto &sm : compose_mapping) {
      LOG_DEBUG << "* " << sm.first << " => " << sm.second;
    }
    LOG_DEBUG << "Offline-mode compose written to " << compose_name;
  }
}

/*
 * TODO: In the future we should add unit tests for everything that is performed by this module;
 * Consider:
 *
 * - Installation of a "good" image with no issues;
 * - Installation of "good" image when there is no storage space in a secure location (temporary directory);
 * - Installation of images with the following issues:
 *   - docker-compose file too big
 *   - docker-compose file with wrong digest
 *   - Corrupt manifest of a Docker image without manifest list
 *   - Corrupt manifest of a Docker image with a manifest list
 *   - Corrupt manifest list of a Docker image with a manifest list
 *   - Corrupt contents of a docker-save tarball: files in tarball do not match expected ones
 */
