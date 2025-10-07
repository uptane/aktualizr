#! /bin/bash

set -euo pipefail

# This expects to find the checked out source code in ./source
# It builds it into ./build and runs the tests.
# Test with
# docker build -t aktualizr-trixie -f docker/Dockerfile.debian.trixie .
# docker run --mount=type=volume,source=ccache,destination=/home/testuser/.cache -it aktualizr-trixie source/scripts/build-and-test-trixie.sh
cmake -G Ninja -S source -B build \
  -DBUILD_SOTA_TOOLS=ON \
  -DBUILD_OSTREE=OFF

cd build
time ninja build_tests check-format
ctest --output-on-failure -j "$(nproc)"
time ninja clang-tidy

