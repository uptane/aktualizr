#! /bin/bash

set -euo pipefail

# This expects to find the checked out source code in ./source
# It builds it into ./build and runs the tests.
# Test with
# docker build -t aktualizr-bullseye -f docker/Dockerfile.debian.bullseye .
# docker run --mount=type=volume,source=ccache,destination=/home/testuser/.cache -it aktualizr-bullseye source/scripts/build-and-test.sh

cmake -G Ninja -S source -B build \
  -DBUILD_SOTA_TOOLS=ON \
  -DBUILD_OSTREE=ON \
  -DBUILD_P11=ON \
  -DPKCS11_ENGINE_PATH=/usr/lib/x86_64-linux-gnu/engines-1.1/libpkcs11.so \
  -DTEST_PKCS11_MODULE_PATH=/usr/lib/softhsm/libsofthsm2.so \
  -DFAULT_INJECTION=ON
cd build
time ninja build_tests

export SOFTHSM2_CONF=../source/tests/test_data/softhsm2.conf
mkdir /tmp/tokens
export TOKEN_DIR=/tmp/tokens
../source/scripts/setup_hsm.sh

ctest --output-on-failure -j "$(nproc)"
