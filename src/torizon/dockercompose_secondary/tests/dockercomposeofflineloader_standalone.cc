#include <boost/filesystem/path.hpp>
#include <iostream>
#include <string>

#include "dockercomposesecondary.h"
#include "dockerofflineloader.h"
#include "logging/logging.h"

#define PROGRAM_NAME "dockercomposeofflineloader"

bool loadDockerImages(const boost::filesystem::path& compose_in, const std::string& compose_sha256,
                      const boost::filesystem::path& images_path, const boost::filesystem::path& manifests_path) {
  boost::filesystem::path compose_new = compose_in;
  compose_new.replace_extension(".off");

  try {
    auto dmcache = std::make_shared<DockerManifestsCache>(manifests_path);

    DockerComposeOfflineLoader dcloader(images_path, dmcache);
    dcloader.loadCompose(compose_in, compose_sha256);
    dcloader.dumpReferencedImages();
    dcloader.dumpImageMapping();
    dcloader.installImages();
    dcloader.writeOfflineComposeFile(compose_new);

  } catch (std::runtime_error& exc) {
    LOG_WARNING << "Offline loading failed: " << exc.what();
    return false;
  }

  return true;
}

int main(int argc, char* argv[]) {
  if (argc != 4 && argc != 5) {
    std::cerr << "Usage: " PROGRAM_NAME " <compose-yml> <images-dir> <manifests-dir> [<compose-sha256>]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Environment variable DOCKER_DEFAULT_PLATFORM can be set to force a specific" << std::endl;
    std::cerr << "platform (e.g. linux/arm/v7 or linux/arm64)." << std::endl;
    return 1;
  }

  logger_init(true);
  logger_set_threshold(boost::log::trivial::trace);

  const boost::filesystem::path compose_file(argv[1]);
  const boost::filesystem::path images_path(argv[2]);
  const boost::filesystem::path manifests_path(argv[3]);
  const std::string compose_sha256(argc >= 5 ? argv[4] : "");

  if (!loadDockerImages(compose_file, compose_sha256, images_path, manifests_path)) {
    return 1;
  }

  return 0;
}
