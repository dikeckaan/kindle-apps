#!/usr/bin/env python3
"""Translatable text: T("...") and N_("...") in core/ and apps/ C sources, plus app.ini names.

  extract.py            writes lang/template.txt (one source text per line, escaped)
  extract.py --check    checks every lang/<code>.txt: no missing/extra/duplicate keys, empty translations,
                        or printf conversions (%d, %s, %.1f …) that differ from the source; exit 1 on problems
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CALL = re.compile(r'\b(?:T|N_)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
FMT = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXeEfgGcsp])')
LANGS = "en de fr es it pt nl pl cs ro hu sv nb da fi el ru uk az".split()


def c_unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            if n == "x":
                j = i + 2
                while j < len(s) and s[j] in "0123456789abcdefABCDEF":
                    j += 1
                out.append(bytes([int(s[i + 2:j], 16)]))
                i = j
                continue
            out.append({"n": b"\n", "t": b"\t", '"': b'"', "\\": b"\\", "'": b"'"}.get(n, n.encode()))
            i += 2
            continue
        out.append(c.encode("utf-8"))
        i += 1
    return b"".join(out).decode("utf-8")


def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def unesc(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out.append({"n": "\n", "t": "\t"}.get(s[i + 1], s[i + 1]))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def sources():
    keys = {}
    files = glob.glob(os.path.join(ROOT, "core/**/*.c"), recursive=True) + glob.glob(os.path.join(ROOT, "apps/**/*.c"), recursive=True)
    for f in sorted(files):
        if "/vendor/" in f:
            continue
        text = open(f, encoding="utf-8").read()
        # N_ tables: every literal inside a { ... } initializer that is marked /* N_ */
        for m in re.finditer(r"/\*\s*N_\s*\*/(.*?)\}", text, re.S):
            for lit in LIT.findall(m.group(1)):
                keys.setdefault(c_unescape(lit), os.path.relpath(f, ROOT))
        for m in CALL.finditer(text):
            s = "".join(c_unescape(x) for x in LIT.findall(m.group(1)))
            if s:
                keys.setdefault(s, os.path.relpath(f, ROOT))
    for ini in sorted(glob.glob(os.path.join(ROOT, "apps/*/pkg/app.ini"))):
        for line in open(ini, encoding="utf-8"):
            if line.startswith("name="):
                keys.setdefault(line[5:].strip(), os.path.relpath(ini, ROOT))
    return keys


def read_catalog(path):
    entries, problems = {}, []
    for n, line in enumerate(open(path, encoding="utf-8").read().split("\n"), 1):
        if not line or line.startswith("#"):
            continue
        if "\t" not in line:
            problems.append(f"{path}:{n}: no TAB")
            continue
        src, dst = line.split("\t", 1)
        src, dst = unesc(src), unesc(dst)
        if src in entries:
            problems.append(f"{path}:{n}: duplicate key {src!r}")
        if not dst.strip():
            problems.append(f"{path}:{n}: empty translation for {src!r}")
        entries[src] = dst
    return entries, problems


def main():
    keys = sources()
    os.makedirs(os.path.join(ROOT, "lang"), exist_ok=True)
    if "--check" not in sys.argv:
        with open(os.path.join(ROOT, "lang/template.txt"), "w", encoding="utf-8") as f:
            f.write("# Translatable texts (Turkish source). Catalog lines: source<TAB>translation; \\n and \\t escaped.\n")
            for k in sorted(keys):
                f.write(f"# {keys[k]}\n{esc(k)}\t\n")
        print(f"{len(keys)} texts -> lang/template.txt")
        return 0
    bad = 0
    for code in LANGS:
        path = os.path.join(ROOT, "lang", f"{code}.txt")
        if not os.path.exists(path):
            print(f"{code}: missing catalog")
            bad += 1
            continue
        entries, problems = read_catalog(path)
        missing = [k for k in keys if k not in entries]
        extra = [k for k in entries if k not in keys]
        for k, v in entries.items():
            # %% takes no argument, so translations may place it anywhere ("%%%d" → "%d %%")
            if k in keys and [c for c in FMT.findall(k) if c != "%%"] != [c for c in FMT.findall(v) if c != "%%"]:
                problems.append(f"{code}: format mismatch {k!r} -> {v!r}")
        for p in problems:
            print(p)
        for k in missing[:20]:
            print(f"{code}: missing {k!r}")
        for k in extra[:20]:
            print(f"{code}: extra {k!r}")
        bad += len(problems) + len(missing) + len(extra)
        print(f"{code}: {len(entries)} entries, {len(missing)} missing, {len(extra)} extra, {len(problems)} problems")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
