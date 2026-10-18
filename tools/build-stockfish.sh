#!/bin/sh
# Builds Stockfish 11 (classical eval, no NNUE network to ship) as a static kindlehf binary.
# Run inside the kindle-apps-tc Docker image: produces build/kindlehf/stockfish.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SF="$ROOT/third_party/Stockfish-sf_11"
if [ ! -d "$SF" ]; then
    mkdir -p "$ROOT/third_party"
    curl -fsSL https://github.com/official-stockfish/Stockfish/archive/refs/tags/sf_11.tar.gz | tar xz -C "$ROOT/third_party"
fi
CROSS_TC=${CROSS_TC:-arm-kindlehf-linux-gnueabihf}
make -C "$SF/src" clean >/dev/null
make -C "$SF/src" build ARCH=armv7 COMP=gcc CXX="$CROSS_TC-g++" \
    EXTRALDFLAGS="-static-libstdc++ -static-libgcc" 2>&1 | grep -E "error|Error" || true
mkdir -p "$ROOT/build/kindlehf"
cp "$SF/src/stockfish" "$ROOT/build/kindlehf/stockfish"
"$CROSS_TC-strip" "$ROOT/build/kindlehf/stockfish"
cp "$SF/Copying.txt" "$ROOT/build/kindlehf/COPYING-stockfish"
ls -la "$ROOT/build/kindlehf/stockfish"
