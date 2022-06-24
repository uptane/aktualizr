#!/bin/bash

LOCATION=$(dirname ${BASH_SOURCE})

source "${LOCATION}/test_common.sh"

if [ "$1" != "get-firmware-info" ]; then
    terminate_with_signal USR1
fi

# Check the presence of expected variables:
SPECIFIC_VARS=(
  "SECONDARY_FWINFO_DATA"
)

for vn in "${SHARED_VARS[@]}" "${SPECIFIC_VARS[@]}"; do
    #echo "$(date '+%Y/%m/%d-%H:%M:%S'): $vn=${!vn}" >> /tmp/VALUES.txt
    if [ -z "${!vn}" ]; then
        #echo "$(date '+%Y/%m/%d-%H:%M:%S'): $vn is empty" >> /tmp/FAILURE.txt
        terminate_with_signal USR2
    fi
done

handle_command "$TEST_COMMAND"
