#!/usr/bin/env bash
#
# Builds (once) and runs a local Ninjam server for integration testing.
#
# The server is built OUT OF TREE. Building in place would drop .o files into
# references/ninjam/WDL/ and references/ninjam/ninjam/, dirtying a pinned
# read-only submodule.
#
# Usage:
#   scripts/testserver.sh                 # build if needed, then run
#   scripts/testserver.sh --rebuild       # force a clean rebuild first
#   scripts/testserver.sh --clean-archive # wipe the archive dir before running
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO_ROOT/references/ninjam"
BUILD_DIR="${NINJAM_SERVER_BUILD:-/tmp/njbuild}"
ARCHIVE_DIR="${NINJAM_ARCHIVE:-/tmp/njarchive}"
CFG="$REPO_ROOT/test/fixtures/testserver.cfg"
SRV="$BUILD_DIR/ninjam/server/ninjamsrv"

REBUILD=0
CLEAN_ARCHIVE=0
for arg in "$@"; do
  case "$arg" in
    --rebuild)       REBUILD=1 ;;
    --clean-archive) CLEAN_ARCHIVE=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [ ! -d "$SRC/ninjam/server" ]; then
  echo "error: $SRC/ninjam/server not found." >&2
  echo "The ninjam reference submodule is not checked out. Run:" >&2
  echo "  git submodule update --init references/ninjam" >&2
  exit 1
fi

if [ "$REBUILD" = 1 ]; then
  rm -rf "$BUILD_DIR"
fi

if [ ! -x "$SRV" ]; then
  echo "==> building ninjamsrv out of tree in $BUILD_DIR"
  rm -rf "$BUILD_DIR"
  mkdir -p "$(dirname "$BUILD_DIR")"
  cp -a "$SRC" "$BUILD_DIR"
  # -w and -fpermissive: this is 2005-era C++ and modern g++ is stricter about
  # things the original compiler accepted.
  make -C "$BUILD_DIR/ninjam/server" CFLAGS="-O2 -w -fpermissive -pthread" -j "$(nproc)"
fi

if [ "$CLEAN_ARCHIVE" = 1 ]; then
  echo "==> wiping $ARCHIVE_DIR"
  rm -rf "$ARCHIVE_DIR"
fi
mkdir -p "$ARCHIVE_DIR"

echo "==> archive dir: $ARCHIVE_DIR"
echo "==> config:      $CFG"
echo "==> connect a client to 127.0.0.1:2049 as anonymous"
echo "==> analyse with: scripts/analyze_archive.py $ARCHIVE_DIR"
echo

exec "$SRV" "$CFG"
