#!/bin/sh
# kui-run.sh <app-id> <binary> [args...]
# Pauses the Kindle UI (as KOReader does), runs a kui app full-screen and ALWAYS restores the UI,
# whether the app exits normally, crashes or is killed.
APP_ID="$1"
shift
[ -n "$APP_ID" ] && [ -n "$1" ] || { echo "usage: kui-run.sh <app-id> <binary> [args...]" >&2; exit 2; }

if [ "$(id -u)" != "0" ]; then
    exec /var/local/kmc/bin/su -c "/bin/sh '$0' '$APP_ID' $*"
fi

export KUI_ROOT="${KUI_ROOT:-/mnt/us/kindle-apps}"
FBINK=/var/local/kmc/bin/fbink
LOG_DIR="$KUI_ROOT/logs"
LOG="$LOG_DIR/$APP_ID.log"
LOCK=/tmp/kui-run.lock
mkdir -p "$LOG_DIR"

# Single instance: the lock dir holds the owner's pid; a stale lock (dead owner) is taken over.
if ! mkdir "$LOCK" 2>/dev/null; then
    owner=$(cat "$LOCK/pid" 2>/dev/null)
    if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then
        echo "kui-run: already running (pid $owner)" >&2
        exit 3
    fi
    rm -rf "$LOCK" && mkdir "$LOCK" || exit 3
fi
echo $$ > "$LOCK/pid"

# Keep the log bounded (~200 KB).
if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 204800 ]; then
    tail -c 102400 "$LOG" > "$LOG.tmp" && mv "$LOG.tmp" "$LOG"
fi

PAUSED_STATUSBAR=0
RESTORED=0
restore() {
    [ "$RESTORED" = 1 ] && return
    RESTORED=1
    killall -CONT volumd 2>/dev/null
    [ "$PAUSED_STATUSBAR" = 1 ] && start statusbar >/dev/null 2>&1
    killall -CONT awesome 2>/dev/null
    lipc-set-prop com.lab126.pillow disableEnablePillow enable 2>/dev/null
    lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home 2>/dev/null
    rm -rf "$LOCK"
}
trap restore EXIT
trap 'exit 130' INT TERM HUP

lipc-set-prop com.lab126.pillow disableEnablePillow disable 2>/dev/null
killall -STOP awesome 2>/dev/null
if [ -f /etc/upstart/statusbar.conf ] && status statusbar 2>/dev/null | grep -q running; then
    stop statusbar >/dev/null 2>&1 && PAUSED_STATUSBAR=1
fi
killall -STOP volumd 2>/dev/null
usleep 300000 2>/dev/null || sleep 1

echo "=== $(date) start $APP_ID: $*" >> "$LOG"
"$@" >> "$LOG" 2>&1 &
CHILD=$!
# Forward termination to the app; if it is wedged, kill it after 5 s so the UI always comes back.
stop_child() {
    kill -TERM "$CHILD" 2>/dev/null
    i=0
    while kill -0 "$CHILD" 2>/dev/null && [ $i -lt 50 ]; do usleep 100000 2>/dev/null || sleep 1; i=$((i + 1)); done
    kill -KILL "$CHILD" 2>/dev/null
    wait "$CHILD" 2>/dev/null
    exit 130
}
trap stop_child INT TERM HUP
wait $CHILD
RC=$?
echo "=== $(date) exit $APP_ID rc=$RC" >> "$LOG"

if [ "$RC" != 0 ] && [ "$RC" != 130 ] && [ -x "$FBINK" ]; then
    $FBINK -q -c -m -y -2 "Uygulama hata verdi (kod $RC)" >/dev/null 2>&1
    $FBINK -q -m -y -1 "Günlük: logs/$APP_ID.log" >/dev/null 2>&1
    sleep 3
fi
exit "$RC"
