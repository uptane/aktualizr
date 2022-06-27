#!/bin/bash

LOCATION=$(dirname ${BASH_SOURCE})

source "${LOCATION}/test_common.sh"

if [ "$1" != "install" ]; then
    terminate_with_signal USR1
fi

# Check the presence of expected variables:
SPECIFIC_VARS=(
  "SECONDARY_INSTALL_DATA"
  "SECONDARY_UPDATE_TYPE"
  "SECONDARY_FIRMWARE_PATH_PREV"
  "SECONDARY_FIRMWARE_PATH"
  "SECONDARY_FIRMWARE_SHA256"
#  "SECONDARY_IMAGE_PATH_OFFLINE"
#  "SECONDARY_METADATA_PATH_OFFLINE"
  "SECONDARY_CUSTOM_METADATA"
)

for vn in "${SHARED_VARS[@]}" "${SPECIFIC_VARS[@]}"; do
    #echo "$(date '+%Y/%m/%d-%H:%M:%S'): $vn=${!vn}" >> /tmp/VALUES.txt
    if [ -z "${!vn}" ]; then
        #echo "$(date '+%Y/%m/%d-%H:%M:%S'): $vn is empty" >> /tmp/FAILURE.txt
        terminate_with_signal USR2
    fi
done

handle_command "$TEST_COMMAND"
