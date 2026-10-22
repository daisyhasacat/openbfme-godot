#!/bin/bash
# Run on x86_64 Linux (including a remote builder launched from macOS).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ "$(uname -s)/$(uname -m)" = Linux/x86_64 ] || { echo 'AppImage builds require x86_64 Linux' >&2; exit 1; }
cd "$ROOT"
OUT=${OPENBFME_APPIMAGE_OUT:-$ROOT/workspace/appimage}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
export XDG_DATA_HOME="$OUT/tool-data"
python3 tools/release/appimage.py fetch-tools --directory "$OUT/tools" --templates "$XDG_DATA_HOME/godot/export_templates/4.7.2.stable"
tools/release/build_linux.sh
tools/release/package.sh --platforms linux --no-launcher --out "$OUT/package" --godot "$OUT/tools/godot"
version=$(git describe --tags --match 'v[0-9]*' --exclude '*-playtest.*' --always --abbrev=10)
package="$OUT/openbfme-$version-linux-x64"
python3 tools/release/make_archive.py --extract-all "$OUT/package/openbfme-$version-linux-x64.tar.gz" "$package"
python3 tools/release/appimage.py stage --package "$package" --output "$OUT/OpenBFME.AppDir"
desktop-file-validate "$OUT/OpenBFME.AppDir/openbfme.desktop"
export ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 VERSION="$version"
export SOURCE_DATE_EPOCH="$(git log -1 --format=%ct)"
image="$OUT/OpenBFME-$version-x86_64.AppImage"
"$OUT/tools/appimagetool" --no-appstream --runtime-file "$OUT/tools/runtime-x86_64" "$OUT/OpenBFME.AppDir" "$image"
# Run away from the project so installed native-library resolution is exercised.
(cd /tmp && timeout 90 "$image" --headless -- --appimage-boot-test) 2>&1 | tee "$OUT/appimage-boot.log"
grep -Fx 'APPIMAGE BOOT PASS' "$OUT/appimage-boot.log"
sha256sum "$image" > "$OUT/SHA256SUMS.txt"
echo "APPIMAGE READY: $image"
