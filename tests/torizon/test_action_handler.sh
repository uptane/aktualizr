#!/bin/bash

LOCATION=$(dirname ${BASH_SOURCE})

source "${LOCATION}/test_common.sh"

handle_command "$1"
