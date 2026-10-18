#!/bin/bash
set -euo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)
ARCHIVE=${1:?usage: macos_start_check.sh openbfme-<version>-macos-universal.zip}
[ -n "${ROTWK_INSTALL:-}" ] && [ -n "${BFME2_INSTALL:-}" ] && [ -n "${RW_GAME_DAT:-}" ] \
  || { echo "MACOS START FAIL: set ROTWK_INSTALL, BFME2_INSTALL and RW_GAME_DAT"; exit 2; }
case "$ARCHIVE" in
  /*) ;;
  *) ARCHIVE="$PWD/$ARCHIVE" ;;
esac
ARCHIVE="$(cd "$(dirname "$ARCHIVE")" && pwd -P)/$(basename "$ARCHIVE")"
[ -f "$ARCHIVE" ] || { echo "MACOS START FAIL: archive not found"; exit 2; }

name=$(basename "$ARCHIVE" .zip)
check_base="$REPO/workspace/macos-package-check"
mkdir -p "$check_base"
workspace=$(mktemp -d "$check_base/$name.XXXXXX")
root="$workspace/$name"
mkdir "$root"
logs=$(mktemp -d "$check_base/$name-logs.XXXXXX")
run_cwd=$(mktemp -d "$check_base/$name-cwd.XXXXXX")
python3 "$REPO/tools/release/audit_package.py" --release macos-universal "$ARCHIVE" || exit 1
python3 "$REPO/tools/release/make_archive.py" --extract-all "$ARCHIVE" "$root" || exit 1

game="$root/OpenBFME.app/Contents/MacOS/OpenBFME"
launcher="$root/OpenBFMELauncher.app/Contents/MacOS/OpenBFME Launcher"
[ -f "$game" ] && [ -f "$launcher" ] || { echo "MACOS START FAIL: app executable missing after unpack"; exit 1; }
codesign --verify --deep --strict "$root/OpenBFME.app"
codesign --verify --deep --strict "$root/OpenBFMELauncher.app"
echo "MACOS START ROOT: $root"
echo "MACOS START LOGS: $logs"
echo "MACOS START CWD: $run_cwd"

rc=0
cd "$run_cwd"
for cpu in arm64 x86_64; do
  log="$logs/start-$cpu.log"
  status=0
  env ROTWK_INSTALL="$ROTWK_INSTALL" BFME2_INSTALL="$BFME2_INSTALL" RW_GAME_DAT="$RW_GAME_DAT" \
    arch -"$cpu" "$game" --headless --quit-after 400 >"$log" 2>&1 || status=$?
  if [ "$status" -ne 0 ]; then
    echo "MACOS START FAIL: $cpu game exited $status (log: $log)"
    rc=1
  elif ! grep -q '^GAME screen: MainMenu\.apt' "$log"; then
    echo "MACOS START FAIL: $cpu did not reach MainMenu.apt (log: $log)"
    rc=1
  elif ! grep -q '^GAME object world: ok=true, 4657 templates' "$log"; then
    echo "MACOS START FAIL: $cpu did not load exactly 4657 templates (log: $log)"
    rc=1
  else
    echo "MACOS START PASS: $cpu exit 0, MainMenu.apt, 4657 templates (log: $log)"
  fi
done
exit "$rc"
