#!/bin/sh
# Installs the shared runtime under /mnt/us/kindle-apps; never touches data/ or logs/.
set -e
ROOT=/mnt/us/kindle-apps
# copy beside, then rename: a running binary (the hub updating itself) keeps its old file
put() { for f in "$1"/*; do cp -f "$f" "$2/.${f##*/}.new" && mv -f "$2/.${f##*/}.new" "$2/${f##*/}"; done; }
mkdir -p "$ROOT/bin" "$ROOT/fonts" "$ROOT/lang" "$ROOT/apps" "$ROOT/data" "$ROOT/logs"
put ./files/bin "$ROOT/bin"
cp -f ./files/fonts/* "$ROOT/fonts/"
cp -f ./files/lang/* "$ROOT/lang/" # translations (Menu → Dil)
chmod +x "$ROOT/bin/"* 2>/dev/null || true
if [ -d ./scriptlets ]; then
    cp -f ./scriptlets/*.sh /mnt/us/documents/
fi
rm -rf ./files
# record the installed version for Uygulamalar → Güncellemeler (from this package's manifest.json)
V=$(tr -d ' \n' < ./manifest.json | sed -n 's/.*"version":\[\([0-9,]*\)\].*/\1/p' | tr , .)
ID=$(tr -d ' \n' < ./manifest.json | sed -n 's/.*"id":"\([a-z0-9-]*\)".*/\1/p')
if [ -n "$V" ] && [ -n "$ID" ]; then mkdir -p /mnt/us/kindle-apps/versions && echo "$V" > "/mnt/us/kindle-apps/versions/$ID"; fi
