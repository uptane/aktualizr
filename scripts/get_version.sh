#!/usr/bin/env bash

set -euo pipefail

GIT=${1:-git}
REPO=${2:-.}

if ! VERSION=$("$GIT" -C "$REPO" describe --long 2>/dev/null | tr -d '\n'); then
    VERSION=$("$GIT" -C "$REPO" rev-parse --short HEAD | tr -d '\n')
fi

printf "%s" "$VERSION"
