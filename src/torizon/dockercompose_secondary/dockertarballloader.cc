#include "dockertarballloader.h"
#include "crypto/crypto.h"
#include "logging/logging.h"

#include <archive.h>
#include <archive_entry.h>
#include <json/reader.h>
#include <json/value.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/process.hpp>

#include <array>
#include <csignal>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace bp = boost::process;

static const std::string DOCKER_PROGRAM = "/usr/bin/docker";
static const std::string JSON_FILE = "json";
static const std::string JSON_EXT = ".json";
static const std::string SHA256_PREFIX = "sha256:";

// Maximum size of a JSON file in a `docker save` tarball.
static constexpr std::size_t MAX_JSON_FILE_SIZE_BYTES = 256 * 1024;

// Maximum aggregate size of all JSON files in a `docker save` tarball.
static constexpr std::size_t MAX_TOT_JSON_FILES_SIZE_BYTES = 4 * 1024 * 1024;

// Size of a block for reading input files from tarball.
static constexpr std::size_t DEFAULT_BLOCK_BUFFER_SIZE_BYTES = 256 * 1024;

/**
 * Class that allows blocking signals in the current thread.
 *
 * How to use:
 *
 * SignalBlocker block(SIGPIPE);
 * ...(protected code)...
 *
 * The destructor will restore the signal mask.
 *
 * TODO: Consider moving to a more general / separate module.
 * TODO: Accept signals possibly generated while they were blocked.
 *
 * - See https://gitlab.int.toradex.com/rd/torizon-core/aktualizr-torizon/-/merge_requests/7#note_59407
 * - See http://www.microhowto.info/howto/ignore_sigpipe_without_affecting_other_threads_in_a_process.html
 */
class SignalBlocker {
 protected:
  sigset_t org_mask{};

  // NOLINTNEXTLINE(modernize-avoid-c-arrays, cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays)
  void block(int sigs[], int n) {
    sigset_t signal_mask;
    ::sigemptyset(&signal_mask);
    for (int i = 0; i < n; i++) {
      // LOG_INFO << "Blocking signal " << sigs[i];
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      ::sigaddset(&signal_mask, sigs[i]);
    }
    ::pthread_sigmask(SIG_BLOCK, &signal_mask, &org_mask);
  }

  void restore() {
    // LOG_INFO << "Restoring signals";
    ::pthread_sigmask(SIG_BLOCK, &org_mask, nullptr);
  }

 public:
  explicit SignalBlocker(int sig1) {
    // NOLINTNEXTLINE(modernize-avoid-c-arrays, cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays)
    int sigs[] = {sig1};
    block(sigs, 1);
  }
  SignalBlocker(const SignalBlocker &) = delete;
  SignalBlocker(SignalBlocker &&) = delete;
  SignalBlocker &operator=(const SignalBlocker &other) = delete;
  SignalBlocker &operator=(SignalBlocker &&other) = delete;
  virtual ~SignalBlocker() { restore(); }
};

/**
 * Assert-like function with a message.
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

/**
 * Print function for string sets:
 */
std::ostream &operator<<(std::ostream &stream, const std::set<std::string> &value) {
  stream << "{";
  for (const auto &item : value) {
    stream << item << ", ";
  }
  stream << "}";
  return stream;
}

static constexpr std::size_t ARCHIVE_CTRL_BUFFER_SIZE = DEFAULT_BLOCK_BUFFER_SIZE_BYTES;

/**
 * Helper class for reading a file and determining its digest.
 */
struct ArchiveCtrl {
 protected:
  using BufferType = std::array<uint8_t, ARCHIVE_CTRL_BUFFER_SIZE>;
  std::ifstream infile_;
  uint64_t nread_{0};
  BufferType buffer_{};
  MultiPartSHA256Hasher hasher_;

 public:
  explicit ArchiveCtrl(const boost::filesystem::path &tarball) : infile_(tarball.string(), std::ios::binary) {
    if (!infile_) {
      throw std::runtime_error("Could not open '" + tarball.string() + "'");
    }
  }
  ArchiveCtrl(const ArchiveCtrl &other) = delete;
  ArchiveCtrl(ArchiveCtrl &&other) = delete;
  ArchiveCtrl &operator=(const ArchiveCtrl &other) = delete;
  ArchiveCtrl &operator=(ArchiveCtrl &&other) = delete;

  virtual ~ArchiveCtrl() { infile_.close(); }

  ssize_t read() {
    infile_.read(reinterpret_cast<char *>(buffer_.data()), static_cast<std::streamsize>(buffer_.size()));
    hasher_.update(buffer_.data(), static_cast<uint64_t>(infile_.gcount()));
    nread_ += static_cast<uint64_t>(infile_.gcount());
    return infile_.gcount();
  }

  uint64_t nread() const { return nread_; }

  void *data() { return static_cast<void *>(buffer_.data()); }

  std::string getHexDigest() { return boost::algorithm::to_lower_copy(hasher_.getHexDigest()); }
};

/**
 * Helper function for integrating with libarchive.
 */
static ssize_t arch_reader(struct archive *arch, void *client_data, const void **buff) {
  (void)arch;
  auto *archctrl = reinterpret_cast<ArchiveCtrl *>(client_data);
  *buff = archctrl->data();
  return archctrl->read();
}

bool DockerTarballLoader::loadMetadataEntryJson(archive *arch, archive_entry *entry) {
  const boost::filesystem::path pathname{archive_entry_pathname(entry)};

  // TODO: We might call archive_entry_size() to allocate the buffer.
  //       However, the size is not guaranteed to be always set.

  // Load the JSON file into memory.
  using JsonBufferType = std::array<uint8_t, MAX_JSON_FILE_SIZE_BYTES + 1>;
  auto buffer = std_::make_unique<JsonBufferType>();
  ssize_t count = archive_read_data(arch, reinterpret_cast<void *>(buffer->data()), buffer->size());
  if (count > static_cast<ssize_t>(MAX_JSON_FILE_SIZE_BYTES)) {
    LOG_WARNING << "JSON file '" << pathname.string() << "' in archive is larger than the maximum size of "
                << MAX_JSON_FILE_SIZE_BYTES << " bytes";
    return false;
  }

  // Determine the file's digest.
  MultiPartSHA256Hasher hasher;
  hasher.update(buffer->data(), static_cast<uint64_t>(count));
  std::string digest = boost::algorithm::to_lower_copy(hasher.getHexDigest());

  // Parse contents.
  std::string _source(reinterpret_cast<char *>(buffer->data()), static_cast<std::string::size_type>(count));
  std::istringstream source(_source);
  Json::Value root;
  source >> root;

  // Store metadata information (keyed by file name).
  MetaInfo info(digest, root);
  MetadataMap::value_type value(pathname.string(), info);
  std::pair<MetadataMap::iterator, bool> res;
  res = metamap_.insert(value);
  // LOG_INFO << "Inserted: (" << value.first
  //          << ", " << value.second.sha256_ << ")";

  if (!res.second) {
    LOG_WARNING << "Archive has duplicate file: " << pathname.string();
    return false;
  }

  // Update statistics.
  metastats_.nfiles_json++;
  metastats_.nbytes_json += static_cast<uint64_t>(count);

  if (metastats_.nbytes_json > MAX_TOT_JSON_FILES_SIZE_BYTES) {
    LOG_WARNING << "Total size of JSON files in tarball was exceeded";
    return false;
  }

  return true;
}

bool DockerTarballLoader::loadMetadataEntryOther(archive *arch, archive_entry *entry) {
  const boost::filesystem::path pathname{archive_entry_pathname(entry)};

  // Process file in blocks.
  using BufferType = std::array<uint8_t, DEFAULT_BLOCK_BUFFER_SIZE_BYTES>;
  auto buffer = std_::make_unique<BufferType>();

  ssize_t count;
  MultiPartSHA256Hasher hasher;

  // Determine the file's digest.
  do {
    count = archive_read_data(arch, reinterpret_cast<void *>(buffer->data()), buffer->size());
    hasher.update(buffer->data(), static_cast<uint64_t>(count));
    // Update statistics.
    metastats_.nbytes_other += static_cast<uint64_t>(count);
  } while (count > 0);

  std::string digest = boost::algorithm::to_lower_copy(hasher.getHexDigest());

  // Store metadata information (keyed by file name).
  MetaInfo info(digest);
  MetadataMap::value_type value(pathname.string(), info);
  std::pair<MetadataMap::iterator, bool> res;
  res = metamap_.insert(value);
  // LOG_INFO << "Inserted: (" << value.first
  //          << ", " << value.second.sha256_ << ")";

  if (!res.second) {
    LOG_WARNING << "Archive has duplicate file: " << pathname.string();
    return false;
  }

  // Update statistics.
  metastats_.nfiles_other++;

  // TODO: Should we limit the size of non-JSON files?
  // if (metastats_.nbytes_other > MAX_TOT_OTHER_FILES_SIZE) {
  //   LOG_WARNING << "Total size of non-JSON files in tarball was exceeded";
  //   return false;
  // }

  return true;
}

bool DockerTarballLoader::loadMetadataEntry(archive *arch, archive_entry *entry) {
  // Ensure path name is good (relative and not using '.' or '..').
  const boost::filesystem::path pathname{archive_entry_pathname(entry)};
  if (!pathname.is_relative() || (pathname.lexically_normal() != pathname)) {
    LOG_WARNING << "Found in archive a file with non-relative name: " << pathname.string();
    return false;
  }

  // Ensure file type is good.
  if (archive_entry_filetype(entry) != AE_IFREG && archive_entry_filetype(entry) != AE_IFDIR) {
    LOG_WARNING << "Found in archive a file with bad file type: " << archive_entry_filetype(entry);
    return false;
  }

  // Do nothing for directory entries.
  if (archive_entry_filetype(entry) == AE_IFDIR) {
    return true;
  }

  assert(archive_entry_filetype(entry) == AE_IFREG);

  // TODO: Should we make these comparisons case-insensitive?
  if (pathname.extension() == JSON_EXT || pathname.filename() == JSON_FILE) {
    return loadMetadataEntryJson(arch, entry);
  } else {
    return loadMetadataEntryOther(arch, entry);
  }
}

void DockerTarballLoader::loadMetadata() {
  archive *arch;
  archive_entry *entry;

  LOG_INFO << "Loading metadata from tarball: " << tarball_.string();
  auto archctrl = std_::make_unique<ArchiveCtrl>(tarball_);

  arch = archive_read_new();
  archive_read_support_filter_none(arch);
  archive_read_support_format_tar(arch);
  archive_read_open(arch, archctrl.get(), nullptr, arch_reader, nullptr);

  metamap_.clear();
  metastats_.clear();
  while (archive_read_next_header(arch, &entry) == ARCHIVE_OK) {
    loadMetadataEntry(arch, entry);
  }
  archive_read_free(arch);

  // Save original digest so we can check it upon loading the images.
  org_tarball_digest_ = archctrl->getHexDigest();
  org_tarball_length_ = archctrl->nread();
  LOG_DEBUG << "1st pass: tarball sha256=" << org_tarball_digest_ << ", len=" << org_tarball_length_;

  LOG_TRACE << "nbytes_other: " << metastats_.nbytes_other << ", nfiles_other: " << metastats_.nfiles_other;
  LOG_TRACE << "nbytes_json: " << metastats_.nbytes_json << ", nfiles_json: " << metastats_.nfiles_json;

  LOG_TRACE << "Files in tarball:";
  for (auto &value : metamap_) {
    LOG_TRACE << value.second.getSHA256() << ": " << value.first;
  }
}

Json::Value DockerTarballLoader::metamapGetRoot(const std::string &key) {
  auto it = metamap_.find(key);
  ensure(it != metamap_.end(), "Key '" + key + "' not found in metamap");
  return it->second.getRoot();
}

std::string DockerTarballLoader::metamapGetSHA256(const std::string &key) {
  auto it = metamap_.find(key);
  ensure(it != metamap_.end(), "Key '" + key + "' not found in metamap");
  return it->second.getSHA256();
}

bool DockerTarballLoader::validateMetadata(StringToStringSet *expected_tags_per_image) {
  try {
    Json::Value manifest = metamapGetRoot("manifest.json");
    // LOG_INFO << "manifest:" << manifest;
    ensure(manifest.isArray(), "bad manifest type");

    std::set<std::string> actual_image_ids;

    // ---
    // Check internal consistency.
    // ---
    for (const auto &man : manifest) {
      ensure(man.isMember("Config"), "no Config in manifest");
      const boost::filesystem::path config(man["Config"].asString());
      ensure(config.extension() == JSON_EXT, "bad config file extension");

      // Ensure the configuration file has correct digest.
      const std::string imgid = metamapGetSHA256(config.string());
      ensure(config.stem() == imgid, imgid + ": config. file name does not match its own checksum");

      // Ensure there is only one configuration per image in the manifest.
      ensure(actual_image_ids.find(imgid) == actual_image_ids.end(),
             imgid + ": config. file declared multiple times in manifest");
      actual_image_ids.insert(imgid);
    }

    LOG_DEBUG << this->tarball_.filename().string() << ": "
              << "manifest validation passed";

    // ---
    // Check internal consistency (not strictly required part).
    // ---
    for (const auto &man : manifest) {
      const boost::filesystem::path config(man["Config"].asString());
      const Json::Value config_value = metamapGetRoot(config.string());

      ensure(config_value["rootfs"]["diff_ids"].isArray(), config.string() + ": bad config. object format");
      ensure(config_value["rootfs"]["diff_ids"].size() == man["Layers"].size(),
             config.string() + ": layer count mismatch");

      const Json::Value &cfg_lhashes = config_value["rootfs"]["diff_ids"];
      const Json::Value &man_layers = man["Layers"];

      for (Json::Value::ArrayIndex idx = 0; idx < cfg_lhashes.size(); idx++) {
        // Get expected hash.
        const std::string cfg_hash_(cfg_lhashes[idx].asString());
        ensure(boost::starts_with(cfg_hash_, SHA256_PREFIX), config.string() + ": bad layer hash in config");
        const std::string cfg_hash = cfg_hash_.substr(SHA256_PREFIX.length());

        // Get actual hash and check it.
        const std::string tar_name(man_layers[idx].asString());
        const std::string tar_hash(metamapGetSHA256(tar_name));
        LOG_TRACE << "layer[" << idx << "]: " << cfg_hash.substr(0, 12) << " = " << tar_hash.substr(0, 12) << "?";
        ensure(cfg_hash == tar_hash, config.string() + ": layer hash mismatch");
      }
    }

    LOG_DEBUG << this->tarball_.filename().string() << ": "
              << "layers validation passed";

    // ---
    // Check external requirements.
    // ---

    if (expected_tags_per_image != nullptr) {
      // Extract expected set of images.
      std::set<std::string> expected_image_ids;
      for (const auto &elem : *expected_tags_per_image) {
        expected_image_ids.insert(elem.first);
      }

      // Ensure list of images in tarball matches expectations.
      ensure(actual_image_ids == expected_image_ids, "Images in manifest do not match expected list");

      // Checks the list of tags related to each image.
      for (const auto &man : manifest) {
        const boost::filesystem::path config(man["Config"].asString());
        const std::string imgid(config.stem().string());

        static constexpr const char *repo_tags_el = "RepoTags";
        ensure(man.isMember(repo_tags_el), "no RepoTags in manifest");
        ensure(man[repo_tags_el].isArray(), "bad RepoTags type");

        std::set<std::string> &expected_repo_tags = expected_tags_per_image->at(imgid);
        std::set<std::string> actual_repo_tags;
        for (const auto &actual_tag : man[repo_tags_el]) {
          actual_repo_tags.insert(actual_tag.asString());
        }

        ensure(actual_repo_tags == expected_repo_tags, imgid + ": does not have the expected tags");
      }

      LOG_DEBUG << this->tarball_.filename().string() << ": "
                << "tag validation passed";
    }

  } catch (std::out_of_range &exc) {
    LOG_WARNING << "validateMetadata: " << exc.what() << " [OOR]";
    return false;

  } catch (std::runtime_error &exc) {
    LOG_WARNING << "validateMetadata: " << exc.what() << " [RE]";
    return false;
  }

  return true;
}

bool DockerTarballLoader::loadImages() {
  // Open tarball as raw binary data.
  std::ifstream infile(tarball_.string(), std::ios::binary);
  // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
  if (!infile) {
    LOG_WARNING << "Could not open '" << tarball_.string() << "'";
    return false;
  }

  // Define a circular buffer of data blocks.
  static constexpr const size_t num_blocks_power = 4;
  static constexpr const size_t num_blocks = (1U << num_blocks_power);
  static constexpr const size_t num_blocks_mask = num_blocks - 1U;

  struct Block {
    std::array<uint8_t, 16 * 1024> buf{};
    size_t len{0};
    bool used{false};
    Block() = default;
    void clear() {
      len = 0;
      used = false;
    }
  };
  using Blocks = std::array<Block, num_blocks>;

  auto blocks = std_::make_unique<Blocks>();
  unsigned block_index = 0;

  MultiPartSHA256Hasher hasher;

  // Prevent SIGPIPE in case the child program exits unexpectedly.
  SignalBlocker blocker(SIGPIPE);

  bp::opstream docker_stdin;
  // Run the `docker load` external program.
  // bp::child docker_proc("/usr/bin/ls", bp::std_in < docker_stdin);
  // bp::child docker_proc("/usr/bin/sha256sum", bp::std_in < docker_stdin);
  bp::child docker_proc(DOCKER_PROGRAM, "load", bp::std_in < docker_stdin);

  // TODO: Handle the program output if more control is needed. See:
  // https://stackoverflow.com/questions/48678012/simultaneous-read-and-write-to-childs-stdio-using-boost-process

  // Read tarball, send it to `docker load` and determine its digest.
  uint64_t nread = 0;
  for (;;) {
    auto &cur_block = blocks->at(block_index);
    if (cur_block.used) {
      // Block already used: send all data to external process.
      docker_stdin.write(reinterpret_cast<char *>(cur_block.buf.data()), static_cast<std::streamsize>(cur_block.len));
      cur_block.clear();
    }

    infile.read(reinterpret_cast<char *>(cur_block.buf.data()), static_cast<std::streamsize>(cur_block.buf.size()));
    cur_block.len = static_cast<size_t>(infile.gcount());
    cur_block.used = true;

    // Prevent modifications of file size: this is very important to avoid attacks
    // where extraneous data is appended to the end marker of the tarball.
    nread += static_cast<uint64_t>(infile.gcount());
    if (nread > org_tarball_length_) {
      LOG_WARNING << "Size of tarball has changed (aborting)";
      break;
    }

    // Update digest.
    hasher.update(cur_block.buf.data(), static_cast<uint64_t>(cur_block.len));

    // Advance.
    block_index = (block_index + 1) & num_blocks_mask;
    if (!infile) {
      break;
    }
  }

  // At this point, not all data has been sent to the child program. So here
  // we decide whether or not to abort the process by truncating the stream.
  std::string new_digest = boost::algorithm::to_lower_copy(hasher.getHexDigest());
  LOG_TRACE << "2nd pass: tarball sha256=" << new_digest << ", len=" << nread;

  bool success = false;
  if (org_tarball_digest_ == new_digest) {
    // Send outstanding blocks if everything is good.
    for (unsigned cnt = 0; cnt < num_blocks; cnt++) {
      auto &cur_block = blocks->at(block_index);
      if (cur_block.used) {
        // Block already used: send all data to external process.
        docker_stdin.write(reinterpret_cast<char *>(cur_block.buf.data()), static_cast<std::streamsize>(cur_block.len));
        cur_block.clear();
      }
      block_index = (block_index + 1) & num_blocks_mask;
    }

    success = !docker_stdin.fail();

  } else {
    // Digest changed from first time we took it.
    LOG_WARNING << "Digest of '" << tarball_.string() << "' has changed from '" << org_tarball_digest_ << "' to '"
                << new_digest << "'";
  }

  docker_stdin.flush();
  docker_stdin.pipe().close();
  docker_stdin.close();

  docker_proc.wait();

  // We have success only if the loading program succeeded.
  success = success && (docker_proc.exit_code() == 0);

  LOG_INFO << "Loading of " << tarball_ << " finished, "
           << "code: " << docker_proc.exit_code() << ", status: " << (success ? "success" : "failed");

  return success;
}
