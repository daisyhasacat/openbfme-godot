#!/bin/bash
# Lane AUTOREL-1: publish an automatic preview release from a rebuild commit that passed the full gate (docs/RELEASE.md, "Automatic
# preview releases"). Runs on the Deck (the release key never leaves it), end to end without prompts:
#
#   tools/release/autorelease.sh <archive sha> [--since <archive sha>] [--dry-run | --plan] [--repo OWNER/NAME] [--remote NAME]
#                                [--archive-branch NAME]
#
#   1. checks: one release at a time (a lock); the sha must have a gate record ($STATE/gated.tsv, `<sha>\t<gate>\t<date>\tALL PASS`,
#      written by tools/release/record_gate.sh) and be on the archive branch; releases only move forward: the sha must descend from the
#      last released archive sha ($STATE/last-released) and differ from it, whatever --since says; the release is skipped when nothing
#      under engine/ godot/ launcher/ tools/release/ changed since the last released sha. --since only sets where the notes start (the
#      first release has no state and needs it);
#   2. syncs the sha to the public main: a commit on top of <remote>/main whose tree is the sha's without archive/ and without the paths of
#      the sha's tools/release/sync_exclude.txt (PUBLISH-2: in step with the CI mirror), with the public README line (no PRIVACY.md: the
#      owner removed it from the public main, PR #34): autorelease.py sync-tree. Before anything is pushed, the tree passes the privacy
#      audit (autorelease.py audit-tree: the public top-level allowlist, no workspace / archive / reference folder, nothing a .gitignore rule
#      ignores, the commit gate's file rules, and PUBLISH-2's denylist: the built-in rules and the operator's private one).
#      Pushed as sync/<date>-<sha>, a PR, merged with a merge commit whose tree must be the audited one (nothing to sync: main is tagged);
#      GHPR-1: right before, each reviewed community pull request of $STATE/community-prs.tsv whose port is in the sha is merged on GitHub
#      (its diff proven to be in the audited tree; tools/release/community_prs.py, docs/RELEASE.md "Community pull requests"), and the
#      sync PR's body credits it; --plan runs the same checks and prints what it would merge, --dry-run prints the merges as WOULD RUN;
#   3. tags the merged public commit with the release's version (annotated) and pushes the tag. Lane VERSION-1 (owner, 2026-10-14):
#      releases are plain numbers, v0.4.5 after v0.4.0-alpha.4, then v0.4.6, ... (--minor: the middle number + 1, v0.5.0, for a feature
#      the owner calls huge), released as "OpenBFME Alpha 0.4.5" (autorelease.py next-version / title) and published as full releases
#      (channel stable; the earlier alpha builds and previews were pre-releases). No bridge any more: the launchers up to
#      v0.3.0-preview.4 read plain tags, so a plain release with the launcher archives is what they install (autorelease.py
#      release-version; RELNAME-1's v0.3.0-preview.<n> bridges were for the alpha builds, which those launchers skip);
#   4. builds at the tag from a clean checkout on the build host, through the operator's builder ($OPENBFME_RELEASE_BUILDER, kept outside
#      this repository, PUBLISH-2; the interface of the tests' stand-in: <builder> <checkout> <version> <out dir>): the Linux GDExtension
#      (build_linux.sh, with Godot's own Linux SDK: GLIBC 2.28 at most, GLIBC-1 / DEP-1),
#      the Windows DLL (build_windows.sh --no-export), package.sh (game + launchers, both platforms), test_export.sh on the Linux
#      package, and a native Windows start of the Windows package (OpenBFME.console.exe --headless --quit-after 400 must exit 0 and print
#      "GAME screen: MainMenu.apt" and the template count; then, lane BUGS-1, a Men skirmish whose help box over a barracks bubble must draw
#      its text: windows_hud6_check.sh); the packages come back to $WORK/pkg;
#  4m. (MACREL-1) the macOS Apple Silicon package on the Mac build host (build_macos_release.sh: package_macos.py from a clean clone of
#      the tag, ad-hoc signed, nothing signed with the release key there); before anything is pushed (step 1) the host must answer and
#      have its tools (build_macos_release.sh --check): no release leaves without the Mac package unless OPENBFME_RELEASE_SKIP_MAC=1 is
#      set, which the log and the summary say (the release then carries a signed manifest-macos.json naming no Mac package);
#   5. checks the returned packages on the Deck, with the scripts of a trusted checkout ($WORK/trusted: a fresh worktree of the tag made
#      here, never written by the build host, whose tree must be the audited sync tree of the archive sha): exactly the package files, the
#      release allowlist of every archive, each library's build record against the tag and the engine id of the tag's engine/, and
#      package.sh --verify (a byte for byte rebuild of every archive from the trusted checkout with the returned libraries);
#   6. seal_package.py (the packages' signed payload inventory, lane ONEZIP-1 r2), make_manifest.py and the signature, all from the trusted
#      checkout, the key $OPENBFME_RELEASE_KEY;
#      MACREL-1: the Mac package is checked here too (the strict audit, its checksum, VERSION naming the tag and its commit, the Mac
#      launcher built with launcher/release_key.pub and the target repository: make_macos_manifest.py --verify-package), then listed in
#      SHA256SUMS after the sealing, and manifest-macos.json (make_macos_manifest.py) is signed separately with the same key, so
#      manifest.json stays byte for byte what the Windows / Linux launchers read;
#   7. release notes for players from the first-parent subjects since the notes baseline (autorelease.py notes: neutralised, the studio
#      shape of docs/RELEASE.md "Patch notes"; --plan also prints NOTES WARNING lines for a Release-note that needs fixing);
#   8. the trusted checkout's publish_release.sh --execute --confirm-tag <tag> (a draft, its assets checked, then published, every
#      outcome read back), then the release is read back once more;
#  8b. (RELTEST-1) the repository's homepage (the About link) set to the new release's page, read back (the releases up to
#      v0.4.0-alpha.4 were all pre-releases, so GitHub showed no "Latest release"; the plain releases are, and the About link still points
#      at the newest one). A failure there is a warning in the summary (the release is out; set the link by hand), not a failed release;
#   9. the Discord announcement, if bfme-community offers a release command (it does not yet: logged);
#  10. a one-line summary on stdout, the log in $LOGS/autorelease-<version>.log, the state.
# On a failure it stops and publishes nothing further; publish_release.sh deletes only a draft it created and read back as a draft; a
# pushed tag stays when no release was created (the repository's ruleset forbids deleting tags, GH013): the open intent makes the next real
# run resume that same version from it (RELTEST-1); the step and the error are named. Nothing is retried within a run.
# Release intent (r3, Sol r2: a failure after publishing left stale state and the next run released older source under a newer version):
#   before main moves or a tag is pushed, $STATE/intent records the sha, the version, the audited tree, the public commit and the step
#   (and $STATE/intent.rid the id of the draft publish_release.sh created). Every real run first reconciles an open intent through
#   GitHub API reads, failing closed when a read fails: a published release with the expected assets (the intent's package folder) is
#   recorded as released; a draft is deleted only when it is the intent's own (intent.rid), then the release is resumed; a tag without a
#   release resumes publishing that same sha and tag. While an intent is open no other sha is accepted: the run resumes the intent's sha
#   and exits 3 ("resumed; the requested sha is not released"). Dry runs and --plan refuse to start while an intent is open.
# Control scripts (r3): the sha must contain MIN_CONTROL (the commit of the r3 release scripts), and every file of the trusted checkout's
#   tools/release and tools/precommit.py must be byte for byte the running autorelease's own copy (an old or modified publish_release.sh
#   never runs).
# The release key's path is never printed: messages, the dry run's WOULD RUN line and the signer's own output show <release key>.
#
# --dry-run: the same run against the real repository without publishing or writing anything to GitHub (r3): the sync branch push, the
#   PR, the merge and the tag push are printed, not run; the tag stays local (deleted at the end), the build / package / tests / Deck checks run in full,
#   the manifest is signed with a throwaway key (the real key is only checked to match launcher/release_key.pub), publish_release.sh runs
#   as a dry run. Every command a real run would add is printed as "WOULD RUN: ...".
# --plan: step 1, the version and the public tree with its privacy audit (PUBLISH-2); nothing is written to GitHub or the state.
# --minor: this release raises the middle number (v0.4.7 -> v0.5.0; VERSION-1, the owner's call for a huge feature); ignored when an open
#   intent is resumed (its version is kept).
# Environment (defaults): OPENBFME_RELEASE_STATE (~/.local/state/openbfme-release), OPENBFME_RELEASE_WORK (~/.cache/openbfme-release),
#   GH (gh), BFME_COMMUNITY (~/.local/bin/bfme-community),
#   required, the operator's private release environment (PUBLISH-2: the public tree names no private path or host):
#   OPENBFME_RELEASE_LOGS (the log folder), OPENBFME_RELEASE_KEY (the release key file: an absolute, normalised path, r6),
#   OPENBFME_RELEASE_DENYLIST (the private denylist, autorelease.py load_denylist()), OPENBFME_RELEASE_BUILDER (step 4; not with a stand-in),
#   OPENBFME_RELEASE_TEMPLATES (4657: the templates the Windows start must load),
#   OPENBFME_RELEASE_SKIP_MAC (unset or 0; 1: release without the macOS package, logged), OPENBFME_RELEASE_MAC_HOST (mac),
#   OPENBFME_RELEASE_LEGACY_LAUNCHERS (1; lane ONEZIP-1: 0 leaves the launcher-only archives out of the release, manifest format 3:
#   only once the launchers published before ONEZIP-1 are retired, docs/RELEASE.md "One archive": they cannot read such a release).
#   Tests only: OPENBFME_RELEASE_MIN_COMMIT replaces MIN_CONTROL (stand-in mode only).
#   Tests only: OPENBFME_RELEASE_STANDIN=<dir> replaces step 4 by <dir>/build <checkout> <version> <out dir> and step 5's package checks by
#   <dir>/verify <trusted checkout> <package dir> <version>; refused unless --repo is not the real repository and the remote is a local
#   folder. MACREL-1: it also replaces step 4m by <dir>/build-mac <checkout> <version> <out dir> (the Deck's Mac checks still run), and
#   OPENBFME_RELEASE_MAC_SSH (a stand-in ssh for the Mac check) is accepted in that mode only.
set -Eeuo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
HERE="$REPO/tools/release"
SHA_ARG=""; SINCE=""; MODE=real; BUMP=patch; TARGET="Open-BFME/openbfme-godot"; REMOTE=origin; ARCHIVE=archive-legacy-codebase
while [ $# -gt 0 ]; do
  case "$1" in
    --since) SINCE=$2; shift 2 ;;
    --dry-run) MODE=dry; shift ;;
    --plan) MODE=plan; shift ;;
    --minor) BUMP=minor; shift ;;
    --repo) TARGET=$2; shift 2 ;;
    --remote) REMOTE=$2; shift 2 ;;
    --archive-branch) ARCHIVE=$2; shift 2 ;;
    -h|--help) sed -n '2,93p' "$0"; exit 0 ;;
    -*) echo "autorelease.sh: an unknown option (not printed: the redactor is not running yet, PUBLISH-2 r4); see --help" >&2; exit 2 ;;
    *) [ -z "$SHA_ARG" ] || { echo "autorelease.sh: one archive sha" >&2; exit 2; }; SHA_ARG=$1; shift ;;
  esac
done
STATE=${OPENBFME_RELEASE_STATE:-$HOME/.local/state/openbfme-release}
WORK_ROOT=${OPENBFME_RELEASE_WORK:-$HOME/.cache/openbfme-release}
# PUBLISH-2: the private values come from the operator's release environment, never from defaults in this public file
for v in OPENBFME_RELEASE_LOGS OPENBFME_RELEASE_KEY OPENBFME_RELEASE_DENYLIST; do
  [ -n "${!v:-}" ] || { echo "AUTORELEASE FAIL: $v is not set (the operator's private release environment)" >&2; exit 2; }
done
LOGS=$OPENBFME_RELEASE_LOGS
DENYLIST=$OPENBFME_RELEASE_DENYLIST
[ -f "$DENYLIST" ] || { echo "AUTORELEASE FAIL: OPENBFME_RELEASE_DENYLIST is not a file" >&2; exit 2; }
# r6: the key is named by one exact path (Sol r5: a relative override leaked its normalised path through the signer's diagnostics): an
# absolute, normalised path (no relative part, no symbolic link); anything else is refused without echoing what was given
KEY=$OPENBFME_RELEASE_KEY
if [ "${KEY#/}" = "$KEY" ] || [ "$(realpath -m -- "$KEY")" != "$KEY" ]; then
  echo "AUTORELEASE FAIL: OPENBFME_RELEASE_KEY is not the configured release key (an absolute, normalised path): only that key is used" >&2
  exit 2
fi
export OPENBFME_RELEASE_KEY   # autorelease.py audit-text: the key's path and folder are private words
BUILDER=${OPENBFME_RELEASE_BUILDER:-}
GH=${GH:-gh}; export GH
COMMUNITY=${BFME_COMMUNITY:-$HOME/.local/bin/bfme-community}
TEMPLATES=${OPENBFME_RELEASE_TEMPLATES:-4657}
LEGACY=${OPENBFME_RELEASE_LEGACY_LAUNCHERS:-1}
case $LEGACY in 1) PKGOPT=""; FORMAT=2 ;; 0) PKGOPT="--no-legacy-launchers"; FORMAT=3 ;;
  *) echo "AUTORELEASE FAIL: OPENBFME_RELEASE_LEGACY_LAUNCHERS is 1 or 0" >&2; exit 2 ;; esac
STANDIN=${OPENBFME_RELEASE_STANDIN:-}
# MACREL-1: the macOS package is part of every release; only OPENBFME_RELEASE_SKIP_MAC=1, given explicitly, releases without it
SKIP_MAC=${OPENBFME_RELEASE_SKIP_MAC:-0}
case $SKIP_MAC in 0|1) ;; *) echo "AUTORELEASE FAIL: OPENBFME_RELEASE_SKIP_MAC is 1 (release without the macOS package) or unset" >&2; exit 2 ;; esac
MAC_HOST=${OPENBFME_RELEASE_MAC_HOST:-mac}
# ---- the one redaction point (r5, Sol r4: an intent field printed the key path through an echo) -------------------------------------------
# Every line this script and the helpers it runs print, stdout and stderr, and every line of its logs, goes through redact: the release
# key's path, its folder and the home folder (both as given and resolved) become <release key>, <release key folder> and ~.
KEYDISP="<release key>"
export AR_KEY=$KEY AR_KEYREAL=$(realpath -m "$KEY") AR_KEYDIR=$(dirname "$KEY") AR_KEYDIRREAL=$(dirname "$(realpath -m "$KEY")")
export AR_HOME=$HOME AR_HOMEREAL=$(realpath -m "$HOME")
export AR_TOOLS=$HERE
# PUBLISH-2 r3 (review F5): the redactor is autorelease.py redact (also publish_release.sh's): the paths above, then every match of the
# built-in and the private rules; it fails closed (a list it cannot load prints one line and nothing of the output), and the list is
# loaded once here first, so a broken list stops the run before anything is printed
why=$(python3 "$HERE/autorelease.py" redact --check --denylist "$DENYLIST" 2>&1) \
  || { echo "AUTORELEASE FAIL: OPENBFME_RELEASE_DENYLIST cannot be loaded: ${why#REDACTOR FAILED: }" >&2; exit 2; }
redact() { python3 -u "$HERE/autorelease.py" redact --denylist "$DENYLIST"; }
exec > >(redact) 2> >(redact >&2)
# VERSION-1 (Sol): a format 3 release (no launcher archives) is refused for as long as any published launcher reads format 2 only; derived
# from each release tag's own manifest.gd, so the day those launchers are retired from the tags the switch works again. PUBLISH-2 r4:
# checked after the redactor starts (the answer it prints can quote git's errors)
if [ "$LEGACY" = 0 ]; then
  rc=0; why=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" legacy-needed --remote "$REMOTE" 2>&1) || rc=$?
  if [ "$rc" != 10 ]; then   # 10: none does; 0: a launcher needs the archives; anything else: no answer (fail closed)
    echo "$why" >&2; echo "AUTORELEASE FAIL: OPENBFME_RELEASE_LEGACY_LAUNCHERS=0 is refused (above)" >&2; exit 2
  fi
fi
REAL_REPO="Open-BFME/openbfme-godot"
# Sol r1: `X=...$([ "$MODE" = dry ] && echo -dryrun)` returned 1 outside dry runs and stopped every real run under set -e
SUFFIX=""; if [ "$MODE" = dry ]; then SUFFIX=-dryrun; fi

# the first commit of the r6 control scripts (a text-only public tree, audited public metadata, one key path): older shas are refused
MIN_CONTROL=126d3014d5758739c7e00016fd33d795949b1b5a
if [ -n "$STANDIN" ] && [ -n "${OPENBFME_RELEASE_MIN_COMMIT:-}" ]; then MIN_CONTROL=$OPENBFME_RELEASE_MIN_COMMIT; fi
INTENT="$STATE/intent"

STEP=start; VERSION=""; BRANCH_PUSHED=""; TAG_LOCAL=""; TAG_PUSHED=0; PUBLISHED=0; WT=""; TRUSTED=""; LOG=""; RESUME=0; REQUESTED=""
keyed() { "$@"; }   # a command that is given the key (its output is redacted with everything else: the redactor below)
summary() { echo "$*"; if [ "$MODE" != plan ]; then mkdir -p "$LOGS" && echo "$(date -u +%FT%TZ) $*" | redact >> "$LOGS/autorelease-summary.log"; fi; }
intent_get() { if [ -f "$INTENT" ]; then sed -n "s/^$1=//p" "$INTENT" | tail -1; fi; }
intent_set() {  # key=value ...: rewritten atomically, so a crash leaves the old or the new record
  local kv tmp="$INTENT.tmp"
  if [ -f "$INTENT" ]; then cp "$INTENT" "$tmp"; else : > "$tmp"; fi
  for kv in "$@"; do grep -v "^${kv%%=*}=" "$tmp" > "$tmp.2" || true; mv "$tmp.2" "$tmp"; printf '%s\n' "$kv" >> "$tmp"; done
  sync "$tmp" 2>/dev/null || true
  mv "$tmp" "$INTENT"
}
record_released() {  # <sha> <version> <public>: the state moves forward, the intent closes
  echo "$1 $2 $3 $(date -u +%FT%TZ)" > "$STATE/last-released.tmp"
  mv "$STATE/last-released.tmp" "$STATE/last-released"
  echo "$1 $2 $3 $(date -u +%FT%TZ)" >> "$STATE/releases.log"
  rm -f "$INTENT" "$STATE/intent.rid"
}
describe_tag() {  # the tags git describe considers for the build's version (BuildVersion.cmake: --match "v[0-9]*" --exclude "*-playtest.*")
  case "$1" in *-playtest.*) return 1 ;; v[0-9]*) return 0 ;; *) return 1 ;; esac
}
git_() { git -C "$REPO" "$@"; }
would() { printf 'WOULD RUN:'; printf ' %q' "$@"; echo; }
cleanup() {  # always: the dry run's traces and the work trees. Releases are never deleted here (publish_release.sh owns its draft).
  local rc=$?
  trap - ERR
  set +e
  if [ "$MODE" = dry ]; then
    if [ -n "$BRANCH_PUSHED" ]; then echo "AUTORELEASE dry run: deleting the pushed branch $BRANCH_PUSHED"; git_ push -q "$REMOTE" --delete "$BRANCH_PUSHED"; fi
    if [ -n "$TAG_LOCAL" ]; then echo "AUTORELEASE dry run: deleting the local tag $TAG_LOCAL"; git_ tag -d "$TAG_LOCAL" >/dev/null; fi
  elif [ $rc != 0 ] && [ "$TAG_PUSHED" = 1 ] && [ "$PUBLISHED" = 0 ]; then
    # RELTEST-1: tags cannot be deleted on GitHub (a repository ruleset, GH013), and none needs to be: the intent stays open
    echo "AUTORELEASE: the tag $VERSION stays on $REMOTE with no release yet; the release intent stays open: run autorelease.sh again (for this sha or a later one) and it resumes $VERSION of ${SHA:0:10} from this tag. Do not delete the tag (the repository's ruleset forbids it, GH013)"
  elif [ $rc != 0 ] && [ -n "$TAG_LOCAL" ]; then
    echo "AUTORELEASE: deleting the local tag $TAG_LOCAL (it was never pushed)"; git_ tag -d "$TAG_LOCAL" >/dev/null
  fi
  if [ $rc != 0 ] && [ -n "$BRANCH_PUSHED" ] && [ "$MODE" != dry ]; then
    echo "AUTORELEASE: the sync branch $BRANCH_PUSHED stays on $REMOTE${PR:+ (PR $PR)}: check it by hand"
  fi
  for w in "$WT" "$TRUSTED"; do
    if [ -n "$w" ] && [ -d "$w" ]; then git_ worktree remove --force "$w" 2>/dev/null; fi
  done
  exit $rc
}
fail() {  # in a command substitution only leave it: the caller's `|| fail` or the ERR trap names the step
  if [ "$BASH_SUBSHELL" -gt 0 ]; then echo "  (line ${BASH_LINENO[0]}: $*)" >&2; exit 1; fi
  summary "AUTORELEASE FAIL at step $STEP${VERSION:+ ($VERSION)}: $*"; exit 1; }
trap cleanup EXIT
trap 'fail "command failed (line $LINENO): $BASH_COMMAND"' ERR

# ---- 1. lock, gate record, archive branch, forward only, skip-when-unchanged -------------------------------------------------------------
STEP=check
[ -n "$SHA_ARG" ] || fail "usage: autorelease.sh <archive sha> [--since <archive sha>] [--dry-run]"
mkdir -p "$STATE"
exec 8>"$STATE/autorelease.lock"
flock -n 8 || fail "another autorelease is running (lock $STATE/autorelease.lock)"
if [ -n "$STANDIN" ]; then
  url=$(git_ remote get-url "$REMOTE" 2>/dev/null) || fail "no remote $REMOTE"
  [ "$TARGET" != "$REAL_REPO" ] && [ -d "$url" ] || fail "OPENBFME_RELEASE_STANDIN is for tests: only with a stand-in --repo and a remote that is a local folder"
  echo "AUTORELEASE stand-in mode ($STANDIN): tests only"
fi
[ -z "${OPENBFME_RELEASE_MAC_SSH:-}" ] || [ -n "$STANDIN" ] || fail "OPENBFME_RELEASE_MAC_SSH is for tests (stand-in mode) only"
# ---- an open release intent: reconciled first, by GitHub reads only (fail closed) ----------------------------------------------------------
# The intent file is untrusted (r4, Sol r3: changing only its sha let older source get the next version): every field is checked against
# the repository, the remote and the state before anything is recorded or resumed; a mismatch stops the run and changes nothing.
RESUME_RID=""
intent_bad() { fail "the release intent $INTENT does not hold: $*. Nothing was changed; repair it by hand (docs/RELEASE.md, operator steps)"; }
validate_intent() {  # $1 = published: the release of the intent's version is published (its manifest is checked too)
  local e lv lastver=""
  [[ "$I_SHA" =~ ^[0-9a-f]{40}$ ]] && git_ cat-file -e "$I_SHA^{commit}" 2>/dev/null || intent_bad "its sha '$I_SHA' is not a commit of this repository"
  git_ merge-base --is-ancestor "$I_SHA" "refs/heads/$ARCHIVE" || intent_bad "$I_SHA is not on $ARCHIVE"
  awk -F'\t' -v s="$I_SHA" '$1 == s && $4 == "ALL PASS" { f = 1 } END { exit !f }' "$STATE/gated.tsv" 2>/dev/null || intent_bad "$I_SHA has no gate record"
  git_ merge-base --is-ancestor "$MIN_CONTROL" "$I_SHA" || intent_bad "$I_SHA predates the release scripts ($MIN_CONTROL)"
  if [ -f "$STATE/last-released" ]; then
    read -r lv lastver _ < "$STATE/last-released"
    [ "$I_SHA" != "$lv" ] && git_ merge-base --is-ancestor "$lv" "$I_SHA" || intent_bad "$I_SHA does not descend from the last released archive sha $lv"
  fi
  # the tree: exactly the sync recipe's public tree of the intent's sha (PUBLISH-2: also without its sync_exclude.txt paths)
  [[ "$I_TREE" =~ ^[0-9a-f]{40}$ ]] && [ "$(git_ cat-file -t "$I_TREE" 2>/dev/null)" = tree ] || intent_bad "its tree '$I_TREE' is not a tree here"
  [ "$(python3 "$HERE/autorelease.py" --git-dir "$REPO" sync-tree --sha "$I_SHA")" = "$I_TREE" ] || intent_bad "its tree is not the public tree of $I_SHA"
  git_ fetch -q "$REMOTE" main || fail "cannot fetch $REMOTE/main to reconcile (fail closed)"
  RTAGS=$(git_ ls-remote --tags "$REMOTE") || fail "cannot list the tags of $REMOTE to reconcile (fail closed)"
  # VERSION-1 / RELNAME-1: a plain release (from v0.4.5 on), an alpha build v0.4.0-alpha.<n> or a bridge v0.3.0-preview.<n>, above every
  # version of its own line released or tagged
  # (Sol r2: with only the last released one compared, an intent edited from alpha.2 to alpha.0 after alpha.1 and a bridge published)
  printf '%s\n' "$RTAGS" | python3 "$HERE/autorelease.py" intent-version "$I_VER" "$lastver" >/dev/null 2>&1 \
    || intent_bad "its version '$I_VER' is not a plain release, v0.4.0-alpha.<n> or v0.3.0-preview.<n> above every released or tagged version of its line"
  local tagc; tagc=$(printf '%s\n' "$RTAGS" | awk -v t="refs/tags/$I_VER" '$2 == t "^{}" { p = $1 } $2 == t && !p { l = $1 } END { print (p ? p : l) }')
  if [ -n "$I_PUBLIC" ]; then
    git_ cat-file -e "$I_PUBLIC^{commit}" 2>/dev/null || intent_bad "its public commit $I_PUBLIC is not known here"
    [ "$(git_ rev-parse "$I_PUBLIC^{tree}")" = "$I_TREE" ] || intent_bad "its public commit $I_PUBLIC does not have its tree"
    git_ merge-base --is-ancestor "$I_PUBLIC" "refs/remotes/$REMOTE/main" || intent_bad "its public commit $I_PUBLIC is not on $REMOTE/main"
    [ -z "$tagc" ] || [ "$tagc" = "$I_PUBLIC" ] || intent_bad "the tag $I_VER on $REMOTE is on $tagc, not on its public commit $I_PUBLIC"
  else
    [ -z "$tagc" ] || intent_bad "the tag $I_VER is on $REMOTE but the intent names no public commit"
  fi
  if [ "${1:-}" = published ]; then
    [ -n "$I_PUBLIC" ] && [ -n "$tagc" ] || intent_bad "$I_VER is published but the intent has no public commit or the tag is missing"
    # the published manifest and signature are the intent's package folder's, and name this version, public commit and repository
    local a f
    for f in manifest.json manifest.json.sig $([ -f "$I_WORK/pkg/manifest-macos.json" ] && echo manifest-macos.json manifest-macos.json.sig); do
      a=$(python3 -c 'import json,sys; print(next((str(x["id"]) for x in json.load(open(sys.argv[1])).get("assets") or [] if x.get("name") == sys.argv[2]), ""))' "$RJ" "$f")
      [ -n "$a" ] || intent_bad "the published $I_VER has no $f"
      "$GH" api -H "Accept: application/octet-stream" "repos/$TARGET/releases/assets/$a" > "$RJ.$f" || fail "cannot download $f of $I_VER (fail closed)"
      cmp -s "$RJ.$f" "$I_WORK/pkg/$f" || intent_bad "the published $f of $I_VER is not the one in $I_WORK/pkg"
    done
    python3 - "$I_WORK/pkg/manifest.json" "$I_VER" "$I_PUBLIC" "$TARGET" <<'PY' || intent_bad "the published manifest does not name $I_VER, $I_PUBLIC and $TARGET"
import json, sys
m = json.load(open(sys.argv[1]))
sys.exit(0 if (m.get("version"), m.get("commit"), m.get("repo")) == tuple(sys.argv[2:5]) else 1)
PY
    keyed python3 "$HERE/sign_manifest.py" --verify "$(git_ show "$I_SHA:launcher/release_key.pub" | tr -d '[:space:]')" "$I_WORK/pkg/manifest.json" \
      || intent_bad "the published manifest's signature does not verify under launcher/release_key.pub of $I_SHA"
    if [ -n "$STANDIN" ]; then
      echo "AUTORELEASE reconcile: stand-in mode: the libraries' engine id is not checked (tests)"
    else
      # the engine id: the Linux library inside the published package was built from the public commit's engine/
      local L; L=$(mktemp -d)
      tar -xzf "$I_WORK/pkg/openbfme-$I_VER-linux-x64.tar.gz" -C "$L" "openbfme-$I_VER-linux-x64/openbfme.linux.template_debug.x86_64.so" \
        || intent_bad "the package folder has no Linux library"
      mkdir -p "$L/src"; git_ archive --format=tar "$I_PUBLIC" engine | tar -x -C "$L/src"
      local lib="$L/openbfme-$I_VER-linux-x64/openbfme.linux.template_debug.x86_64.so" rec x86 opts id
      rec=$(python3 "$HERE/lib_provenance.py" --record "$lib") || intent_bad "the published Linux library has no single build record"
      x86=$(echo "$rec" | sed -n 's/^x86-32=//p'); opts=$(echo "$rec" | sed -n 's/^id-options=//p')
      id=$(python3 "$HERE/lib_provenance.py" --engine-id-of "$L/src/engine" --x86-32 "${x86:-OFF}" --id-options "$opts")
      python3 "$HERE/lib_provenance.py" "$lib" --version "$I_VER" --commit "$I_PUBLIC" --engine-id "$id" \
        || intent_bad "the published Linux library is not the build of $I_VER at $I_PUBLIC (engine id)"
      rm -rf "$L"
    fi
  fi
}
if [ -f "$INTENT" ]; then
  STEP=reconcile
  I_SHA=$(intent_get sha); I_VER=$(intent_get version); I_STEP=$(intent_get step); I_WORK=$(intent_get work)
  I_TREE=$(intent_get tree); I_PUBLIC=$(intent_get public)
  [ -n "$I_SHA" ] && [ -n "$I_VER" ] || intent_bad "it is incomplete"
  [ "$MODE" = real ] || fail "an unfinished release is open ($I_VER of $I_SHA, step $I_STEP, $INTENT): only a real run reconciles it"
  command -v "$GH" >/dev/null || fail "gh not found"
  echo "AUTORELEASE reconcile: an open release intent: $I_VER of $I_SHA (step $I_STEP)"
  ids=$("$GH" api --paginate "repos/$TARGET/releases?per_page=100" --jq ".[] | select(.tag_name == \"$I_VER\") | .id") \
    || fail "cannot list the releases of $TARGET to reconcile $I_VER (fail closed)"
  n=$(printf '%s\n' "$ids" | grep -c . || true)
  [ "$n" -le 1 ] || fail "$TARGET has $n releases for $I_VER: check them by hand"
  if [ "$n" = 1 ]; then
    RJ=$(mktemp)
    "$GH" api "repos/$TARGET/releases/$ids" > "$RJ" || fail "cannot read the release $ids ($I_VER) back (fail closed)"
    draft=$(python3 -c 'import json,sys; print(str(json.load(open(sys.argv[1])).get("draft")).lower())' "$RJ")
    if [ "$draft" = false ]; then
      [ -f "$I_WORK/pkg/manifest.json" ] || fail "$I_VER is published, but its package folder $I_WORK/pkg is gone: verify it by hand, then record it in $STATE/last-released and delete $INTENT"
      # VERSION-1: a plain release is a full release, an alpha build or a bridge a pre-release (the channel of its version)
      case $I_VER in *-*) I_PRE=true ;; *) I_PRE=false ;; esac
      python3 "$HERE/release_assets.py" --release-json "$RJ" --dir "$I_WORK/pkg" --version "$I_VER" --draft false --prerelease "$I_PRE" --repo "$TARGET" \
        || intent_bad "$I_VER is published but does not match its package folder $I_WORK/pkg"
      validate_intent published
      record_released "$I_SHA" "$I_VER" "$I_PUBLIC"
      summary "AUTORELEASE reconciled: $I_VER of $I_SHA was published (an earlier run's answer was lost): recorded as the last release"
    elif [ "$draft" = true ]; then
      rid=$(cat "$STATE/intent.rid" 2>/dev/null || true)
      [ -n "$rid" ] && [ "$rid" = "$ids" ] || fail "a draft release $ids for $I_VER exists that this intent did not record creating: check it by hand"
      validate_intent
      # never deleted (r4): the draft is resumed by publish_release.sh --resume-id once the packages are rebuilt and checked again
      echo "AUTORELEASE reconcile: the intent's own draft $rid ($I_VER) is resumed"
      RESUME_RID=$rid
      RESUME=1
    else
      fail "the release $ids ($I_VER) reads back as draft=$draft: check it by hand"
    fi
  else
    validate_intent
    RESUME=1
  fi
  if [ $RESUME = 1 ]; then
    if [ "$SHA_ARG" != "$I_SHA" ] && [ "$(git_ rev-parse -q --verify "$SHA_ARG^{commit}" || true)" != "$I_SHA" ]; then
      REQUESTED=$SHA_ARG
      echo "AUTORELEASE reconcile: the open release of $I_SHA ($I_VER) is resumed first; $REQUESTED is not considered in this run"
    fi
    SHA_ARG=$I_SHA
  fi
fi
STEP=check
SHA=$(git_ rev-parse -q --verify "$SHA_ARG^{commit}") || fail "$SHA_ARG is not a commit"
# the release scripts this sha carries must be the reviewed ones (Sol r2: a gated older sha supplied its old publisher)
git_ cat-file -e "$MIN_CONTROL^{commit}" 2>/dev/null || fail "the release scripts' commit $MIN_CONTROL is not in this repository"
git_ merge-base --is-ancestor "$MIN_CONTROL" "$SHA" || fail "$SHA predates the release scripts of AUTOREL-1 r3 ($MIN_CONTROL): its publish_release.sh and package checks are not the reviewed ones, so it is not released automatically"
# the gate record of this very commit (never ancestry as a stand-in for it)
if ! awk -F'\t' -v s="$SHA" '$1 == s && $4 == "ALL PASS" { found = 1 } END { exit !found }' "$STATE/gated.tsv" 2>/dev/null; then
  fail "$SHA has no gate record in $STATE/gated.tsv (tools/release/record_gate.sh <sha> <remote-verify name> after its RESULT ALL PASS)"
fi
git_ rev-parse -q --verify "refs/heads/$ARCHIVE" >/dev/null || fail "no branch $ARCHIVE"
git_ merge-base --is-ancestor "$SHA" "refs/heads/$ARCHIVE" || fail "$SHA is not on $ARCHIVE"
LAST=""; if [ -f "$STATE/last-released" ]; then LAST=$(awk 'NR==1 {print $1}' "$STATE/last-released"); fi
if [ -n "$LAST" ]; then
  # releases only move forward, whatever --since says (Sol r1: --since let an older sha get a newer version)
  [ "$SHA" != "$LAST" ] || fail "$SHA is the last released archive sha"
  if git_ merge-base --is-ancestor "$SHA" "$LAST"; then fail "$SHA is older than the last released archive sha $LAST"; fi
  git_ merge-base --is-ancestor "$LAST" "$SHA" || fail "$SHA does not descend from the last released archive sha $LAST"
fi
if [ $RESUME = 1 ] && [ -n "$(intent_get base)" ]; then BASE=$(intent_get base)
elif [ -n "$SINCE" ]; then BASE=$(git_ rev-parse -q --verify "$SINCE^{commit}") || fail "--since $SINCE is not a commit"
elif [ -n "$LAST" ]; then BASE=$LAST
else fail "no release has been made yet ($STATE/last-released is missing): pass --since <archive sha of the last sync to main> for the notes"; fi
git_ merge-base --is-ancestor "$BASE" "$SHA" || fail "the notes baseline $BASE is not an ancestor of $SHA"
if [ -n "$LAST" ] && [ $RESUME = 0 ]; then
  if ! changed=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" changed --from "$LAST" --to "$SHA"); then
    summary "AUTORELEASE SKIP $SHA: nothing under engine/ godot/ launcher/ tools/release/ changed since the last released archive sha $LAST"
    exit 0
  fi
  echo "AUTORELEASE $changed since $LAST"
fi
if [ $RESUME = 1 ]; then VERSION=$I_VER
else VERSION=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" release-version --remote "$REMOTE" --repo "$TARGET" --sha "$SHA" --gh "$GH" --bump "$BUMP") \
  || fail "no version for this release (autorelease.py release-version)"; fi
DATE=$(date -u +%Y-%m-%d)
# GHPR-1: reviewed community pull requests (community_prs.py, the operator's list in $STATE). --plan: what this release would merge first
# and credit, every check run (GitHub read, never written)
community() { python3 "$HERE/community_prs.py" --git-dir "$REPO" --repo "$TARGET" --remote "$REMOTE" --gh "$GH" --state "$STATE" "$@"; }
if [ "$MODE" = plan ]; then
  # PUBLISH-2: the public tree this release would publish, built and audited as step 2 does (nothing pushed); GHPR-1's checks run on it
  PTREE=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" sync-tree --sha "$SHA") || fail "the sync recipe could not build the public tree of $SHA"
  python3 "$HERE/autorelease.py" --git-dir "$REPO" audit-tree --tree "$PTREE" --denylist "$DENYLIST" || fail "the public tree $PTREE failed the privacy audit"
  community plan --sha "$SHA" || fail "a reviewed community pull request cannot land with this release (above)"
  python3 "$HERE/public_workflows.py" --git-dir "$REPO" --tree "$PTREE" || fail "the public tree's .github folder breaks the public workflow policy (above)"
  summary "AUTORELEASE PLAN $VERSION from $SHA (notes since $BASE), public tree $PTREE"
  python3 "$HERE/autorelease.py" --git-dir "$REPO" warnings --from "$BASE" --to "$SHA" --denylist "$DENYLIST"   # NOTES-1: a Release-note that needs splitting; never fails
  exit 0
fi
# ---- MACREL-1: the Mac build host answers and is ready, before anything is pushed (a release without the Mac package is the operator's call) --
STEP=mac-check
MACOPT="--mac-manifest --mac"   # the notes' download rows (autorelease.py download_section)
if [ "$SKIP_MAC" = 1 ]; then
  MACOPT=--mac-manifest
  summary "AUTORELEASE WARNING: OPENBFME_RELEASE_SKIP_MAC=1: $VERSION is released WITHOUT the macOS package (manifest-macos.json names no Mac package; Macs keep their version)"
else
  bash "$HERE/build_macos_release.sh" --check \
    || fail "the Mac build host ($MAC_HOST) is not ready (above): nothing was pushed or published. Wake it / fix it and run again, or set OPENBFME_RELEASE_SKIP_MAC=1 to release without the macOS package"
fi
mkdir -p "$LOGS" "$WORK_ROOT"
LOG="$LOGS/autorelease-$VERSION$SUFFIX.log"
: > "$LOG"
exec > >(redact | tee -a "$LOG") 2>&1   # the log is written redacted too
echo "AUTORELEASE $VERSION ($MODE$([ $RESUME = 1 ] && echo ', resumed' || true)) from $ARCHIVE $SHA, notes since $BASE, target $TARGET, $(date -u +%FT%TZ)"
WORK="$WORK_ROOT/$VERSION$SUFFIX"
rm -rf "$WORK"; mkdir -p "$WORK"
# the release key: present, private, and the key whose public half the launcher is built with (only its public half is printed)
[ -f "$KEY" ] || fail "the release key $KEYDISP does not exist"
[ "$(stat -c %a "$KEY")" = 600 ] || fail "the release key $KEYDISP is not mode 600"
pub=$(keyed python3 "$HERE/sign_manifest.py" --public-key "$KEY") || fail "cannot read the release key's public half"
[ "$pub" = "$(git_ show "$SHA:launcher/release_key.pub" | tr -d '[:space:]')" ] || fail "the release key does not match launcher/release_key.pub of $SHA"
command -v "$GH" >/dev/null || fail "gh not found"
# the public commits and the tag carry the GitHub account's noreply address (GitHub refuses pushes that publish a private one, GH007;
# the manual syncs used the same identity)
ident=$("$GH" api user --jq '"\(.login) \(.id)"') || fail "gh api user failed (gh auth status?)"
export GIT_AUTHOR_NAME=${ident% *} GIT_COMMITTER_NAME=${ident% *}
export GIT_AUTHOR_EMAIL="${ident#* }+${ident% *}@users.noreply.github.com" GIT_COMMITTER_EMAIL="${ident#* }+${ident% *}@users.noreply.github.com"
echo "AUTORELEASE identity $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>"
if [ -z "$STANDIN" ]; then [ -n "$BUILDER" ] && [ -x "$BUILDER" ] || fail "OPENBFME_RELEASE_BUILDER is not set or not executable (step 4's builder)"; fi

# ---- release notes (step 7's text; the PR and the sync commit use their items) ------------------------------------------------------------
STEP=notes
git_ show "$SHA:tools/release/known_issues.json" > "$WORK/known_issues.json"
python3 "$HERE/autorelease.py" --git-dir "$REPO" notes --from "$BASE" --to "$SHA" --version "$VERSION" --known-issues "$WORK/known_issues.json" --repo "$TARGET" \
  --out "$WORK/notes-draft.md" --title-out "$WORK/title.txt" --items-out "$WORK/items.md" --plain-items-out "$WORK/items.txt" $MACOPT \
  --sources-out "$WORK/note-sources.txt"

# ---- the public metadata, written and audited before anything is pushed (r6, Sol r5: a Release-note with a private path and the key path
# reached the public commit message, the PR body and the release notes). A privacy hit stops the run; nothing is redacted into it.
STEP=metadata
# GHPR-1: the reviewed community pull requests this release lands: the sync PR credits them
community credits --sha "$SHA" --md "$WORK/community.md" --json "$WORK/community.json" || fail "the reviewed community pull requests cannot be read (above)"
TITLE="Sync the rebuild: $DATE ($(cat "$WORK/title.txt"))"
printf '%s\n' "$TITLE" > "$WORK/pr-title.txt"
{ echo "$TITLE (#0)"; echo; echo "Automatic release $VERSION."; } > "$WORK/merge-msg.txt"
{ echo "$TITLE"; echo
  echo "The rebuild's working branch at ${SHA:0:10} (changes since ${BASE:0:10}), without the archived legacy code."
  if [ -s "$WORK/items.txt" ]; then echo; cat "$WORK/items.txt"; fi; echo
  echo "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"; } > "$WORK/commit-msg.txt"
{ echo "Brings \`main\` up to the rebuild's working branch at ${SHA:0:10} (changes since ${BASE:0:10}), without the archived legacy code."
  echo; echo "For players:"; echo
  if [ -s "$WORK/items.md" ]; then cat "$WORK/items.md"; else echo "- No changes you will notice in the game (tools and documentation)."; fi; echo
  if [ -s "$WORK/community.md" ]; then cat "$WORK/community.md"; echo; fi
  GATE_NAME=$(awk -F'\t' -v s="$SHA" '$1 == s { n = $2 } END { print n }' "$STATE/gated.tsv" 2>/dev/null)
  case "$GATE_NAME" in
    fastpath*) echo "**Verification:** ${SHA:0:10} was compiled for Linux and Windows and passed the privacy check before this sync (the fast release path: no full test gate). The release $VERSION is built from the merge commit." ;;
    *) echo "**Verification:** ${SHA:0:10} passed the full gate (build, the C++ suite, smoke / start / end / LAN / audio runs, the Windows cross build, pytest and the simulation audit) before this sync. The release $VERSION is built from the merge commit." ;;
  esac
  echo; echo "🤖 Generated with [Claude Code](https://claude.com/claude-code)"; } > "$WORK/pr-body.md"
echo "OpenBFME $VERSION" > "$WORK/tag-msg.txt"
python3 "$HERE/autorelease.py" title "$VERSION" > "$WORK/release-title.txt"   # publish_release.sh's release name ("OpenBFME v0.4.0 Alpha 1")
python3 "$HERE/autorelease.py" audit-text "$WORK/pr-title.txt" "$WORK/merge-msg.txt" "$WORK/commit-msg.txt" "$WORK/pr-body.md" \
  "$WORK/notes-draft.md" "$WORK/tag-msg.txt" "$WORK/release-title.txt" "$WORK/note-sources.txt" --denylist "$DENYLIST" \
  || fail "the public metadata failed the privacy audit: nothing pushed"
# the note warnings print note text: only after the audit passed it (PUBLISH-2 r2, Sol r1 P1), and masked
python3 "$HERE/autorelease.py" --git-dir "$REPO" warnings --from "$BASE" --to "$SHA" --denylist "$DENYLIST"

# ---- 2. sync to the public main ------------------------------------------------------------------------------------------------------
STEP=sync
git_ fetch -q "$REMOTE" main
MAIN=$(git_ rev-parse "refs/remotes/$REMOTE/main")
TREE=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" sync-tree --sha "$SHA") || fail "the sync recipe could not build the public tree of $SHA"
# the privacy audit, before anything leaves the Deck (Sol r1: a tracked workspace/ file and a retail file went to the public branch)
STEP=sync-audit
python3 "$HERE/autorelease.py" --git-dir "$REPO" audit-tree --tree "$TREE" --denylist "$DENYLIST" || fail "the public tree $TREE failed the privacy audit: nothing pushed"
STEP=sync
# GHPR-1: each reviewed community pull request is merged on GitHub first (its changes proven to be in the audited tree; all checked before
# the first merge), so its author sees it merged; the sync commit then goes on top of the main that holds those merges. A resumed run
# whose sync is already merged merges nothing more (the next release takes the rest).
if [ $RESUME = 1 ] && [ -n "$(intent_get public)" ]; then echo "AUTORELEASE community: resumed after the sync, nothing to merge"
else
  STEP=community
  python3 "$HERE/public_workflows.py" --git-dir "$REPO" --tree "$TREE" || fail "the public tree's .github folder breaks the public workflow policy: nothing pushed"
  community land --json "$WORK/community.json" --sha "$SHA" --tree "$TREE" --mode "$MODE" || fail "a reviewed community pull request cannot land: nothing synced"
  STEP=sync
  git_ fetch -q "$REMOTE" main
  MAIN=$(git_ rev-parse "refs/remotes/$REMOTE/main")
fi
PR=""
if [ $RESUME = 1 ]; then
  [ "$(intent_get tree)" = "$TREE" ] || fail "the audited tree of $SHA is not the one the release intent recorded: check $INTENT by hand"
fi
if [ $RESUME = 1 ] && [ -n "$(intent_get public)" ]; then
  PUBLIC=$(intent_get public)
  git_ cat-file -e "$PUBLIC^{commit}" 2>/dev/null || git_ fetch -q "$REMOTE" "$PUBLIC" || fail "the intent's public commit $PUBLIC cannot be fetched"
  [ "$(git_ rev-parse "$PUBLIC^{tree}")" = "$TREE" ] || fail "the intent's public commit $PUBLIC does not have the audited tree"
  echo "AUTORELEASE sync: resumed, the public commit is $PUBLIC"
elif [ "$TREE" = "$(git_ rev-parse "$MAIN^{tree}")" ]; then
  echo "AUTORELEASE sync: $REMOTE/main already has the tree of $SHA: no PR, the release tags $MAIN"
  PUBLIC=$MAIN
  if [ "$MODE" = real ]; then intent_set "sha=$SHA" "version=$VERSION" "tree=$TREE" "base=$BASE" "work=$WORK" "public=$PUBLIC" step=tag; fi
else
  if [ $RESUME = 1 ] && [ -n "$(intent_get branch)" ]; then
    # the earlier run's sync branch: if it is on the remote unmerged (an open PR?), only the operator can tell what to do with it
    if git_ ls-remote --exit-code "$REMOTE" "refs/heads/$(intent_get branch)" >/dev/null; then
      fail "an earlier run's sync branch $(intent_get branch) is on $REMOTE but not merged: close its PR and delete the branch by hand, then run again (docs/RELEASE.md, operator steps)"
    fi
  fi
  C=$(GIT_AUTHOR_DATE="@$(date +%s) +0000" git_ commit-tree "$TREE" -p "$MAIN" -F "$WORK/commit-msg.txt")
  BRANCH="sync/$DATE-${SHA:0:10}$SUFFIX"
  if [ "$MODE" = real ]; then intent_set "sha=$SHA" "version=$VERSION" "tree=$TREE" "base=$BASE" "work=$WORK" "branch=$BRANCH" step=sync; fi
  if [ "$MODE" = dry ]; then
    # r3: the dry run writes nothing to GitHub (the coordinator: nothing against the real GitHub); the push is printed like the rest
    would git push "$REMOTE" "$C:refs/heads/$BRANCH"
    would "$GH" pr create --repo "$TARGET" --base main --head "$BRANCH" --title "$TITLE" --body-file "$WORK/pr-body.md"
    would "$GH" pr merge "<pr>" --repo "$TARGET" --merge --subject "$TITLE (#<pr>)"
    would git push "$REMOTE" --delete "$BRANCH"
    PUBLIC=$C   # the dry run builds the unmerged sync commit: the same tree the merge commit gets
  else
    git_ push -q "$REMOTE" "$C:refs/heads/$BRANCH"
    BRANCH_PUSHED=$BRANCH
    url=$("$GH" pr create --repo "$TARGET" --base main --head "$BRANCH" --title "$TITLE" --body-file "$WORK/pr-body.md") || fail "gh pr create failed"
    PR=${url##*/}; echo "AUTORELEASE sync: PR $url"
    [[ "$PR" =~ ^[0-9]+$ ]] || fail "gh pr create did not answer a PR URL ($url)"
    if ! "$GH" pr merge "$PR" --repo "$TARGET" --merge --subject "$TITLE (#$PR)" --body "Automatic release $VERSION."; then
      echo "AUTORELEASE sync: gh pr merge failed or its answer was lost: reading the PR back"
    fi
    PUBLIC=$("$GH" pr view "$PR" --repo "$TARGET" --json mergeCommit -q .mergeCommit.oid) || fail "cannot read PR $PR back"
    [[ "$PUBLIC" =~ ^[0-9a-f]{40}$ ]] || fail "PR $PR is not merged"
    git_ push -q "$REMOTE" --delete "$BRANCH" && BRANCH_PUSHED=""
    git_ fetch -q "$REMOTE" main
    [ "$(git_ rev-parse "$PUBLIC^{tree}")" = "$TREE" ] || fail "the merge commit $PUBLIC does not have the audited tree ($REMOTE/main moved during the sync?)"
    intent_set "public=$PUBLIC" step=tag
  fi
fi
echo "AUTORELEASE public commit $PUBLIC"
if [ "$MODE" = real ]; then community record --json "$WORK/community.json" --public "$PUBLIC" || fail "the landed community pull requests could not be recorded"
else would community_prs.py record --public "$PUBLIC"; fi

# ---- 3. the tag ------------------------------------------------------------------------------------------------------------------------
STEP=tag
# the remote's tags, read once; a failed read stops the run (Sol r2: a swallowed listing failure let two release tags onto one commit)
RTAGS=$(git_ ls-remote --tags "$REMOTE") || fail "cannot list the tags of $REMOTE (fail closed)"
peeled() { printf '%s\n' "$RTAGS" | awk -v t="refs/tags/$1" '$2 == t "^{}" { p = $1 } $2 == t && !p { l = $1 } END { print (p ? p : l) }'; }
TAGOBJ=""
if [ $RESUME = 1 ] && printf '%s\n' "$RTAGS" | awk -v t="refs/tags/$VERSION" '$2 == t { f = 1 } END { exit !f }'; then
  # the earlier run pushed the tag: it must be on the public commit; the release continues with it
  [ "$(peeled "$VERSION")" = "$PUBLIC" ] || fail "the tag $VERSION on $REMOTE is not on the intent's public commit $PUBLIC: check it by hand"
  git_ fetch -q "$REMOTE" "+refs/tags/$VERSION:refs/tags/$VERSION" || fail "cannot fetch the tag $VERSION"
  echo "AUTORELEASE tag: resumed, $VERSION is already on $REMOTE"
  TAG_PUSHED=1
else
  # one release tag per commit, counted the way the build's version is (git describe --match "v[0-9]*" --exclude "*-playtest.*"): a second
  # one makes the version pick either
  others=""
  for t in $( { git_ tag --points-at "$PUBLIC"
                printf '%s\n' "$RTAGS" | awk -v c="$PUBLIC" '$1 == c { sub("^refs/tags/", "", $2); sub("\\^\\{\\}$", "", $2); print $2 }'; } | sort -u); do
    if describe_tag "$t" && [ "$t" != "$VERSION" ]; then others="$others$t "; fi
  done
  [ -z "$others" ] || fail "the public commit $PUBLIC already carries the release tag(s) $others(not from an open release intent). A second tag would make the build's version ambiguous. Tags cannot be deleted (the repository's ruleset, GH013): such a tag is left by a run whose release intent was lost; write the intent back by hand ($INTENT: sha, version, tree, public, work, step=tagged; docs/RELEASE.md, operator steps) and run again to resume that version"
  if git_ rev-parse -q --verify "refs/tags/$VERSION" >/dev/null; then
    # an earlier run of this intent made the tag but did not push it
    [ $RESUME = 1 ] && [ "$(git_ rev-parse "$VERSION^{commit}")" = "$PUBLIC" ] \
      || fail "a local tag $VERSION already exists (left by an earlier run? check and delete it by hand)"
  else
    git_ tag -a "$VERSION" -F "$WORK/tag-msg.txt" "$PUBLIC"
    TAG_LOCAL=$VERSION
  fi
  TAGOBJ=$(git_ rev-parse "refs/tags/$VERSION")
  if [ "$MODE" = dry ]; then
    would git push "$REMOTE" "refs/tags/$VERSION"
  else
    intent_set "sha=$SHA" "version=$VERSION" "tree=$TREE" "base=$BASE" "work=$WORK" "public=$PUBLIC" step=tag
    if ! git_ push -q "$REMOTE" "refs/tags/$VERSION"; then
      # an uncertain answer: the remote decides
      [ "$(git_ ls-remote "$REMOTE" "refs/tags/$VERSION" | cut -f1)" = "$TAGOBJ" ] || fail "pushing the tag $VERSION failed"
      echo "AUTORELEASE: the tag push reported a failure, but $REMOTE has the tag: continuing"
    fi
    TAG_PUSHED=1; TAG_LOCAL=""
  fi
fi
if [ "$MODE" = real ]; then intent_set step=tagged; fi

# ---- 4. build, package and test on the build host -------------------------------------------------------------------------------------
STEP=build
WT="$WORK_ROOT/autorel-src"     # the builder's lane "autorel-src": its build dir and ccache carry over between releases
git_ worktree remove --force "$WT" 2>/dev/null || true
rm -rf "$WT"
git_ worktree add -q --detach "$WT" "$VERSION"
[ "$(git -C "$WT" describe --tags --match 'v[0-9]*' --exclude '*-playtest.*' --always --dirty --abbrev=10)" = "$VERSION" ] || fail "the clean checkout does not describe as $VERSION"
RETURNED="$WORK/returned"; mkdir -p "$RETURNED"
DLL=godot/bin/openbfme.windows.template_debug.x86_64.dll
SO=godot/bin/openbfme.linux.template_debug.x86_64.so
if [ -n "$STANDIN" ]; then
  "$STANDIN/build" "$WT" "$VERSION" "$RETURNED" || fail "the stand-in build failed"
else
# PUBLISH-2: the build host, its paths and its lane scripts are the operator's (kept outside this repository): the builder has the
# stand-in's interface and builds the Linux and Windows libraries, packages, runs test_export.sh and the native Windows start
# (windows_start_check.sh, windows_hud6_check.sh from AR_TOOLS) and copies the packages to $RETURNED
AR_TOOLS=$HERE AR_TARGET=$TARGET AR_PKGOPT=$PKGOPT AR_TEMPLATES=$TEMPLATES AR_MODE=$MODE "$BUILDER" "$WT" "$VERSION" "$RETURNED" \
  || fail "the release build (OPENBFME_RELEASE_BUILDER) failed (log above)"
fi

# ---- 4m. the macOS package on the Mac build host (MACREL-1) ------------------------------------------------------------------------------
STEP=mac-build
MACOUT="$WORK/returned-macos"; mkdir -p "$MACOUT"
MACZIP="openbfme-$VERSION-macos-arm64.zip"
if [ "$SKIP_MAC" = 1 ]; then
  echo "AUTORELEASE mac-build: skipped (OPENBFME_RELEASE_SKIP_MAC=1): $VERSION has no macOS package"
elif [ -n "$STANDIN" ]; then
  "$STANDIN/build-mac" "$WT" "$VERSION" "$MACOUT" || fail "the stand-in Mac build failed"
else
  bash "$HERE/build_macos_release.sh" "$WT" "$VERSION" "$MACOUT" || fail "the macOS package build on $MAC_HOST failed (above)"
fi

# ---- 5. the Deck's checks of what came back, with a trusted checkout ------------------------------------------------------------------
STEP=deck-verify
# the trusted checkout: a fresh worktree of the tag, made here from the Deck's objects, whose tree is the audited sync tree of $SHA (so its
# tools are the archive sha's); the build host never writes to it. Every script that touches the packages, the key or GitHub from here on is its.
TRUSTED="$WORK/trusted"
git_ worktree add -q --detach "$TRUSTED" "$VERSION"
[ "$(git_ rev-parse "$VERSION^{commit}")" = "$PUBLIC" ] || fail "the tag $VERSION is not on $PUBLIC"
[ "$(git -C "$TRUSTED" rev-parse 'HEAD^{tree}')" = "$TREE" ] || fail "the trusted checkout's tree is not the audited sync tree of $SHA"
[ -z "$(git -C "$TRUSTED" status --porcelain --untracked-files=all)" ] || fail "the trusted checkout is not clean"
[ "$(python3 "$HERE/autorelease.py" --git-dir "$REPO" sync-tree --sha "$SHA")" = "$TREE" ] || fail "the tag's tree is not the public tree of $SHA"
T="$TRUSTED/tools/release"
# the control scripts the Deck runs from here on are byte for byte the running autorelease's own (Sol r2: an old publisher ran)
for f in $(git -C "$TRUSTED" ls-files tools/release tools/precommit.py); do
  cmp -s "$TRUSTED/$f" "$REPO/$f" || fail "the tag's $f differs from the running autorelease's copy in $REPO: run autorelease.sh from a checkout whose tools are the released sha's"
done
PKG="$WORK/pkg"; mkdir -p "$PKG"
# lane ONEZIP-1: per platform the package (the game and the launcher in one archive) and, while the launchers published before ONEZIP-1
# are served, the launcher-only archive they update themselves from
WANT=("openbfme-$VERSION-linux-x64.tar.gz" "openbfme-$VERSION-windows-x64.zip")
[ "$LEGACY" = 0 ] || WANT+=("openbfme-launcher-$VERSION-linux-x64.tar.gz" "openbfme-launcher-$VERSION-windows-x64.zip")
NARCH=${#WANT[@]}
WANT+=("SHA256SUMS-$VERSION.txt" "BUILDINFO-$VERSION.txt")
got=$(cd "$RETURNED" && find . -mindepth 1 -printf '%P %y\n' | sort)
want=$(printf '%s f\n' "${WANT[@]}" | sort)
[ "$got" = "$want" ] || fail "the returned folder is not exactly the package files: $(echo $got)"
for f in "${WANT[@]}"; do cp "$RETURNED/$f" "$PKG/$f"; done
if [ -n "$STANDIN" ]; then
  "$STANDIN/verify" "$TRUSTED" "$PKG" "$VERSION" || fail "the stand-in check of the returned packages failed"
else
  (cd "$PKG" && python3 "$T/audit_package.py" --sums "SHA256SUMS-$VERSION.txt" "${WANT[@]:0:$NARCH}") || fail "the checksum file does not list the $NARCH archives"
  for a in "${WANT[@]:0:$NARCH}"; do
    case "$a" in
      openbfme-launcher-*-linux-x64.tar.gz) ap=launcher-linux ;; openbfme-launcher-*) ap=launcher-windows ;;
      *-linux-x64.tar.gz) ap=linux ;; *) ap=windows ;;
    esac
    (cd "$PKG" && python3 "$T/audit_package.py" --release "$ap" "$a") || fail "$a failed the release allowlist on the Deck"
  done
  # the two libraries, from the returned archives, checked like package.sh's check_lib: one build record naming the tag, its commit, a
  # clean tree and the engine id of the tag's committed engine/
  L="$WORK/libs"; mkdir -p "$L/src"
  tar -xzf "$PKG/openbfme-$VERSION-linux-x64.tar.gz" -C "$L" "openbfme-$VERSION-linux-x64/$(basename "$SO")"
  (cd "$L" && unzip -q "$PKG/openbfme-$VERSION-windows-x64.zip" "openbfme-$VERSION-windows-x64/$(basename "$DLL")")
  git -C "$TRUSTED" archive --format=tar HEAD engine | tar -x -C "$L/src"
  for lib in "$L/openbfme-$VERSION-linux-x64/$(basename "$SO")" "$L/openbfme-$VERSION-windows-x64/$(basename "$DLL")"; do
    rec=$(python3 "$T/lib_provenance.py" --record "$lib") || fail "$(basename "$lib") has no single build record"
    x86=$(echo "$rec" | sed -n 's/^x86-32=//p'); opts=$(echo "$rec" | sed -n 's/^id-options=//p')
    id=$(python3 "$T/lib_provenance.py" --engine-id-of "$L/src/engine" --x86-32 "${x86:-OFF}" --id-options "$opts")
    python3 "$T/lib_provenance.py" "$lib" --version "$VERSION" --commit "$PUBLIC" --engine-id "$id" || fail "$(basename "$lib") is not the build of $VERSION"
  done
  # a byte for byte rebuild of every archive from the trusted checkout with these libraries (package.sh --verify, Godot 4.7.2 here)
  mkdir -p "$TRUSTED/godot/bin"
  cp "$L/openbfme-$VERSION-linux-x64/$(basename "$SO")" "$TRUSTED/$SO"
  echo "AUTORELEASE: package.sh --verify on the Deck (a rebuild from the trusted checkout)"
  "$T/package.sh" --verify "$PKG" --windows-dll "$L/openbfme-$VERSION-windows-x64/$(basename "$DLL")" --repo "$TARGET" \
    || fail "the returned packages are not what $VERSION builds on the Deck"
fi
# MACREL-1: the Mac package, in every mode (the stand-in build returns one too): exactly the package folder package_macos.py writes, the
# strict audit, its checksum, VERSION naming this tag at the public commit, its launcher trusting launcher/release_key.pub and this repository
if [ "$SKIP_MAC" = 0 ]; then
  got=$(cd "$MACOUT" && find . -mindepth 1 -printf '%P %y\n' | sort)
  want=$(printf '%s f\n' "$MACZIP" "$MACZIP.sha256" "${MACZIP%.zip}.inventory.txt" README-MAC.txt | sort)
  [ "$got" = "$want" ] || fail "the returned Mac folder is not exactly the Mac package files: $(echo $got)"
  python3 "$T/make_macos_manifest.py" --verify-package "$MACOUT/$MACZIP" --checksum "$MACOUT/$MACZIP.sha256" --version "$VERSION" \
    --commit "$PUBLIC" --repo "$TARGET" --release-key-file "$TRUSTED/launcher/release_key.pub" || fail "$MACZIP failed the Deck's checks"
fi
echo "AUTORELEASE: the returned packages passed the Deck's checks"

# ---- 6. manifest and signature (the trusted checkout's scripts) ----------------------------------------------------------------------------
# lane ONEZIP-1 r2: first the packages' signed payload inventory (seal_package.py: payload.json + its signature into each combined archive,
# which the game and the launcher check at the first start, before any update check), then the manifest of the sealed archives
STEP=seal
# MACPLAY-1: the Mac package joins the folder BEFORE the sealing, so seal_package.py seals it like the Windows / Linux ones (payload.json +
# its signature inside the ZIP, outside both apps: their code seals are unchanged) and rewrites its checksum line with the others; the
# Mac launcher starts the game beside it only when that inventory verifies (launcher/scripts/core/updater.gd bundled)
if [ "$SKIP_MAC" = 0 ]; then
  cp "$MACOUT/$MACZIP" "$PKG/$MACZIP"
  cat "$MACOUT/$MACZIP.sha256" >> "$PKG/SHA256SUMS-$VERSION.txt"
fi
if [ "$MODE" = dry ]; then
  mkdir -p "$WORK/dry-key"; chmod 700 "$WORK/dry-key"
  python3 "$T/sign_manifest.py" --generate-key "$WORK/dry-key/throwaway.pem" > "$WORK/dry-key/throwaway.pub"
  would python3 "$T/seal_package.py" seal "$PKG" --key "$KEYDISP" --repo "$TARGET"
  python3 "$T/seal_package.py" seal "$PKG" --key "$WORK/dry-key/throwaway.pem" --repo "$TARGET" || fail "sealing the packages failed (above)"
else
  keyed python3 "$T/seal_package.py" seal "$PKG" --key "$KEY" --repo "$TARGET" || fail "sealing the packages failed (above)"
fi
STEP=manifest
python3 "$T/make_manifest.py" "$PKG" --repo "$TARGET" --format "$FORMAT"
# the Mac launchers read only manifest-macos.json (launcher/scripts/core/updater.gd macos_release_manifest), signed on its own: manifest.json
# is the Windows / Linux manifest the released launchers read, unchanged by the Mac package. The same version, repository, commit and date.
MDATE=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["date"])' "$PKG/manifest.json")
if [ "$SKIP_MAC" = 1 ]; then
  python3 "$T/make_macos_manifest.py" --version "$VERSION" --repo "$TARGET" --commit "$PUBLIC" --date "$MDATE" --no-entry --out "$PKG/manifest-macos.json"
else
  # the sealed ZIP's own checksum (SHA256SUMS' line, rewritten by the sealing), not the unsealed one the build host returned
  grep -F "  $MACZIP" "$PKG/SHA256SUMS-$VERSION.txt" > "$WORK/$MACZIP.sealed.sha256" || fail "SHA256SUMS-$VERSION.txt has no line for $MACZIP"
  python3 "$T/make_macos_manifest.py" --version "$VERSION" --repo "$TARGET" --commit "$PUBLIC" --date "$MDATE" \
    --archive "$PKG/$MACZIP" --checksum "$WORK/$MACZIP.sealed.sha256" --out "$PKG/manifest-macos.json"
fi
STEP=sign
if [ "$MODE" = dry ]; then
  for m in manifest.json manifest-macos.json; do
    would python3 "$T/sign_manifest.py" --key "$KEYDISP" "$PKG/$m"
    python3 "$T/sign_manifest.py" --key "$WORK/dry-key/throwaway.pem" "$PKG/$m"
  done
  PUBOPT=(--key-file "$WORK/dry-key/throwaway.pub")
else
  for m in manifest.json manifest-macos.json; do
    keyed python3 "$T/sign_manifest.py" --key "$KEY" "$PKG/$m" || fail "signing $m failed (above)"
  done
  PUBOPT=()
fi

# ---- 7. the notes ---------------------------------------------------------------------------------------------------------------------
STEP=notes
python3 "$HERE/autorelease.py" --git-dir "$REPO" notes --from "$BASE" --to "$SHA" --version "$VERSION" --known-issues "$WORK/known_issues.json" --repo "$TARGET" \
  --commit "$PUBLIC" --date "$DATE" $([ "$LEGACY" = 1 ] || echo --no-legacy-launchers) $MACOPT --out "$WORK/notes.md"
python3 "$HERE/autorelease.py" audit-text "$WORK/notes.md" --denylist "$DENYLIST" || fail "the release notes failed the privacy audit: nothing published"
cat "$WORK/notes.md"   # printed only once audited (PUBLISH-2 r2)

# ---- 8. publish (the trusted checkout's publish_release.sh: draft, assets checked, published, read back) -------------------------------
STEP=publish
# RELNAME-1 (Sol r2): an alpha build, also one resumed from an open intent (its version is kept; VERSION-1: only an intent opened before
# the plain releases can still be one), is published only while the launchers up to v0.3.0-preview.4 still have their verified bridge on the page they read, with room for this release; otherwise the run stops
# here, nothing published, the intent open (docs/RELEASE.md, "The alpha builds": publish the bridge again by hand, then run again)
case $VERSION in
  *-alpha.*) python3 "$HERE/autorelease.py" --git-dir "$REPO" bridge-ok --remote "$REMOTE" --repo "$TARGET" --sha "$SHA" --gh "$GH" \
       || fail "the bridge for the launchers up to v0.3.0-preview.4 is gone: $VERSION is not published (the intent stays open)" ;;
esac
if [ "$MODE" = dry ]; then
  "$T/publish_release.sh" "$PKG" --repo "$TARGET" --notes-file "$WORK/notes.md" --denylist "$DENYLIST" "${PUBOPT[@]}"
  would "$T/publish_release.sh" "$PKG" --repo "$TARGET" --notes-file "$WORK/notes.md" --denylist "$DENYLIST" --execute --confirm-tag "$VERSION"
else
  intent_set step=publishing
  RESUMEOPT=(); if [ -n "$RESUME_RID" ]; then RESUMEOPT=(--resume-id "$RESUME_RID"); fi
  "$T/publish_release.sh" "$PKG" --repo "$TARGET" --notes-file "$WORK/notes.md" --denylist "$DENYLIST" --execute --confirm-tag "$VERSION" --id-file "$STATE/intent.rid" "${RESUMEOPT[@]}" \
    || fail "publish_release.sh failed (above); the release intent stays open: the next run reconciles it"
  # read back once more: the published release of the tag (GitHub's tags endpoint shows published releases only)
  [ "$("$GH" api "repos/$TARGET/releases/tags/$VERSION" --jq .draft)" = false ] || fail "the release $VERSION does not read back as published"
  PUBLISHED=1
  record_released "$SHA" "$VERSION" "$PUBLIC"
fi
URL="https://github.com/$TARGET/releases/tag/$VERSION"

# ---- 8b. the About link (RELTEST-1) ---------------------------------------------------------------------------------------------------
STEP=homepage
HOMEPAGE_NOTE=""
if [ "$MODE" = dry ]; then
  would "$GH" api --method PATCH "repos/$TARGET" -f "homepage=$URL"
elif ! "$GH" api --method PATCH "repos/$TARGET" -f "homepage=$URL" --jq .homepage > /dev/null; then
  HOMEPAGE_NOTE="the repository's About link could not be set to $URL (gh api PATCH failed): set it by hand"
elif [ "$("$GH" api "repos/$TARGET" --jq .homepage)" != "$URL" ]; then
  HOMEPAGE_NOTE="the repository's About link does not read back as $URL: set it by hand"
else
  echo "AUTORELEASE homepage: the About link is $URL"
fi

# ---- 9. Discord ----------------------------------------------------------------------------------------------------------------------
STEP=announce
cmds=""
if [ -x "$COMMUNITY" ]; then cmds=$("$COMMUNITY" --help 2>/dev/null | grep -oE '\b[a-z-]*(release|announce)[a-z-]*\b' | sort -u | tr '\n' ' ' || true); fi
if [ -n "$cmds" ]; then
  echo "AUTORELEASE announce: bfme-community offers '$cmds' but autorelease.sh does not know its arguments yet: announce $URL by hand and wire it in here"
else
  echo "AUTORELEASE announce: bfme-community has no release announcement command: nothing posted to Discord"
fi

# ---- 10. summary ---------------------------------------------------------------------------------------------------------------------
STEP=done
if [ "$MODE" = dry ]; then
  summary "AUTORELEASE DRY RUN OK $VERSION from ${SHA:0:10} (public ${PUBLIC:0:10}; nothing published; packages in $PKG; log $LOG)"
else
  summary "AUTORELEASE OK $VERSION $URL (archive ${SHA:0:10}, public ${PUBLIC:0:10}; log $LOG)"
  if [ -n "$HOMEPAGE_NOTE" ]; then summary "AUTORELEASE WARNING: $HOMEPAGE_NOTE"; fi
fi
if [ "$SKIP_MAC" = 1 ]; then summary "AUTORELEASE WARNING: $VERSION has no macOS package (OPENBFME_RELEASE_SKIP_MAC=1)"; fi
if [ -n "$REQUESTED" ]; then
  summary "AUTORELEASE RESUMED ONLY: the open release $VERSION of ${SHA:0:10} was finished; $REQUESTED was not released: run again for it"
  exit 3
fi
