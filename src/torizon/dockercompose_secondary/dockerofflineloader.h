#ifndef SECONDARY_DOCKEROFFLINELOADER_H_
#define SECONDARY_DOCKEROFFLINELOADER_H_

#include <boost/filesystem/path.hpp>
#include <json/value.h>
#include <memory>
#include <regex>
#include <string>

// TODO: Should we put this in some specific namespace?

/**
 * Basic wrapper to a JSON object which is expected to contain a Docker
 * manifest or manifest list.
 */
class DockerManifestWrapper {
  public:
    /**
     * Constructor taking a Docker manifest in a JsonCpp Value object.
     *
     * @param manifest the root Json::Value object to be wrapped.
     */
    explicit DockerManifestWrapper(const Json::Value &manifest);

    /**
     * Return whether or not the manifest is multi-platform (or more precisely,
     * if it is a manifest list).
     */
    bool isMultiPlatform() const;

    /**
     * Get digest/platform pair most closely matching the requested platform
     * from a manifest list. If none is found, throws a runtime_exception.
     */
    void findBestPlatform(
        std::string req_platform,
        std::string *sel_platform, std::string *sel_digest) const;

    /**
     * Get digest of the configuration object of an image.
     */
    std::string getConfigDigest(bool removePrefix=false) const;

  protected:
    Json::Value manifest_;

    std::string platformString(const Json::Value &plat) const;
    std::string getMediaType() const;
    void ensureMediaType(const std::string &req_type) const;

    // Known media types.
    struct MEDIA_TYPE {
      static const std::string SINGLE_PLAT;
      static const std::string MULTI_PLAT;
    };
};

/**
 * Simple LRU cache for keeping Docker manifests.
 */
class DockerManifestsCache {
  public:
    typedef std::shared_ptr<DockerManifestWrapper> ManifestPtr;
    typedef std::pair<size_t, ManifestPtr> ManifestCacheElem;
    typedef std::map<std::string, ManifestCacheElem> DigestToManifestCacheElemMap;

  public:
    explicit DockerManifestsCache(
        const boost::filesystem::path &manifests_dir, size_t max_manifests=32)
      : manifests_dir_(manifests_dir),
        max_manifests_(max_manifests), access_counter_(0) {}

    /**
     * Load the manifest (specified by its digest) from the manifest directory
     * storing it into the cache.
     *
     * @return a smart pointer to an object wrapping the actual manifest.
     */
    ManifestPtr loadByDigest(const std::string &digest);

  protected:
    boost::filesystem::path manifests_dir_;
    size_t max_manifests_;
    size_t access_counter_;
    DigestToManifestCacheElemMap manifests_cache_;
};

/**
 * Auxiliary class for keeping information extracted from a docker-compose
 * file.
 */
class ImagePlatformPair {
  protected:
    std::string image_;
    std::string platform_;
  public:
    ImagePlatformPair(const std::string &image)
      : image_(image) {}
    ImagePlatformPair(const std::string &image,
                      const std::string &platform)
      : image_(image), platform_(platform) {}

  const std::string &getImage() { return image_; }
  const std::string &getPlatform() { return platform_; }
};

typedef std::map<std::string, ImagePlatformPair> StringToImagePlatformPair;

/**
 * Class for transforming a docker-compose file from the original form with
 * images being referenced by digest to the "offline" form where images are
 * referenced by their tags (forward transformation); the transformation in
 * the opposite direction is provided as well.
 *
 * This class does some basic parsing of the compose file but it assumes the
 * file to be in a "canonical" form as it is expected to come from the OTA
 * server.
 */
class DockerComposeFile {
  protected:
    typedef std::list<std::string> ComposeLinesType;
    ComposeLinesType compose_lines_;

    static const std::string services_section_name;
    static const std::string offline_mode_header;
    static const std::string image_tag;
    static const std::string image_tag_old;

    static const std::regex offline_mode_header_re;
    static const std::regex level1_key_re;
    static const std::regex level2_key_re;
    static const std::regex image_name_re;
    static const std::regex image_name_old_re;
    static const std::regex plat_name_re;

  public:
    typedef std::map<std::string, std::string> ServiceToImageMapping;

  public:
    DockerComposeFile() {}

    /**
     * Constructor: construct and execute the read() method with the passed
     * argument.
     */
    DockerComposeFile(const boost::filesystem::path &compose_path);

    /**
     * Determine if object is in the good (docker-compose data is loaded
     * into memory).
     */
    operator bool() { return !compose_lines_.empty(); }
    bool good() { return !compose_lines_.empty(); }

    /**
     * Read the docker-compose into memory. Once read, one can utilize
     * methods forwardTransform() and backwardTransform() to transform the
     * file and possibly write it int a new file (with write()) or determine
     * its digest in memory (with getSHA256()).
     *
     * @return true iff operation was successful.
     */
    bool read(const boost::filesystem::path &compose_path);

    /**
     * Dump the docker-compose lines currently in memory.
     */
    void dumpLines();

    /**
     * Get the list of services in the docker-compose.
     *
     * @param dest this is a map between the service name and a (image, platform)
     *  pair (with an empty platform when that field is not set); this field is
     *  populated by the function.
     * @param verbose whether or not to show extra information.
     */
    bool getServices(StringToImagePlatformPair &dest, bool verbose=true);

    /**
     * Transform the docker-compose file (in memory) so that each service
     * uses the image defined by the specified mapping. The result can be
     * written to a file via `write()` or have its digest determined by
     * `getSHA256()` in memory.
     *
     * @param service_image_mapping (service name, desired image) mapping.
     */
    void forwardTransform(const ServiceToImageMapping &service_image_mapping);

    /**
     * Undo the transformation done by `forwardTransform()`. The result can be
     * written to a file via `write()`, have its digest determined by
     * `getSHA256()` or obtained as a string with `toString()`.
     */
    void backwardTransform();

    /**
     * Write a docker-compose file with the text lines currently in memory.
     *
     * @return true iff operation was successful.
     */
    bool write(const boost::filesystem::path &compose_path);

    /**
     * Get the docker-compose file currently in memory as a single string.
     */
    std::string toString();

    /**
     * Determine the SHA256 checksum of the data in memory (text lines kept
     * in compose_lines_).
     */
    std::string getSHA256();
};

/**
 * Class for loading images referenced by a docker-compose file (and validating
 * all referenced images against their manifests). How to use:
 *
 *     boost::filesystem::path images_dir("update/images/xyz.images/");
 *     boost::filesystem::path manifests_dir("update/metadata/docker/xyz.manifests/");
 *     boost::filesystem::path compose_name("update/images/docker-compose.yml");
 *     try {
 *       auto dmcache = std::make_shared<DockerManifestsCache>(manifests_dir);
 *       DockerComposeOfflineLoader dcloader(images_dir, dmcache);
 *       dcloader.loadCompose(compose_name, "<expected_compose_digest>");
 *       dcloader.dumpReferencedImages();
 *       dcloader.dumpImageMapping();
 *       dcloader.installImages();
 *       dcloader.writeOfflineComposeFile("docker-compose-OFFLINE.yml", true);
 *     } catch (std::runtime_error &exc) {
 *       LOG_WARNING << "Process stopped: " << exc.what();
 *     }
 */
class DockerComposeOfflineLoader {
  protected:
    class ImageMappingEntry {
      protected:
        std::string org_image_;
        std::string org_platform_;
        std::string sel_image_;
        std::string sel_platform_;
        std::string sel_man_digest_;
        std::string sel_cfg_digest_;
      public:
        ImageMappingEntry() {}
        ImageMappingEntry(
            const std::string &org_image, const std::string &org_platform,
            const std::string &sel_image, const std::string &sel_platform,
            const std::string &sel_man_digest,
            const std::string &sel_cfg_digest)
          : org_image_(org_image), org_platform_(org_platform),
            sel_image_(sel_image), sel_platform_(sel_platform),
            sel_man_digest_(sel_man_digest),
            sel_cfg_digest_(sel_cfg_digest) {}

        const std::string &getOrgImage() const {
          return org_image_;
        }
        const std::string &getOrgPlatform() const {
          return org_platform_;
        }
        const std::string &getSelImage() const {
          return sel_image_;
        }
        const std::string &getSelPlatform() const {
          return sel_platform_;
        }
        const std::string &getSelManDigest() const {
          return sel_man_digest_;
        }
        const std::string &getSelCfgDigest() const {
          return sel_cfg_digest_;
        }
    };

    typedef std::map<std::string, ImageMappingEntry> PerServiceImageMapping;

  public:
    DockerComposeOfflineLoader();
    DockerComposeOfflineLoader(
        const boost::filesystem::path &images_dir,
        const std::shared_ptr<DockerManifestsCache> &manifests_cache);

    /**
     * Configure what images directory and manifest cache object to be
     * used by this instance of the offline loader.
     */
    void setUp(
        const boost::filesystem::path &images_dir,
        const std::shared_ptr<DockerManifestsCache> &manifests_cache);

    /**
     * Load the specified docker-compose file, check its SHA256 against the
     * specified one and determine images referenced by it and corresponding
     * mapping to offline images (to be installed from tarballs). So, after
     * invoking this method, one can tell what images would be installed by
     * the `installImages()` method, if invoked.
     *
     * In case of errors, a `std::runtime_error` exception will be thrown.
     */
    void loadCompose(const boost::filesystem::path &compose_name,
                     const std::string &compose_sha256);

    /**
     * Install images defined by the docker-compose file last "loaded".
     */
    void installImages(bool make_copy=false);

    /**
     * Write an offline version of the docker-compose file last "loaded".
     */
    void writeOfflineComposeFile(
        const boost::filesystem::path &compose_name, bool verbose=true);

    /**
     * Dump internal state: images being referenced by the docker-compose file.
     */
    void dumpReferencedImages();

    /**
     * Dump internal state: mapping between images in the docker-compose file
     * and actual tagged images to be loaded from tarballs (plus other pieces
     * of information needed for validating those tarballs).
     */
    void dumpImageMapping();

  protected:
    void updateReferencedImages();
    void updateImageMapping();

    // TODO: Allow configuring this attribute (FUTURE)?
    std::string default_platform_;
    boost::filesystem::path images_dir_;
    std::shared_ptr<DockerManifestsCache> manifests_cache_;
    std::shared_ptr<DockerComposeFile> compose_file_;

    StringToImagePlatformPair referenced_images_;
    PerServiceImageMapping per_service_image_mapping_;
};

/**
 * Load manifest into memory, ensure it has desired digest and parse it
 * into a Json::Value.
 *
 * @return true iff loading was successful.
 */
bool loadManifest(
    const std::string &req_digest,
    const boost::filesystem::path manifests_dir, Json::Value &target);

/**
 * Determine if two platform specification strings match.
 *
 * E.g. linux matches linux/
 *      linux matches linux/arm
 *      linux matches linux/arm/v5
 *      linux/arm matches linux/arm/v7
 *      linux/arm/v5 DOES NOT match linux/arm/v6
 *      linux DOES NOT match windows ;-)
 *
 */
bool platformMatches(
    const std::string plat1, const std::string plat2, unsigned *grade=nullptr);

/**
 * Determine current Docker platform (default platform for fetching images).
 * Platform is detected based on information returned by uname(), but it can
 * be overriden by setting environment variable DOCKER_DEFAULT_PLATFORM.
 *
 * @return a string such as "linux/arm/v7" or "linux/arm64".
 */
std::string getDockerPlatform();

/**
 * Given a docker image name containing a digest, split the name from the
 * digest.
 *
 * @param name full image name with digest.
 * @param name_nodigest pointer to string that will hold the name without
 *  the digest (if pointer is not null).
 * @param digest pointer to string that will hold the digest. For a name such
 *  as "repo/hello-world@sha256:123abc...123" the function would return
 *  "sha256:123abc...123" in *digest.
 * @param removePrefix whether or not to remove the sha256 prefix from the
 *  value returned as digest.
 */
void splitDigestFromName(
    const std::string &name,
    std::string *name_nodigest, std::string *digest, bool removePrefix=false);

/**
 * Return digest without the prefix `sha256:`.
 */
std::string removeDigestPrefix(const std::string &digest);

/**
 * Determine if a platform string matches any of an iterable.
 */
template <class T>
  bool platformIn(const std::string plat, const T &container) {
  bool res = false;
  for (auto &it : container) {
    if (platformMatches(plat, it)) {
      res = true;
      break;
    }
  }
  return res;
}

#endif /* SECONDARY_DOCKEROFFLINELOADER_H_ */
