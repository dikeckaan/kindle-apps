#!/bin/sh
# run.sh <app> "<script>" [outdir] — runs the simulated app; PNG screenshots land in outdir (default build/sim/out/<app>).
set -eu
APP="$1"; SCRIPT="${2:-}"; OUT="${3:-build/sim/out/$APP}"
make -s "build/sim/$APP"
rm -rf "$OUT"; mkdir -p "$OUT"
KUI_ROOT="${KUI_ROOT:-$PWD/build/simroot}" KUI_SIM="$SCRIPT" KUI_SIM_OUT="$OUT" "./build/sim/$APP" 2>&1 | grep -v "^sim: flush" || true
ls "$OUT"
