#!/bin/sh
# Removes the runtime; user data (data/) and installed apps are left in place.
ROOT=/mnt/us/kindle-apps
rm -rf "$ROOT/bin" "$ROOT/fonts"
rm -f /mnt/us/documents/uygulamalar.sh
