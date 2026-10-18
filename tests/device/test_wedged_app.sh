# usage: sh wedge_test.sh <kui-run.sh>; an app that ignores TERM must still end and the UI must come back
printf '#!/bin/sh\ntrap "" TERM INT HUP\nwhile :; do sleep 1; done\n' > /tmp/wedged.sh; chmod +x /tmp/wedged.sh
sh "$1" wedged /tmp/wedged.sh & L=$!
sleep 3
s=$(date +%s); kill -TERM $L
i=0; while kill -0 $L 2>/dev/null && [ $i -lt 15 ]; do sleep 1; i=$((i+1)); done
if kill -0 $L 2>/dev/null; then echo "launcher still waiting after 15 s"; pkill -9 -f wedged.sh; wait $L; else echo "launcher ended after $(( $(date +%s) - s )) s"; fi
echo "awesome: $(grep State /proc/$(pidof awesome)/status | cut -f2)"
