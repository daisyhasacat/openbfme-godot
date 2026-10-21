extends RefCounted
## Runs outside the mounted AppImage. All paths are positional arguments, never shell code.
const SCRIPT := """#!/bin/sh
set -eu
unset APPIMAGE APPDIR ARGV0 OWD LD_LIBRARY_PATH
image=$1
candidate=$2
parent=$3
expected=$4
lock=$5
shift 5
exec >> "$image.update.log" 2>&1
cleanup() { rm -f -- "$candidate" "$lock/finish.sh" "$lock/previous"; rmdir -- "$lock" 2>/dev/null || true; }
trap cleanup EXIT
# The caller must leave before its file is replaced. Bound stale-PID waits.
i=0
while kill -0 "$parent" 2>/dev/null; do
    i=$((i+1))
    if [ "$i" -ge 120 ]; then echo 'Update aborted: game did not exit'; exit 1; fi
    sleep 1
done
actual=$(sha256sum -- "$candidate")
actual=${actual%% *}
[ "$actual" = "$expected" ] || { echo 'Update aborted: checksum changed'; exit 1; }
[ -f "$image" ] && [ ! -L "$image" ] || exit 1
[ ! -L "$image.previous" ] || exit 1
[ ! -e "$image.previous" ] || [ -f "$image.previous" ] || exit 1
chmod 755 "$candidate"
# Keep the old inode as a backup; rename the candidate over the original atomically.
cp -p -- "$image" "$lock/previous"
mv -fT -- "$lock/previous" "$image.previous"
mv -fT -- "$candidate" "$image"
cd -- "$(dirname -- "$image")"
"$image" "$@" &
child=$!
sleep 2
if kill -0 "$child" 2>/dev/null; then exit 0; fi
if wait "$child"; then exit 0; fi
echo 'New AppImage failed to start; restoring previous version'
mv -fT -- "$image.previous" "$image"
cleanup
trap - EXIT
exec "$image" "$@"
"""
