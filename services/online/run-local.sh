#!/usr/bin/env bash
# Build only the standalone relay, then run the combined server. No game assets required.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
if [[ "${1:-}" == --help ]]; then exec node "$HERE/server.mjs" --help; fi
BUILD="$HERE/.build/relay"
"${CMAKE:-cmake}" -S "$HERE/relay" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
"${CMAKE:-cmake}" --build "$BUILD" --parallel 2
exec node "$HERE/server.mjs" --relay-bin "$BUILD/openbfme_relay" "$@"
