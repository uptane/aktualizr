#! /bin/bash

set -euo pipefail

# This expects to find the checked out source code in ./source
# It builds it into ./build and runs the tests.
# Test with
# docker build -t aktualizr-bullseye -f docker/Dockerfile.debian.bullseye 
# docker run --mount=type=volume,source=ccache,destination=/home/testuser/.cache -it aktualizr-bullseye source/scripts/build-and-test.sh
cmake -G Ninja -S source -B build
cd build
time ninja build_tests
ctest --output-on-failure -j 4

