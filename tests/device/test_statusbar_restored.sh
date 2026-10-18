# usage: sh sb_test.sh <launcher>; statusbar already stopped before launch must be running after the app exits
exec > /tmp/sb.result 2>&1
printf '#!/bin/sh\nsleep 2\n' > /tmp/app2.sh; chmod +x /tmp/app2.sh
stop statusbar >/dev/null 2>&1
echo "before launch: $(status statusbar)"
sh "$1" test /tmp/app2.sh
sleep 1
echo "after exit: $(status statusbar)"
status statusbar | grep -q running && echo PASS || { echo FAIL; start statusbar >/dev/null 2>&1; }
