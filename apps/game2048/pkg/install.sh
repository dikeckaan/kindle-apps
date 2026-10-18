#!/bin/sh
# Installs the app into the Uygulamalar menu; user data in /mnt/us/kindle-apps/data is untouched.
set -e
DEST=/mnt/us/kindle-apps/apps/game2048
mkdir -p "$DEST"
cp -f ./files/game2048 ./app.ini "$DEST/"
chmod +x "$DEST/game2048" 2>/dev/null || true
rm -rf ./files
