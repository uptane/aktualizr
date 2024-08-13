#! /bin/bash

set -euo pipefail

CLANG_TIDY="${1}"
CMAKE_BINARY_DIR="${2}"
CMAKE_SOURCE_DIR="${3}"
FILE="${4}"

if [[ ! -e "${CMAKE_BINARY_DIR}/compile_commands.json" ]]; then
  echo "compile_commands.json not found!"
  exit 1
fi

${CLANG_TIDY} -quiet -header-filter="\(\(${CMAKE_SOURCE_DIR}|\\.\\.\)/src/|include/libaktualizr/\).*" \
  --extra-arg-before=-Wno-unknown-warning-option \
  --extra-arg-before=-Wno-warnings-as-errors \
  --extra-arg=-Wno-deprecated-declarations \
  --format-style=file -p "${CMAKE_BINARY_DIR}" "${FILE}"
