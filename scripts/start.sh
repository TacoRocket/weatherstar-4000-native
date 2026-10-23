#!/bin/bash
set -euo pipefail
mkdir -p /data /stream/hls
children=()
cleanup() {
  trap - EXIT TERM INT
  for child in "${children[@]}"; do kill "$child" 2>/dev/null || true; done
  wait || true
}
trap cleanup EXIT TERM INT
(
  while true; do
    if ! weather-collect --state /data; then
      echo 'Weather collection failed; keeping previous snapshot' >&2
    fi
    sleep 300 & wait $!
  done
) &
children+=("$!")
weather-serve &
children+=("$!")
wait -n "${children[@]}"
exit 1
