#!/bin/sh
# stage-app.sh <appdir> <stage-dir> <file>... — lays out one app package from apps/<appdir>/pkg and tools/pkg templates.
set -eu
APP="$1"; STAGE="$2"; shift 2
SRC="apps/$APP/pkg"
NAME=$(sed -n 's/^name=//p' "$SRC/app.ini")
rm -rf "$STAGE"; mkdir -p "$STAGE/files"
cp "$SRC/manifest.json" "$SRC/app.ini" tools/pkg/launch.sh "$STAGE/"
[ -f "$SRC/post-install.sh" ] && cp "$SRC/post-install.sh" "$STAGE/"
sed -e "s|@APPDIR@|$APP|g" -e "s|@NAME@|$NAME|g" tools/pkg/install.sh.in > "$STAGE/install.sh"
PRE=":"
[ -f "$SRC/pre-uninstall.sh" ] && PRE=$(cat "$SRC/pre-uninstall.sh")
python3 -c 'import sys;t=open(sys.argv[1]).read();open(sys.argv[2],"w").write(t.replace("@APPDIR@",sys.argv[3]).replace("@PRE_UNINSTALL@",sys.argv[4]))' \
    tools/pkg/uninstall.sh.in "$STAGE/uninstall.sh" "$APP" "$PRE"
cp "$@" "$STAGE/files/"
