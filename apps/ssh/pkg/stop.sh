# stop every root process running our dropbear (listener and logged-in sessions, also after the
# binary was replaced: "(deleted)") and close the port it had opened
BIN=/mnt/us/kindle-apps/apps/ssh/dropbear
P=/var/local/kindle-apps/ssh/dropbear.pid
port=""
[ -f "$P" ] && port=$(tr '\0' ' ' < /proc/$(cat "$P")/cmdline 2>/dev/null | sed -n 's/.* -p \([0-9]*\).*/\1/p')
ours() {
    for d in /proc/[0-9]*; do
        case "$(readlink "$d/exe" 2>/dev/null)" in "$BIN"|"$BIN (deleted)") echo "${d#/proc/}" ;; esac
    done
}
pids=$(ours)
[ -n "$pids" ] && kill $pids 2>/dev/null && sleep 1
pids=$(ours)
[ -n "$pids" ] && kill -9 $pids 2>/dev/null
[ -n "$port" ] && while iptables -D INPUT -p tcp --dport "$port" -j ACCEPT 2>/dev/null; do :; done
rm -f "$P"
