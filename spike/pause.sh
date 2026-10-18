#!/bin/sh
# Throwaway: pause Kindle UI like KOReader does, run "$@", always restore.
restore() {
    [ -n "$RESTORED" ] && return; RESTORED=1
    killall -CONT volumd 2>/dev/null
    [ "$SB" = 1 ] && start statusbar 2>/dev/null
    killall -CONT awesome 2>/dev/null
    lipc-set-prop com.lab126.pillow disableEnablePillow enable
    lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home
    echo restored
}
trap restore EXIT INT TERM HUP
lipc-set-prop com.lab126.pillow disableEnablePillow disable
killall -STOP awesome
SB=0; if [ -f /etc/upstart/statusbar.conf ] && status statusbar 2>/dev/null | grep -q running; then stop statusbar; SB=1; fi
killall -STOP volumd 2>/dev/null
sleep 1
"$@"
