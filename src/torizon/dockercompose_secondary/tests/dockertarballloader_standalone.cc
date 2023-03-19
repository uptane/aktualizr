#include <dockertarballloader.h>
#include <iostream>
#include <string>

#include "logging/logging.h"

#define PROGRAM_NAME "dockertarballloader"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " PROGRAM_NAME " <tarball>" << std::endl;
    return 1;
  }

  logger_init(true);
  logger_set_threshold(boost::log::trivial::trace);

  DockerTarballLoader loader(argv[1]);
  if (!loader.loadMetadata()) {
    LOG_ERROR << "Metadata loading failed; aborting.";
    return 1;
  }
  if (loader.validateMetadata()) {
    LOG_INFO << "Metadata validation succeeded; loading images...";
    loader.loadImages();
  } else {
    LOG_ERROR << "Failed when verifying metadata; aborting.";
    return 1;
  }

  return 0;
}
