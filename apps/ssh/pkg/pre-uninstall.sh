# stop the server and close its port; secrets in /var/local/kindle-apps/ssh go with the app
P=/var/local/kindle-apps/ssh/dropbear.pid
if [ -f "$P" ]; then
    pid=$(cat "$P")
    port=$(tr '\0' ' ' < /proc/$pid/cmdline 2>/dev/null | sed -n 's/.* -p \([0-9]*\).*/\1/p')
    case "$(readlink /proc/$pid/exe 2>/dev/null)" in */apps/ssh/dropbear) kill "$pid" ;; esac
    [ -n "$port" ] && while iptables -D INPUT -p tcp --dport "$port" -j ACCEPT 2>/dev/null; do :; done
fi
rm -rf /var/local/kindle-apps/ssh
