#!/bin/sh
# gen-manifest.sh <dist-dir> — writes <dist>/manifest.json (KMC kpm repo format v2) from the packages present.
set -eu
python3 - "$1" <<'PY'
import json, os, sys, tarfile
dist = sys.argv[1]
pkgs = {}
for pid in sorted(os.listdir(os.path.join(dist, "packages"))):
    d = os.path.join(dist, "packages", pid)
    for f in sorted(os.listdir(d)):
        if not f.endswith(".kpkg"):
            continue
        with tarfile.open(os.path.join(d, f)) as t:
            name = next(n for n in t.getnames() if n.lstrip("./") == "manifest.json")
            m = json.load(t.extractfile(name))
        p = pkgs.setdefault(m["id"], {"name": m["name"], "author": m["author"],
                                      "description": m["description"], "artifacts": []})
        p["artifacts"].append({"url": f"packages/{pid}/{f}", "version": m["version"],
                               "dependencies": m["dependencies"],
                               "supported_platforms": m["supported_platforms"]})
repo = {"manifest_version": 2, "id": "kindle-apps", "name": "Kindle Apps (TR)",
        "description": "Kaan Dikeç'in Türkçe Kindle uygulamaları", "packages": pkgs}
with open(os.path.join(dist, "manifest.json"), "w", encoding="utf-8") as f:
    json.dump(repo, f, ensure_ascii=False, indent=2)
print(os.path.join(dist, "manifest.json"))
PY
