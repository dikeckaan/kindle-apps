#!/bin/sh
# Fetch third-party build dependencies into third_party/ (git-ignored).
# Pinned revisions live in tools/deps.lock; pass --update to re-pin FBInk to master HEAD.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TP="$ROOT/third_party"
LOCK="$ROOT/tools/deps.lock"
mkdir -p "$TP/fonts"

FBINK_REV=""
[ -f "$LOCK" ] && FBINK_REV=$(sed -n 's/^FBINK_REV=//p' "$LOCK")
if [ ! -d "$TP/FBInk/.git" ]; then
    git clone -q --recursive https://github.com/NiLuJe/FBInk "$TP/FBInk"
fi
if [ "${1:-}" = "--update" ] || [ -z "$FBINK_REV" ]; then
    FBINK_REV=$(git -C "$TP/FBInk" rev-parse origin/master)
fi
git -C "$TP/FBInk" checkout -q "$FBINK_REV"
git -C "$TP/FBInk" submodule update -q --init --recursive

DEJAVU_VER=2.37
if [ ! -f "$TP/fonts/DejaVuSans.ttf" ] || [ ! -f "$TP/fonts/DejaVuSerif.ttf" ]; then
    curl -fsSL "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_${DEJAVU_VER%.*}_${DEJAVU_VER#*.}/dejavu-fonts-ttf-${DEJAVU_VER}.tar.bz2" \
        | tar -xjf - -C "$TP" "dejavu-fonts-ttf-${DEJAVU_VER}/ttf/DejaVuSans.ttf" "dejavu-fonts-ttf-${DEJAVU_VER}/ttf/DejaVuSans-Bold.ttf" \
           "dejavu-fonts-ttf-${DEJAVU_VER}/ttf/DejaVuSerif.ttf" "dejavu-fonts-ttf-${DEJAVU_VER}/ttf/DejaVuSerif-Bold.ttf" "dejavu-fonts-ttf-${DEJAVU_VER}/LICENSE"
    mv "$TP/dejavu-fonts-ttf-${DEJAVU_VER}/ttf/"*.ttf "$TP/fonts/"
    mv "$TP/dejavu-fonts-ttf-${DEJAVU_VER}/LICENSE" "$TP/fonts/DejaVu-LICENSE"
    rm -rf "$TP/dejavu-fonts-ttf-${DEJAVU_VER}"
fi

printf 'FBINK_REV=%s\nDEJAVU_VER=%s\n' "$FBINK_REV" "$DEJAVU_VER" > "$LOCK"
echo "deps ok: FBInk $FBINK_REV, DejaVu $DEJAVU_VER"
