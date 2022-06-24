#!/bin/bash

SHARED_VARS=(
  "SECONDARY_INTERFACE_MAJOR"
  "SECONDARY_INTERFACE_MINOR"
  "SECONDARY_FIRMWARE_PATH"
  "SECONDARY_HARDWARE_ID"
  "SECONDARY_ECU_SERIAL"
)

terminate_with_signal() {
    local signame="${1:?No signal name or number provided}"
    echo "Performing a suicide with signal $signame"
    kill -$signame $$
}

exit_with_code() {
    local codenum="${1:?No exit code provided}"
    exit "$codenum"
}

exit_without_json_output() {
    local codenum="${1:?No exit code provided}"
    exit "$codenum"
}

exit_with_json_output() {
    local codenum="${1:?No exit code provided}"
    if [ -z "$TEST_JSON_OUTPUT" ]; then
        echo '{"status": "ok"}'
    else
        echo "$TEST_JSON_OUTPUT"
    fi
    exit "$codenum"
}

handle_command() {
    case "$1" in
        terminate-with-signal-*)
            terminate_with_signal "${1#terminate-with-signal-}"
            ;;
        exit-with-code-*)
            exit_with_code "${1#exit-with-code-}"
            ;;
        exit-without-json-output-code-*)
            exit_without_json_output "${1#exit-without-json-output-code-}"
            ;;
        exit-with-json-output-code-*)
            exit_with_json_output "${1#exit-with-json-output-code-}"
            ;;
    esac
}
