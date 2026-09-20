#!/usr/bin/env bash
# Launch digiwage-qt against a throwaway datadir and capture every page/dialog
# in Dark and Light. Usage: screenshots.sh <out-dir> [scale-factor]
set -euo pipefail
OUT=${1:?out dir}; SCALE=${2:-1}
BIN=${BIN:-$(dirname "$0")/../../src/qt/digiwage-qt}
WALLET_TOOL=${WALLET_TOOL:?set WALLET_TOOL to a built digiwage-wallet binary}
DATADIR=${DATADIR:-/tmp/qt-shot-datadir}
PEER=${PEER:-127.0.0.1:34618}
mkdir -p "$OUT"
if [ ! -d "$DATADIR/forktest/shot" ]; then
  mkdir -p "$DATADIR"
  "$WALLET_TOOL" -chain=forktest -datadir="$DATADIR" -wallet=shot create >/dev/null
fi
export QT_SCALE_FACTOR=$SCALE
xvfb-run -a -s "-screen 0 1600x1000x24" "$BIN" -chain=forktest -datadir="$DATADIR" \
  -wallet=shot -connect="$PEER" -listen=0 -dnsseed=0 -upnp=0 -splash=0 -server=0 \
  -uitour="$OUT" -choosedatadir=0 -lang=en_US >"$OUT/run.log" 2>&1 || true
ls "$OUT"/*.png 2>/dev/null | wc -l
