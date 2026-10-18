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
for id in core 2048 chess sleep ssh firewall weather clock currency news; do
    ls dist/packages/kindle-apps-$id/*.kpkg >/dev/null 2>&1 && ok "package kindle-apps-$id built" || bad "package kindle-apps-$id missing"
done
has() { tar tzf dist/packages/$1/*.kpkg | grep -qx "$2" && ok "$1 has $2" || bad "$1 lacks $2"; }
has kindle-apps-chess files/stockfish
has kindle-apps-chess files/COPYING-stockfish
has kindle-apps-ssh files/dropbear
has kindle-apps-ssh files/dropbearkey
has kindle-apps-ssh files/LICENSE-dropbear
t=$(mktemp -d); tar xzf dist/packages/kindle-apps-ssh/*.kpkg -C "$t"
# an upgrade must stop the running server (and its sessions) before the binary is replaced
awk '/pre-install.sh/{s=NR} /cp -f/{c=NR} END{exit !(s && c && s < c)}' "$t/install.sh" \
    && grep -q "(deleted)" "$t/pre-install.sh" && grep -q "(deleted)" "$t/uninstall.sh" \
    && ok "ssh package stops every dropbear process before install and on uninstall" || bad "ssh package does not stop the server before replacing it"
rm -rf "$t"
for k in dist/packages/*/*.kpkg; do
    t=$(mktemp -d); tar xzf "$k" -C "$t"
    sh -n "$t/install.sh" && sh -n "$t/uninstall.sh" && ! grep -q "@[A-Z_]*@" "$t/install.sh" "$t/uninstall.sh" \
        && ok "$k scripts parse, no template markers" || bad "$k install/uninstall script broken"
    rm -rf "$t"
done
python3 -m json.tool dist/manifest.json >/dev/null 2>&1 && ok "dist/manifest.json is JSON" || bad "dist/manifest.json invalid"
exit $fail
