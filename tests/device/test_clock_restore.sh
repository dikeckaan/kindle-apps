# usage: sh test_clock_restore.sh <kui-run.sh> <clock-binary>; the clock is SIGKILLed (a crash), and the launcher must
# still put preventScreenSaver back to its earlier value (result in /tmp/clock.result)
before=$(lipc-get-prop com.lab126.powerd preventScreenSaver)
sh "$1" clock "$2" & L=$!
sleep 6
during=$(lipc-get-prop com.lab126.powerd preventScreenSaver)
kill -9 $(pidof clock) 2>/dev/null
wait $L
after=$(lipc-get-prop com.lab126.powerd preventScreenSaver)
left=$([ -f /var/local/kindle-apps/prevent_ss.restore ] && echo yes || echo no)
r="before=$before during=$during after=$after restore_file_left=$left"
[ "$during" = 1 ] && [ "$after" = "$before" ] && [ "$left" = no ] && r="PASS $r" || r="FAIL $r"
echo "$r" | tee /tmp/clock.result
