#!/bin/sh
# Builds dropbear + dropbearkey for kindlehf with the kindle-apps password patch.
# Run inside the kindle-apps-tc Docker image: produces build/kindlehf/{dropbear,dropbearkey}.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VER=DROPBEAR_2026.94
SRC="$ROOT/third_party/dropbear-$VER"
WORK="$ROOT/build/dropbear-src"
if [ ! -d "$SRC" ]; then
    mkdir -p "$ROOT/third_party"
    curl -fsSL "https://github.com/mkj/dropbear/archive/refs/tags/$VER.tar.gz" | tar xz -C "$ROOT/third_party"
fi
rm -rf "$WORK"
cp -R "$SRC" "$WORK"
cd "$WORK"
patch -p1 < "$ROOT/tools/patches/dropbear-apppass.patch"
cp "$ROOT/apps/ssh/sshcfg.c" "$ROOT/apps/ssh/sshcfg.h" "$ROOT/core/kui/sha256.c" "$ROOT/core/kui/kui.h" src/
cat > localoptions.h <<'OPT'
#define DROPBEAR_X11FWD 0
#define DROPBEAR_SVR_PASSWORD_AUTH 1
#define DROPBEAR_CLI_PASSWORD_AUTH 0
#define DSS_PRIV_FILENAME "/var/local/kindle-apps/ssh/dropbear_dss_host_key"
#define RSA_PRIV_FILENAME "/var/local/kindle-apps/ssh/dropbear_rsa_host_key"
#define ECDSA_PRIV_FILENAME "/var/local/kindle-apps/ssh/dropbear_ecdsa_host_key"
#define ED25519_PRIV_FILENAME "/var/local/kindle-apps/ssh/dropbear_ed25519_host_key"
OPT
CROSS_TC=${CROSS_TC:-arm-kindlehf-linux-gnueabihf}
./configure --host="$CROSS_TC" --disable-zlib --disable-lastlog --disable-utmp --disable-utmpx \
    --disable-wtmp --disable-wtmpx --disable-pututline --disable-pututxline --disable-pam >/dev/null
make -j4 PROGRAMS="dropbear dropbearkey" >/dev/null
mkdir -p "$ROOT/build/kindlehf"
cp dropbear dropbearkey "$ROOT/build/kindlehf/"
"$CROSS_TC-strip" "$ROOT/build/kindlehf/dropbear" "$ROOT/build/kindlehf/dropbearkey"
cp LICENSE "$ROOT/build/kindlehf/LICENSE-dropbear"
ls -la "$ROOT/build/kindlehf/dropbear" "$ROOT/build/kindlehf/dropbearkey"
