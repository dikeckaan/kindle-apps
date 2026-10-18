#!/bin/sh
# pack.sh <staging-dir> <dist-dir>
# Tars a staged package directory into <dist>/packages/<id>/<id>_<Y.M.P>_kindlehf.kpkg (id/version from manifest.json).
set -eu
STAGE="$1"
DIST="$2"
read -r ID VER <<EOF2
$(python3 -c 'import json,sys;m=json.load(open(sys.argv[1]));print(m["id"], ".".join(map(str,m["version"])))' "$STAGE/manifest.json")
EOF2
if [ -n "$(find "$STAGE" -type l)" ]; then echo "pack: symlinks are not allowed on /mnt/us" >&2; exit 1; fi
mkdir -p "$DIST/packages/$ID"
OUT="$(cd "$DIST" && pwd)/packages/$ID/${ID}_${VER}_kindlehf.kpkg"
# Entries are stored without a "./" prefix (kpm looks up "manifest.json" literally); plain ustar, no macOS xattrs.
(cd "$STAGE" && COPYFILE_DISABLE=1 tar --format=ustar -czf "$OUT.tmp" $(ls -A)) && mv "$OUT.tmp" "$OUT"
echo "$OUT"
