#!/usr/bin/env python3
"""Builds the Bulmaca word + clue list (tools/bulmaca/words.tsv) from licensed sources.

  words    Turkish lemmas (nouns, adjectives, adverbs, verbs) of the Turkish Wiktionary (tr.wiktionary.org,
           CC BY-SA 4.0), as extracted by wiktextract and published by kaikki.org (raw JSONL of the trwiktionary
           dump), kept only when they also occur among the most frequent words of hermitdave/FrequencyWords
           tr_50k (CC BY-SA 4.0): common words people know.
  clues    a short Wiktionary definition of the word (its first usable sense): no form-of / alt-of / dialectal /
           obsolete / slang / vulgar senses, nothing tagged or sourced as TDK, at most MAX_CLUE characters,
           never containing the answer (or a word that starts with its stem).

Family-friendly: a word with any vulgar/offensive/slang sense or a sexual/crime/death/violence topic is dropped,
and so is every word or clue that contains a word of BLOCK.

Usage: tools/bulmaca/build-words.py [--cache DIR] [out.tsv]
The kaikki.org file is replaced by a newer extraction about once a week, so the committed words.tsv is the
reproducible artefact; --cache keeps the downloaded files (their SHA-256 sums go to stdout for data/SOURCES).
"""
import gzip, hashlib, io, json, os, re, sys, urllib.request

KAIKKI = "https://kaikki.org/trwiktionary/raw-wiktextract-data.jsonl.gz"
FREQ = "https://raw.githubusercontent.com/hermitdave/FrequencyWords/525f9b560de45753a5ea01069454e72e9aa541c6/content/2018/tr/tr_50k.txt"
TR = set("abcçdefgğhıijklmnoöprsştuüvyz")
COMMON = 20000      # frequency rank limit (verbs: the root's best inflected form counts)
MIN_LEN, MAX_LEN = 3, 11
MAX_CLUE = 40
POS = {"noun", "adj", "adv", "verb"}
POS_ORDER = {"adj": 0, "noun": 1, "adv": 2, "verb": 3}
BAD_TAGS = {"form-of", "alt-of", "no-gloss", "dialectal", "obsolete", "slang", "vulgar", "misspelling", "abbreviation",
            "short-form", "offensive", "derogatory", "archaic", "rare", "jargon", "Internet", "informal"}
BAD_WORD_TAGS = {"vulgar", "offensive", "derogatory", "slang"}
BAD_TOPIC = re.compile(r"cinsel|üreme organ|argo|kaba|küfür|hakaret|aşağılay|suç|şiddet|ölüm|cinayet|uyuşturucu|"
                       r"genel ev|fahişe|savaş|silah|\bdin\b|tasavvuf|Ehl-i|mezhep", re.I)
# whole-word blocklist for answers and clue words (insults, slurs, sexual, violent, drug words)
BLOCK = set("""
aptal salak ahmak gerizekalı budala enayi keriz kahpe orospu fahişe pezevenk ibne puşt kaltak sürtük göt meme
seks sevişmek cinsel çıplak tecavüz zina piç gavur zenci kafir dinsiz imansız lanet kahrolmak öldürmek ölmek ölüm
ölü cinayet katil intihar boğmak bıçaklamak vurmak dövmek kan kanlı savaş silah tabanca tüfek bomba patlayıcı
esrar eroin kokain uyuşturucu sarhoş alkol rakı şarap bira içki kumar hırsız hırsızlık soygun dolandırıcı
yalancı pislik bok sıçmak işemek osurmak kusmak cehennem şeytan şerefsiz namussuz alçak hain domuz eşek köpek
light ispanya sik sikmek gibmek çiş sidik dışkı amcık yarak taşak dübür kıç popo sapık genelev kerhane
""".split())
# frequent function words and inflected forms that Wiktionary also lists as rare nouns (bu "yerde", olan "oğlan")
FUNCTION = set("""
bir bu şu ne ben sen biz siz onlar onun bunun şunun ama fakat ancak lakin ve ile veya ya de da ki mi mı mu mü
var yok değil gibi kadar daha çok en hiç bile diye için göre sonra önce beri dek değin rağmen
olan olur olacak olabilir olarak olmuş oldu geldi gitti dedi yani evet hayır hani işte nasıl neden niye
nerede nereye burada orada şurada şimdi artık hala hâlâ yine gene belki sadece yalnız hep hepsi kimse biri
şey öyle böyle şöyle içinde yüzden hakkında aslında üzere sanki zaten bazen hemen asla
""".split())
STOP_CLUE = re.compile(r"\b(TDK|Türk Dil Kurumu)\b|https?://|\[|\]|=|\||:|[Ee]ş anlam|[Kk]arşıt anlam")


def fetch(url, cache):
    name = os.path.join(cache, os.path.basename(url)) if cache else None
    if name and os.path.exists(name):
        data = open(name, "rb").read()
    else:
        with urllib.request.urlopen(url, timeout=300) as r:
            data = r.read()
        if name:
            open(name, "wb").write(data)
    print(f"{url}  sha256={hashlib.sha256(data).hexdigest()}  bytes={len(data)}")
    return data


def lower(w):
    return w.replace("I", "ı").replace("İ", "i").lower()


def fold(w):
    return lower(w).replace("â", "a").replace("î", "i").replace("û", "u")


def upper_first(s):
    if not s:
        return s
    c = s[0]
    c = {"i": "İ", "ı": "I"}.get(c, c.upper())
    return c + s[1:]


def clean_gloss(g):
    g = g.strip()
    g = re.sub(r"\s*\([^()]*\)", "", g)          # parentheticals
    g = re.sub(r"\s+", " ", g).strip(" .;:,")
    if ";" in g:                                   # first of several near-synonyms
        g = g.split(";")[0].strip(" .,")
    parts = g.split(",")
    if len(g) > MAX_CLUE and len(parts) > 1 and all(len(p.split()) <= 2 for p in parts):
        out = ""                                   # a list of synonyms "a, b, c" → as many leading ones as fit
        for p in parts:
            cand = (out + "," + p) if out else p
            if len(cand.strip()) > MAX_CLUE:
                break
            out = cand
        g = out.strip(" .,")
    return g


def edits(a, b):
    prev = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        cur = [i]
        for j, y in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (x != y)))
        prev = cur
    return prev[-1]


def stem(w):
    return w[:max(3, len(w) - 2)] if len(w) > 3 else w


RANK = {}


def known(x):
    return RANK.get(x, 10 ** 9) < 40000 or (x.endswith(("mek", "mak")) and len(x) > 4)


def clue_ok(word, g, verb_root):
    if not g or len(g) > MAX_CLUE or len(g) < 3 or STOP_CLUE.search(g):
        return False
    words = re.findall(r"[a-zçğıöşüâîû]+", fold(g))
    if not words:
        return False
    for s in {stem(word)} | ({verb_root} if verb_root and len(verb_root) >= 3 else set()):
        if any(x.startswith(s) for x in words):
            return False
    for x in words:  # shares the answer's start (İnsan → insanoğlu, Zeki → zekalı)
        k = 0
        while k < min(len(x), len(word)) and x[k] == word[k]:
            k += 1
        if k >= 4 or (k >= 3 and min(len(x), len(word)) <= 4):
            return False
    if any(x in BLOCK for x in words):
        return False
    if re.match(r"(.* )?(biçimi|çoğulu|yazımı|kısaltması|hâli|hali)$", fold(g)):
        return False
    if words[0] in ("bu", "o", "u", "şu", "bunun", "onun", "bunda", "bundan", "böyle"):
        return False  # refers to the sense before it ("Bu süre içindeki karanlık")
    if len(words) == 1 and edits(words[0], word) <= 2:
        return False  # a spelling variant (gidmek: "Gitmek")
    if len(words) == 1 and RANK.get(words[0], 10 ** 9) >= 30000 and not words[0].endswith(("mek", "mak")):
        return False  # a rare synonym ("Temek", "Gözene")
    if "," in g and not all(known(x) for x in words):
        return False
    if len(words) > 1 and re.search(r"(nı|ni|nu|nü|nın|nin|nun|nün|ına|ine|una|üne|yla|yle|ya|ye|nda|nde|ndan|nden)$", words[-1]) \
            and not re.search(r"(ya|ye)$", words[-1]) or g[0] in "–-":
        return False  # cut mid-sentence ("Duruşmada bilgisine")
    if re.search(r"\s[A-ZÇĞİÖŞÜ]", g):
        return False  # a proper name or a leaked label ("Askerlik Bu sınıftan …")
    if "$" in g or any(c.isdigit() for c in g):
        return False
    return True


def main():
    args = sys.argv[1:]
    cache = None
    if args[:1] == ["--cache"]:
        cache, args = args[1], args[2:]
        os.makedirs(cache, exist_ok=True)
    out = args[0] if args else "tools/bulmaca/words.tsv"
    rank = {}
    for i, line in enumerate(fetch(FREQ, cache).decode("utf-8").splitlines()):
        if line.strip():
            rank.setdefault(fold(line.split()[0]), i)
    RANK.update(rank)
    def verb_rank(root):
        """best rank of a common finite form of the verb (gel- → geldi, geliyor, gelmiş, gelecek, gelmek)"""
        forms = [root + "mek", root + "mak"]
        for v in "ıiuü":
            forms += [root + c + v for c in "dt"] + [root + v + "yor", root + "m" + v + "ş", root + "yor"]
        forms += [root + "ecek", root + "acak", root + "yecek", root + "yacak"]
        return min(rank.get(f, 10 ** 9) for f in forms)

    entries = []
    form_of = {}  # common word → lemmas that list it among their inflected forms
    with gzip.open(io.BytesIO(fetch(KAIKKI, cache))) as f:
        for line in f:
            d = json.loads(line)
            if d.get("lang_code") != "tr":
                continue
            raw = d.get("word", "")
            for fm in d.get("forms", []):
                x = fold(fm.get("form", ""))
                if x != fold(raw) and x in rank and rank[x] < COMMON:
                    form_of.setdefault(x, set()).add(fold(raw))
            if d.get("pos") not in POS:
                continue
            w = fold(raw)
            if raw != lower(raw) or not set(w) <= TR:  # proper nouns, phrases, foreign letters
                continue
            entries.append((w, d))
    best = {}  # word → (rank, clue)
    dropped = set()
    for w, d in entries:
        verb_root = None
        if d["pos"] == "verb":
            if not w.endswith(("mek", "mak")) or len(w) < 5:
                continue
            verb_root = w[:-3]
            r = verb_rank(verb_root)
        else:
            r = rank.get(w, 10 ** 9)
        if not MIN_LEN <= len(w) <= MAX_LEN or r >= COMMON or w in BLOCK or w in FUNCTION:
            continue
        senses = d.get("senses", [])
        if any(set(s.get("tags", [])) & BAD_WORD_TAGS for s in senses) or \
           any(BAD_TOPIC.search(c) for s in senses for c in s.get("categories", []) + s.get("raw_tags", [])):
            dropped.add(w)
            continue
        usable = [s for s in senses if not set(s.get("tags", [])) & BAD_TAGS and not s.get("form_of") and not s.get("alt_of")]
        # an inflected form of another word (yüzden, geldi) that Wiktionary also lists with one rare sense
        if w in form_of and len(usable) < 2 and d["pos"] != "verb":
            continue
        for si, s in enumerate(usable[:2]):  # the main senses; later ones are often specialised or historical
            if any("TDK" in e.get("ref", "") or "Türk Dil Kurumu" in e.get("ref", "") for e in s.get("examples", [])):
                continue
            gl = s.get("glosses") or []
            if not gl:
                continue
            g = clean_gloss(gl[-1])
            if verb_root and not re.search(r"(mek|mak)$", fold(g)):
                continue  # a verb is clued with a verb ("Erişmek, ulaşmak")
            if clue_ok(w, g, verb_root):
                key = (si, POS_ORDER[d["pos"]])  # the main sense; between entries an adjective before a noun
                if w not in best or (r, key) < (best[w][0], best[w][2]):
                    best[w] = (r, upper_first(g), key)
                break
    for w in dropped:
        best.pop(w, None)
    rows = sorted(best.items(), key=lambda kv: (kv[1][0], kv[0]))
    with open(out, "w", encoding="utf-8") as f:
        f.write("# Bulmaca words: answer<TAB>frequency rank<TAB>clue — generated by tools/bulmaca/build-words.py\n")
        f.write("# Clues: Turkish Wiktionary (CC BY-SA 4.0) via kaikki.org/wiktextract; ranks: FrequencyWords tr_50k (CC BY-SA 4.0)\n")
        for w, (r, g, _) in rows:
            f.write(f"{w}\t{r}\t{g}\n")
    print(f"{len(rows)} words -> {out}")


if __name__ == "__main__":
    main()
