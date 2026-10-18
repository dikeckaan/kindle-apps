#!/bin/sh
# Minimal UCI engine for tests. FAKE_MODE=die exits on "go", FAKE_MODE=hang never answers "go".
while IFS= read -r line; do
    case "$line" in
        uci) echo "id name Fake"; echo "option name UCI_Elo type spin default 1350 min 1350 max 2850"; echo "uciok" ;;
        isready) echo "readyok" ;;
        go*)
            [ "$FAKE_MODE" = die ] && exit 3
            [ "$FAKE_MODE" = hang ] && continue
            echo "info depth 1 score cp 20"; echo "bestmove e2e4 ponder e7e5" ;;
        quit) exit 0 ;;
    esac
done
