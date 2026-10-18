#!/usr/bin/env python3
"""Generates apps/sky/skydata.c (stars, constellation figures, names) for Gökyüzü.

  tools/sky/gen_skydata.py            downloads the sources (pinned, sha256-checked) into build/sky-src/ and writes
                                      apps/sky/skydata.c

Sources:
  * Yale Bright Star Catalogue, 5th revised ed. (Hoffleit & Warren 1991), CDS catalogue V/50 — public domain.
    J2000 positions, V magnitudes, Bayer/Flamsteed designations.
  * d3-celestial (Olaf Frohn), data/constellations.lines.json — BSD 3-Clause (apps/sky/data/LICENSE).
    Constellation stick figures; their vertices are matched to catalogue stars.
Kept: every star of magnitude <= MAG_LIMIT plus the (fainter) stars the figures need.
Turkish star and constellation names are ours (marked with N_() for the translation catalogs).
"""
import gzip, hashlib, json, math, os, sys, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "build", "sky-src")
OUT = os.path.join(ROOT, "apps", "sky", "skydata.c")
MAG_LIMIT = 4.5

FILES = {
    "bsc5.gz": ("https://cdsarc.cds.unistra.fr/ftp/V/50/catalog.gz",
                "3dc44b1e90be8fbe5bcc7656032560f51275f985c7e3f783c9028e1838ec7bed"),
    "constellations.lines.json": ("https://raw.githubusercontent.com/ofrohn/d3-celestial/"
                                  "7e720a3de062059d4c5400a379146a601d9010e0/data/constellations.lines.json",
                                  "294f66bef5d5cf50b1e17f16d2efa1d97a15131612c68dd935adef6e7373e13c"),
}

GREEK = {"Alp": "α", "Bet": "β", "Gam": "γ", "Del": "δ", "Eps": "ε", "Zet": "ζ", "Eta": "η", "The": "θ", "Iot": "ι",
         "Kap": "κ", "Lam": "λ", "Mu": "μ", "Nu": "ν", "Xi": "ξ", "Omi": "ο", "Pi": "π", "Rho": "ρ", "Sig": "σ",
         "Tau": "τ", "Ups": "υ", "Phi": "φ", "Chi": "χ", "Psi": "ψ", "Ome": "ω"}

LATIN = {
    "And": "Andromeda", "Ant": "Antlia", "Aps": "Apus", "Aqr": "Aquarius", "Aql": "Aquila", "Ara": "Ara", "Ari": "Aries",
    "Aur": "Auriga", "Boo": "Boötes", "Cae": "Caelum", "Cam": "Camelopardalis", "Cnc": "Cancer", "CVn": "Canes Venatici",
    "CMa": "Canis Major", "CMi": "Canis Minor", "Cap": "Capricornus", "Car": "Carina", "Cas": "Cassiopeia",
    "Cen": "Centaurus", "Cep": "Cepheus", "Cet": "Cetus", "Cha": "Chamaeleon", "Cir": "Circinus", "Col": "Columba",
    "Com": "Coma Berenices", "CrA": "Corona Australis", "CrB": "Corona Borealis", "Crv": "Corvus", "Crt": "Crater",
    "Cru": "Crux", "Cyg": "Cygnus", "Del": "Delphinus", "Dor": "Dorado", "Dra": "Draco", "Equ": "Equuleus",
    "Eri": "Eridanus", "For": "Fornax", "Gem": "Gemini", "Gru": "Grus", "Her": "Hercules", "Hor": "Horologium",
    "Hya": "Hydra", "Hyi": "Hydrus", "Ind": "Indus", "Lac": "Lacerta", "Leo": "Leo", "LMi": "Leo Minor", "Lep": "Lepus",
    "Lib": "Libra", "Lup": "Lupus", "Lyn": "Lynx", "Lyr": "Lyra", "Men": "Mensa", "Mic": "Microscopium",
    "Mon": "Monoceros", "Mus": "Musca", "Nor": "Norma", "Oct": "Octans", "Oph": "Ophiuchus", "Ori": "Orion",
    "Pav": "Pavo", "Peg": "Pegasus", "Per": "Perseus", "Phe": "Phoenix", "Pic": "Pictor", "Psc": "Pisces",
    "PsA": "Piscis Austrinus", "Pup": "Puppis", "Pyx": "Pyxis", "Ret": "Reticulum", "Sge": "Sagitta",
    "Sgr": "Sagittarius", "Sco": "Scorpius", "Scl": "Sculptor", "Sct": "Scutum", "Ser": "Serpens", "Sex": "Sextans",
    "Tau": "Taurus", "Tel": "Telescopium", "Tri": "Triangulum", "TrA": "Triangulum Australe", "Tuc": "Tucana",
    "UMa": "Ursa Major", "UMi": "Ursa Minor", "Vel": "Vela", "Vir": "Virgo", "Vol": "Volans", "Vul": "Vulpecula",
}

# well-known constellations get a Turkish name (and a label on the chart)
TURKISH_CON = {
    "UMa": "Büyükayı", "UMi": "Küçükayı", "Ori": "Avcı", "Cas": "Kraliçe", "Cyg": "Kuğu", "Sco": "Akrep", "Leo": "Aslan",
    "Tau": "Boğa", "Gem": "İkizler", "Cep": "Kral", "Dra": "Ejderha", "Boo": "Çoban", "Lyr": "Çalgı", "Aql": "Kartal",
    "Cnc": "Yengeç", "Vir": "Başak", "Lib": "Terazi", "Sgr": "Yay", "Cap": "Oğlak", "Aqr": "Kova", "Psc": "Balıklar",
    "Ari": "Koç", "CMa": "Büyükköpek", "CMi": "Küçükköpek", "Peg": "Kanatlı At", "And": "Andromeda", "Per": "Kahraman",
    "Aur": "Arabacı", "Her": "Herkül", "Oph": "Yılancı",
}

# the brightest / best-known stars: (Bayer letter, constellation) → Turkish name
TURKISH_STAR = [
    ("Alp", "CMa", "Sirius"), ("Alp", "Car", "Süheyl"), ("Alp", "Boo", "Arktürüs"), ("Alp", "Lyr", "Vega"),
    ("Alp", "Aur", "Kapella"), ("Bet", "Ori", "Rigel"), ("Alp", "CMi", "Prokyon"), ("Alp", "Ori", "Betelgöz"),
    ("Alp", "Aql", "Altair"), ("Alp", "Tau", "Aldebaran"), ("Alp", "Sco", "Antares"), ("Alp", "Vir", "Spika"),
    ("Bet", "Gem", "Polluks"), ("Alp", "PsA", "Fomalhaut"), ("Alp", "Cyg", "Deneb"), ("Alp", "Leo", "Regulus"),
    ("Alp", "Gem", "Kastor"), ("Alp", "UMi", "Kutup Yıldızı"), ("Bet", "Per", "Algol"), ("Zet", "UMa", "Mizar"),
]


def fetch(name):
    url, sha = FILES[name]
    path = os.path.join(SRC, name)
    if not os.path.exists(path):
        os.makedirs(SRC, exist_ok=True)
        print(f"fetch {url}")
        data = urllib.request.urlopen(url).read()
        with open(path, "wb") as f:
            f.write(data)
    data = open(path, "rb").read()
    got = hashlib.sha256(data).hexdigest()
    if got != sha:
        sys.exit(f"{name}: sha256 {got}, expected {sha}")
    return data


def parse_bsc(data):
    stars = []
    for line in gzip.decompress(data).decode("latin-1").splitlines():
        line = line.ljust(197)
        if not line[75:77].strip() or not line[102:107].strip():
            continue  # no J2000 position (novae, galaxies) or no V magnitude
        hr = int(line[0:4])
        ra = (int(line[75:77]) + int(line[77:79]) / 60 + float(line[79:83]) / 3600) * 15
        dec = int(line[84:86]) + int(line[86:88]) / 60 + int(line[88:90]) / 3600
        if line[83] == "-":
            dec = -dec
        flam, bayer, sup, con = line[4:7].strip(), line[7:10].strip(), line[10:11].strip(), line[11:14].strip()
        stars.append({"hr": hr, "ra": ra, "dec": dec, "mag": float(line[102:107]), "flam": flam,
                      "bayer": bayer, "sup": sup, "con": con})
    return stars


def unit(ra, dec):
    r, d = math.radians(ra), math.radians(dec)
    return (math.cos(d) * math.cos(r), math.cos(d) * math.sin(r), math.sin(d))


def sep(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))))


def desig(s):
    if s["bayer"] in GREEK:
        sup = {"1": "¹", "2": "²", "3": "³"}.get(s["sup"], "")
        return f"{GREEK[s['bayer']]}{sup} {s['con']}"
    if s["flam"]:
        return f"{s['flam']} {s['con']}"
    return f"HR {s['hr']}"


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    bsc = parse_bsc(fetch("bsc5.gz"))
    lines = json.loads(fetch("constellations.lines.json"))
    for s in bsc:
        s["u"] = unit(s["ra"], s["dec"])
    # constellations in catalogue order of their ids (Ser appears twice: Caput and Cauda)
    cons = []
    for f in lines["features"]:
        if f["id"] not in cons:
            cons.append(f["id"])
    for c in cons:
        if c not in LATIN:
            sys.exit(f"unknown constellation {c}")
    # match every figure vertex to the nearest catalogue star
    cell = {}
    for i, s in enumerate(bsc):
        cell.setdefault((int(s["ra"] // 5), int((s["dec"] + 90) // 5)), []).append(i)

    def nearest(ra, dec):
        u, best, bi = unit(ra, dec), 9.0, -1
        for dra in (-1, 0, 1):
            for dde in (-1, 0, 1):
                key = ((int(ra // 5) + dra) % 72, int((dec + 90) // 5) + dde)
                for i in cell.get(key, []):
                    d = sep(u, bsc[i]["u"])
                    if d < best:
                        best, bi = d, i
        # near the poles the 5° cells are narrow in RA: fall back to a full search
        if best > 0.2 and abs(dec) > 70:
            for i, s in enumerate(bsc):
                d = sep(u, s["u"])
                if d < best:
                    best, bi = d, i
        return bi, best

    segs, used, worst = [], set(), 0.0
    for f in lines["features"]:
        ci = cons.index(f["id"])
        for poly in f["geometry"]["coordinates"]:
            idx = []
            for lon, lat in poly:
                i, d = nearest(lon % 360, lat)
                if d > 0.2:
                    sys.exit(f"{f['id']}: no star near {lon},{lat} ({d:.2f}°)")
                worst = max(worst, d)
                idx.append(i)
                used.add(i)
            for a, b in zip(idx, idx[1:]):
                if a != b and (a, b, ci) not in segs and (b, a, ci) not in segs:
                    segs.append((a, b, ci))
    keep = sorted({i for i, s in enumerate(bsc) if s["mag"] <= MAG_LIMIT} | used, key=lambda i: (bsc[i]["mag"], bsc[i]["hr"]))
    newi = {old: new for new, old in enumerate(keep)}
    names = {}
    for bayer, con, tr in TURKISH_STAR:
        cand = [i for i in keep if bsc[i]["bayer"] == bayer and bsc[i]["con"] == con]
        if not cand:
            sys.exit(f"no star {bayer} {con}")
        names[min(cand, key=lambda i: bsc[i]["mag"])] = tr
    rank = {f["id"]: int(f["properties"]["rank"]) for f in lines["features"]}

    out = ["/* Generated by tools/sky/gen_skydata.py — do not edit.",
           " * Stars: Yale Bright Star Catalogue, 5th revised ed. (Hoffleit & Warren 1991, CDS V/50), public domain;",
           f" * magnitude <= {MAG_LIMIT} plus the stars of the figures, brightest first, J2000.",
           " * Constellation figures: d3-celestial (Olaf Frohn), BSD 3-Clause, see apps/sky/data/LICENSE. */",
           '#include <stddef.h>', '#include "kui.h"', '#include "skydata.h"', ""]
    out.append("const SkyCon sky_cons[] = {")
    for c in cons:
        tr = f'N_({cstr(TURKISH_CON[c])})' if c in TURKISH_CON else "NULL"
        out.append(f'\t{{ "{c}", {cstr(LATIN[c])}, {tr}, {rank[c]} }},')
    out.append("};")
    out.append(f"const int sky_n_cons = {len(cons)};")
    out.append("")
    out.append("const SkyStar sky_stars[] = {")
    for i in keep:
        s = bsc[i]
        ci = cons.index(s["con"]) if s["con"] in cons else -1
        nm = f'N_({cstr(names[i])})' if i in names else "NULL"
        out.append(f'\t{{ {s["ra"]:.4f}f, {s["dec"]:.4f}f, {s["mag"]:.2f}f, {ci}, {cstr(desig(s))}, {nm} }},')
    out.append("};")
    out.append(f"const int sky_n_stars = {len(keep)};")
    out.append("")
    out.append("const SkySeg sky_segs[] = {")
    row = []
    for a, b, ci in segs:
        row.append(f"{{ {newi[a]}, {newi[b]}, {ci} }}")
        if len(row) == 6:
            out.append("\t" + ", ".join(row) + ",")
            row = []
    if row:
        out.append("\t" + ", ".join(row) + ",")
    out.append("};")
    out.append(f"const int sky_n_segs = {len(segs)};")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"{len(keep)} stars ({len(used)} in figures), {len(segs)} segments, {len(cons)} constellations; "
          f"worst vertex match {worst * 3600:.0f}\" -> {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
