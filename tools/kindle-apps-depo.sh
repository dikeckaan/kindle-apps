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
# One package per kpm call, core first: kpm 0.2.2 aborts when it has to resolve dependencies itself.
ok=1
for p in kindle-apps-core kindle-apps-2048; do
    /var/local/kmc/bin/su -c "$KPM -y install $p" || ok=0
done
if [ "$ok" = 1 ]; then
    say "Kuruldu: kitaplıkta 'Uygulamalar'ı açın"
else
    say "Kurulum başarısız: Wi-Fi bağlantısını kontrol edin"
fi
sleep 3
