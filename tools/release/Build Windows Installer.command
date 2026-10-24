#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
exec python3 "$ROOT/tools/release/windows_from_mac.py" --upload --output "$ROOT/build/windows-installers/$(date +%Y%m%d-%H%M%S)" "$@"
