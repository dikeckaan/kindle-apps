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

# Run from RAM copies: an update replaces files on /mnt/us (fuse), which does not keep a replaced file alive for
# a shell still reading this script or for the hub binary still running.
case "$0" in
    /tmp/kui-run.*) ;;
    *) S=/tmp/kui-run.$$.sh; cp -f "$0" "$S" 2>/dev/null && exec /bin/sh "$S" "$APP_ID" "$@" ;;
esac
if [ "$APP_ID" = hub ] && [ -f "$1" ]; then
    cp -f "$1" /tmp/kui-hub.new && chmod +x /tmp/kui-hub.new && mv -f /tmp/kui-hub.new /tmp/kui-hub && { shift; set -- /tmp/kui-hub "$@"; }
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
        case "$0" in /tmp/kui-run.*) rm -f "$0" ;; esac
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
    # an app kept the Kindle awake (kui_prevent_sleep) and did not undo it: put the saved value back
    # (only a file from this boot: "<0|1> <boot_id>")
    R=/var/local/kindle-apps/prevent_ss.restore
    if [ -f "$R" ]; then
        read -r v id < "$R"
        if [ "$id" = "$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)" ]; then
            case "$v" in 0|1) lipc-set-prop com.lab126.powerd preventScreenSaver "$v" 2>/dev/null ;; esac
        fi
        rm -f "$R"
    fi
    # The normal Kindle UI always runs the status bar: bring it back even if something else had
    # stopped it before we started (otherwise the clock/brightness bar stays missing until a reboot).
    if [ -f /etc/upstart/statusbar.conf ] && ! status statusbar 2>/dev/null | grep -q running; then
        start statusbar >/dev/null 2>&1
    fi
    # Görev Yöneticisi / the hub guard stopped a program whose launcher had stopped the framework (KOReader's
    # STOP_FRAMEWORK) and could not bring it back itself: start it now that our app is gone.
    if [ -f "$LOCK/start_gui" ] && [ -f /etc/upstart/lab126_gui.conf ] && status lab126_gui 2>/dev/null | grep -q 'stop/'; then
        echo "=== $(date) restore $APP_ID: starting the framework (lab126_gui)" >> "$LOG"
        start lab126_gui >/dev/null 2>&1
    fi
    echo "=== $(date) restore $APP_ID (paused_statusbar=$PAUSED_STATUSBAR, now: $(status statusbar 2>/dev/null))" >> "$LOG"
    killall -CONT awesome 2>/dev/null
    lipc-set-prop com.lab126.pillow disableEnablePillow enable 2>/dev/null
    lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home 2>/dev/null
    rm -rf "$LOCK"
    case "$0" in /tmp/kui-run.*) rm -f "$0" ;; esac
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
