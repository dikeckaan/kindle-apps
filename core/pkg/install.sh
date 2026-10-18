#!/bin/sh
# Installs the shared runtime under /mnt/us/kindle-apps; never touches data/ or logs/.
set -e
ROOT=/mnt/us/kindle-apps
mkdir -p "$ROOT/bin" "$ROOT/fonts" "$ROOT/lang" "$ROOT/apps" "$ROOT/data" "$ROOT/logs"
cp -f ./files/bin/* "$ROOT/bin/"
cp -f ./files/fonts/* "$ROOT/fonts/"
cp -f ./files/lang/* "$ROOT/lang/" # translations (Menu → Dil)
chmod +x "$ROOT/bin/"* 2>/dev/null || true
if [ -d ./scriptlets ]; then
    cp -f ./scriptlets/*.sh /mnt/us/documents/
fi
rm -rf ./files
