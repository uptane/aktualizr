#ifndef SECONDARY_DOCKERTARBALLLOADER_H_
#define SECONDARY_DOCKERTARBALLLOADER_H_

#include <boost/filesystem/path.hpp>
#include <json/value.h>
#include <map>
#include <set>
#include <string>

struct archive;
struct archive_entry;

// TODO: Should we put this in some specific namespace?

/**
 * Class for validating and loading tarballs produced by the `docker save`
 * command.
 */
class DockerTarballLoader {
  public:
    class MetaInfo {
      protected:
        std::string sha256_;  // Metadata file's digest.
        Json::Value root_;    // Metadata in the file.
      public:
        explicit MetaInfo(const std::string &sha256)
          : sha256_(sha256), root_() {}
        MetaInfo(const std::string &sha256, Json::Value &root)
          : sha256_(sha256), root_(root) {}
        Json::Value &getRoot() { return root_; }
        std::string &getSHA256() { return sha256_; }
    };

    typedef std::map<std::string, MetaInfo> MetadataMap;

    struct MetaStats {
      uint32_t nfiles_json;
      uint32_t nfiles_other;
      uint64_t nbytes_json;
      uint64_t nbytes_other;
      void clear() {
        nfiles_json = 0;
        nfiles_other = 0;
        nbytes_json = 0;
        nbytes_other = 0;
      }
    };

    typedef std::map<std::string, std::set<std::string>> StringToStringSet;

  public:
    /**
     * Constructor.
     */
    explicit DockerTarballLoader(const boost::filesystem::path& tarball)
      : tarball_(tarball), org_tarball_length_(0) {}

    /**
     * Parse tarball archive and load all metadata (JSON) files into
     * memory. It also determines the sha256 of all files in the tarball.
     */
    void loadMetadata();

    /**
     * Validate the metadata loaded by loadMetadata().
     *
     * @param expected_tags_per_image top-level keys in this map are the expected
     *  images in the tarball; the values are sets containing the expected tags
     *  each image must have; if this parameter is null then only internal
     *  validation of the tarball will be performed.
     */
    bool validateMetadata(StringToStringSet *expected_tags_per_image = nullptr);

    /**
     * Load the Docker images from the tarball; this function does not call
     * validateMetadata(); thus, it would be possible to load the images even
     * if no validation was performed (or if it failed); that decision is left
     * at the discretion of the caller.
     */
    bool loadImages();

  protected:
    boost::filesystem::path tarball_;
    std::string org_tarball_digest_;
    uint64_t org_tarball_length_;
    MetadataMap metamap_;
    MetaStats metastats_;

    bool loadMetadataEntry(archive *arch, archive_entry *entry);
    bool loadMetadataEntryJson(archive *arch, archive_entry *entry);
    bool loadMetadataEntryOther(archive *arch, archive_entry *entry);

    Json::Value metamapGetRoot(const std::string &key);
    std::string metamapGetSHA256(const std::string &key);
};

#endif /* SECONDARY_DOCKERTARBALLLOADER_H_ */
