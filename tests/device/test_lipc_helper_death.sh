# usage: sh cpu_test.sh <binary>; prints CPU ticks the app used in 3 s after its lipc helper is killed
R=/mnt/us/kindle-apps
sh $R/bin/kui-run.sh game2048 "$1" & L=$!
sleep 4
P=$(pidof $(basename "$1"))
killall lipc-wait-event 2>/dev/null
sleep 1
t0=$(awk '{print $14+$15}' /proc/$P/stat); sleep 3; t1=$(awk '{print $14+$15}' /proc/$P/stat)
echo "cpu ticks in 3s (100/s): $((t1 - t0))"
kill -TERM $L; wait $L
