#!/bin/sh
# Installs the app into the Uygulamalar menu; user data in /mnt/us/kindle-apps/data is untouched.
# No kpm dependency is declared (kpm 0.2.2 aborts while resolving uninstalled dependencies),
# so warn here when the core package is missing.
set -e
DEST=/mnt/us/kindle-apps/apps/game2048
mkdir -p "$DEST"
cp -f ./files/game2048 ./app.ini "$DEST/"
chmod +x "$DEST/game2048" 2>/dev/null || true
rm -rf ./files
[ -x /mnt/us/kindle-apps/bin/hub ] || echo "UYARI: önce kindle-apps-core paketini kurun (kpm install kindle-apps-core)"
