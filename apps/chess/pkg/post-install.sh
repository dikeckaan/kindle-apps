# Satranç Bulmacası now lives inside Satranç (Menü → Bulmacalar): its puzzles, history and streak move to
# data/chess/puzzle, then the old standalone app (its Uygulamalar entry) and its version file are removed.
# Only /mnt/us/kindle-apps is touched; kpm's own record of kindle-apps-puzzle is left alone (see README).
R=/mnt/us/kindle-apps
if [ -d "$R/data/puzzle" ] && [ ! -e "$R/data/chess/puzzle" ]; then
    # the old app stays when the move fails: nothing is lost, and Satranç retries the move on first use
    mkdir -p "$R/data/chess" && mv "$R/data/puzzle" "$R/data/chess/puzzle" || exit 0
fi
rm -rf "$R/apps/puzzle"
rm -f "$R/versions/kindle-apps-puzzle"
exit 0
