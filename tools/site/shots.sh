#!/bin/sh
# shots.sh — renders the website's screenshots with the host simulator into site/shots/<name>.png (1000 px tall).
# Live data (weather, news, quakes, puzzle) is fetched by the simulator, so the pictures show real content.
# macOS: uses sips to scale. Usage: tools/site/shots.sh [name...]
set -eu
cd "$(dirname "$0")/../.."
OUT=site/shots
ROOT="$PWD/build/simroot-site"
mkdir -p "$OUT"
rm -rf "$ROOT" && cp -R build/simroot "$ROOT" 2>/dev/null || mkdir -p "$ROOT"
mkdir -p "$ROOT/apps" "$ROOT/versions"
for d in apps/*/pkg; do # every app in the menu (the binaries are not needed to draw the hub)
	a=$(basename "$(dirname "$d")")
	mkdir -p "$ROOT/apps/$a" && cp "$d/app.ini" "$ROOT/apps/$a/"
	# installed versions for the store picture: current, except a few that show an update
	id=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["id"])' "$d/manifest.json")
	v=$(python3 -c 'import json,sys;print(".".join(map(str,json.load(open(sys.argv[1]))["version"])))' "$d/manifest.json")
	case "$a" in pano|news|weather) v=2026.10.12 ;; esac
	echo "$v" > "$ROOT/versions/$id"
done
rm -f "$ROOT/data/pano/settings" # defaults (prayer times off)
mkdir -p "$ROOT/bin" && : > "$ROOT/bin/hub" && echo 2026.10.17 > "$ROOT/versions/kindle-apps-core"

# name app "script" [frame-pattern]  — the last PNG matching the pattern is kept
shot() {
	name=$1 app=$2 script=$3 pat=${4:-png}
	if [ -n "$ONLY" ] && ! echo " $ONLY " | grep -q " $name "; then return; fi
	KUI_ROOT="$ROOT" tools/sim/run.sh "$app" "$script" "build/sim/site/$name" >/dev/null 2>&1 || true
	f=$(ls build/sim/site/"$name"/*.png 2>/dev/null | grep -E "$pat" | tail -1)
	[ -n "$f" ] || { echo "no frame for $name" >&2; return; }
	sips -Z 1000 "$f" --out "$OUT/$name.png" >/dev/null
	echo "$OUT/$name.png"
}
ONLY="$*"

shot hub hub "none"
shot store hub "swipe right none tap 870 700 none tap 190 180 none" hub_005
shot pano pano "none"
shot power pano "none tap 540 45 none"
shot clock clock "none"
shot weather weather "none"
shot news news "none"
shot currency currency "none"
shot deprem deprem "none"
shot deprem-list deprem "none tap 990 60 none"
shot puzzle puzzle "none" _full
SKY_TIME=1792432800 shot sky sky "none" _full
shot siir siir "none"
shot ajanda ajanda "none" _full
shot odak odak "none" _full
shot wordle wordle "none"
shot sudoku sudoku "tap 536 440 none"
shot mines mines "tap 536 440 tap 300 700 none"
shot solitaire solitaire "tap 536 440 none"
shot chess chess "tap 536 455 tap 536 268 none"
shot ebooks ebooks "none"
