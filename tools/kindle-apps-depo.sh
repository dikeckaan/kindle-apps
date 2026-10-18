#!/bin/sh
# Name: Kindle Apps Deposu
# Author: Kaan Dikeç
# DontUseFBInk

# Adds the Kindle Apps kpm repository and installs every app. Safe to run again (it updates).
FBINK=/var/local/kmc/bin/fbink
KPM=/var/local/kmc/bin/kpm
say() { [ -x "$FBINK" ] && $FBINK -q -m -y -3 "$1" >/dev/null 2>&1; echo "$1"; }

say "Kindle Apps deposu ekleniyor..."
/var/local/kmc/bin/su -c "$KPM list-repo" | grep -q dikeckaan.github.io/kindle-apps || \
    /var/local/kmc/bin/su -c "$KPM add-repo https://dikeckaan.github.io/kindle-apps/manifest.json"
/var/local/kmc/bin/su -c "$KPM update" >/dev/null 2>&1
say "Uygulamalar kuruluyor..."
if /var/local/kmc/bin/su -c "$KPM -y install kindle-apps-core kindle-apps-2048"; then
    say "Kuruldu: kitaplıkta 'Uygulamalar'ı açın"
else
    say "Kurulum başarısız: Wi-Fi bağlantısını kontrol edin"
fi
sleep 3
