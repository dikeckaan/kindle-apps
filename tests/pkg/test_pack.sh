#!/bin/sh
# Checks every dist/*.kpkg: required files present, no symlinks, valid manifest; and dist/manifest.json lists them.
set -u
cd "$(dirname "$0")/../.."
fail=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fail=1; }
ls dist/packages/*/*.kpkg >/dev/null 2>&1 || { bad "no packages in dist/packages"; exit 1; }
for k in dist/packages/*/*.kpkg; do
    [ -f "$k" ] || continue
    t=$(mktemp -d)
    tar tzf "$k" | grep -qx "manifest.json" && ok "$k stores manifest.json at the root (no ./ prefix)" \
        || bad "$k must store 'manifest.json' without a ./ prefix (kpm cannot load it otherwise)"
    tar xzf "$k" -C "$t" || { bad "$k: not a tar.gz"; continue; }
    for f in manifest.json install.sh uninstall.sh launch.sh; do
        [ -f "$t/$f" ] && ok "$k has $f" || bad "$k lacks $f"
    done
    [ -z "$(find "$t" -type l)" ] && ok "$k has no symlinks" || bad "$k contains symlinks"
    python3 -m json.tool "$t/manifest.json" >/dev/null 2>&1 && ok "$k manifest is JSON" || bad "$k manifest invalid"
    python3 -c 'import json,sys
m=json.load(open(sys.argv[1]))
assert all(isinstance(d,dict) and isinstance(d.get("id"),str) for d in m["dependencies"])' "$t/manifest.json" 2>/dev/null \
        && ok "$k dependencies are {id,min,max} objects" || bad "$k dependencies must be objects with an id (kpm format)"
    id=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["id"])' "$t/manifest.json" 2>/dev/null)
    grep -q "\"$id\"" dist/manifest.json 2>/dev/null && ok "repo manifest lists $id" || bad "repo manifest misses $id"
    rm -rf "$t"
done
python3 -m json.tool dist/manifest.json >/dev/null 2>&1 && ok "dist/manifest.json is JSON" || bad "dist/manifest.json invalid"
exit $fail
