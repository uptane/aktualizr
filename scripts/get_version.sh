#!/usr/bin/env bash

set -euo pipefail

GIT=${1:-git}
REPO=${2:-.}

# TODO: [TDX] Consider a more user friendly versioning scheme.
HASH=$("$GIT" -C "$REPO" rev-parse --short HEAD)
RES1=$?
if [ $RES1 -eq 0 ]; then
    DIRTY=""
    if ! "$GIT" -C "$REPO" diff --quiet || ! "$GIT" -C "$REPO" diff --cached --quiet; then
        DIRTY="-dirty"
    fi
    echo -n "tdx-${HASH}${DIRTY}"
    exit 0
fi

exit $RES1
