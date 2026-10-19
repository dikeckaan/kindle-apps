#!/usr/bin/env python3
"""Builds the Kelime Öğren word lists (apps/kelime/data/<lang>.txt) from the cache made by tools/kelime/fetch.py.

For every language L in LANGS:
  1. Lemmas are ranked by how often they and their inflected forms occur in the OpenSubtitles 2018 frequency list
     (FrequencyWords); forms are mapped to lemmas with Wiktionary's inflection tables and "form of" entries.
  2. Level. English: the CEFR-J Wordlist 1.5 (A1–B2) and the Octanove Vocabulary Profile (C1/C2). The other
     languages have no open CEFR list, so their level is an APPROXIMATION by frequency rank: the BANDS[0] most
     frequent usable lemmas are "A1", the next ones up to BANDS[1] "A2", and so on.
  3. Part of speech and sense: when a word has several (Turkish "ben" = I / mole), the one whose translations
     occur in the Tatoeba translations of sentences with the word wins; translations are ranked the same way.
  4. Definition in L itself: Simple English Wiktionary for English, else L's own Wiktionary edition (the sense
     with the most translations, not a form-of/archaic/vulgar one). Every occurrence of the word is marked
     {like this} so a question can hide it.
  5. Translations into every other language: L's translation tables, the other editions' tables read backwards,
     English glosses of en.wiktionary, and as a last resort a pivot through English that needs two paths.
  6. Example: a short Tatoeba sentence containing the word, with its Tatoeba translations; else a Wiktionary
     usage example (with en.wiktionary's English translation). The word is marked {like this}.
  Vulgar, offensive and derogatory words or senses, proper names and abbreviations are left out.

Output (UTF-8; '#' header lines, then one word per line, 8 tab-separated fields):
  word  article  pos  level(1-6)  definition  example  translations  example-translations
  translations / example-translations: "tr=köpek|en=dog|…" (one text per native language)

Usage: tools/kelime/build-words.py <cache dir> [outdir]   (outdir default apps/kelime/data)
"""
import collections, csv, datetime, gzip, json, os, re, sys, unicodedata

LANGS = ["tr", "en", "de", "fr", "es", "it", "pt"]
BANDS = [450, 1100, 2000, 3100, 4300, 5600]  # cumulative frequency ranks of usable lemmas for A1 … C2
ENGLISH_MAX = {1: 900, 2: 1100, 3: 1300, 4: 1300, 5: 1100, 6: 1000}  # per CEFR level, most frequent first
POS_MAP = {"noun": "noun", "verb": "verb", "adj": "adj", "adv": "adv", "pron": "pron", "prep": "prep", "postp": "prep",
           "conj": "conj", "num": "num", "det": "det", "article": "det", "intj": "intj", "phrase": "phrase"}
POS_PREF = ["noun", "verb", "adj", "adv", "pron", "prep", "conj", "num", "det", "intj", "phrase"]
CEFRJ_POS = {"noun": "noun", "verb": "verb", "adjective": "adj", "adverb": "adv", "pronoun": "pron", "preposition": "prep",
             "conjunction": "conj", "number": "num", "determiner": "det", "interjection": "intj"}
BAD_TAGS = {"vulgar", "offensive", "derogatory", "slur", "pejorative", "ethnic", "obscene", "sexual"}
SKIP_SENSE_TAGS = BAD_TAGS | {"form-of", "obsolete", "archaic", "dated", "rare", "historical", "abbreviation", "initialism",
                              "acronym", "alt-of", "misspelling", "nonstandard", "dialectal", "regional", "slang"}
# texts about sex, drugs or insults are not shown even when Wiktionary does not tag them
SENSITIVE = re.compile(r"\bsex|penis|vagin|genital|cinsel|erkeklik organ|kadınlık organ|üreme organ|orgasm|prostitu|\bfuck|"
                       r"\bcoït|\bcoito|\bhure\b|\bnutte|fahişe|orospu|kokain|cocaïn|\bheroin\b|nigg|schwuchtel|\bpiç\b|\bbok\b|\bsik(?:mek|ik|tir)|"
                       r"kahpe|\bputa\b|putain|\bmerde|scheiße|scheisse|\bshit|\bbitch|\bwhore|\bcazzo|caralho|\bporra\b|\bcoño|\bcul\b|\barsch", re.I)
MAX_DEF, MAX_EX, MAX_TR = 170, 110, 44
NAMES = {"tr": "Türkçe", "en": "English", "de": "Deutsch", "fr": "Français", "es": "Español", "it": "Italiano", "pt": "Português"}
STOP = {"to", "a", "an", "the", "of", "someone", "something", "oneself", "s", "o"}
WORD_RE = re.compile(r"[^\W\d_]+", re.UNICODE)


def jl(cache, name):
    p = os.path.join(cache, name + ".jsonl.gz")
    if not os.path.exists(p):
        print("  (missing)", p, flush=True)
        return
    with gzip.open(p, "rt", encoding="utf-8") as f:
        for line in f:
            yield json.loads(line)


def low(s, lang):
    """lower case that keeps the length (Turkish dotted and dotless I)"""
    return s.replace("İ", "i").replace("I", "ı").lower() if lang == "tr" else s.lower()


def clean(s):
    s = unicodedata.normalize("NFC", s or "")
    s = s.replace("\t", " ").replace("|", "/").replace("\n", " ").replace("{", "(").replace("}", ")")
    return re.sub(r"\s+", " ", s).strip()


def clean_tr(s):
    """a translation as shown on a button: no notes in brackets, no trailing punctuation"""
    return re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", clean(s)).strip(" ,;:.!?")


def sense_bad(s):
    return bool(set(s.get("tags", [])) & BAD_TAGS) or bool(SENSITIVE.search(s.get("g", "") or s.get("w", "")))


def entry_bad(e):
    ss = e.get("senses") or []
    if not ss:
        return False
    bad = sum(1 for s in ss if set(s.get("tags", [])) & BAD_TAGS)
    return bool(set(ss[0].get("tags", [])) & BAD_TAGS) or bad * 2 >= len(ss)


def usable_sense(s):
    return s.get("g") and not set(s.get("tags", [])) & SKIP_SENSE_TAGS and not s.get("of") and not s.get("alt") and not sense_bad(s)


def mark(text, lemma, forms, lang):
    """marks the first token that is the lemma, or a form of it starting with it, as {lemma}rest; None if none"""
    ll = low(lemma, lang)
    for m in WORD_RE.finditer(text):
        tl = low(m.group(0), lang)
        if lang == "tr" and m.start() > 0 and text[m.start() - 1] in "'’":
            continue
        if tl == ll or (len(ll) >= 2 and tl.startswith(ll) and tl in forms):
            a, b = m.start(), m.start() + len(lemma)
            return text[:a] + "{" + text[a:b] + "}" + text[b:]
    return None


def mask_all(text, lemma, forms, lang):
    """marks every occurrence of the word (lemma or any form) so that a definition does not give it away"""
    keys = {low(lemma, lang)} | {f for f in forms if f and " " not in f}
    out, last = [], 0
    for m in WORD_RE.finditer(text):
        if low(m.group(0), lang) in keys:
            out += [text[last:m.start()], "{" + m.group(0) + "}"]
            last = m.end()
    out.append(text[last:])
    return "".join(out)


def cut(s, n):
    if len(s) <= n:
        return s
    s = s[:n]
    sp = s.rfind(" ")
    if sp > n * 2 // 3:
        s = s[:sp]
    return s.rstrip(" ,;:") + "…"


def sense_rank(i):
    m = re.match(r"(\d+)", str(i or ""))
    return int(m.group(1)) if m else 1


# ---------------------------------------------------------------- loading

class Lang:
    def __init__(self, code):
        self.code = code
        self.entries = collections.defaultdict(list)  # lower word -> entries of L's own edition (usable POS)
        self.en_entries = collections.defaultdict(list)  # lower word -> entries of en.wiktionary
        self.simple = collections.defaultdict(list)  # English only: Simple English Wiktionary
        self.form_of = collections.defaultdict(set)  # lower form -> lower lemmas
        self.forms = collections.defaultdict(set)  # lower lemma -> lower forms
        self.first_pos = {}  # lower word -> part of speech of its first entry in en.wiktionary (else own edition)
        self.freq = []
        self.rank = {}


def add_entry(L, e, table, first_pos=False):
    lw = low(e["w"], L.code)
    ss = e.get("senses", [])
    if first_pos and lw not in L.first_pos and e["w"][:1] == lw[:1]:
        L.first_pos[lw] = e["pos"]
    if ss and all(s.get("of") for s in ss):  # an inflected form: "ging" -> "gehen"
        for s in ss:
            for o in s["of"]:
                L.form_of[lw].add(low(o, L.code))
        return
    if not POS_MAP.get(e["pos"]):
        return
    table[lw].append(e)
    for fm in e.get("forms", []):
        if fm and " " not in fm:
            L.forms[lw].add(low(fm, L.code))
            L.form_of[low(fm, L.code)].add(lw)


def load_lang(cache, code):
    L = Lang(code)
    with open(os.path.join(cache, f"freq_{code}.txt"), encoding="utf-8") as f:
        for line in f:
            p = line.split()
            if len(p) == 2:
                L.freq.append((p[0], int(p[1])))
    for i, (w, _) in enumerate(L.freq):
        L.rank.setdefault(w, i)
    for e in jl(cache, f"native_{code}"):
        add_entry(L, e, L.entries, code == "en")
    if code != "en":
        for e in jl(cache, f"enwikt_{code}"):
            add_entry(L, e, L.en_entries, True)
    else:
        for e in jl(cache, "simple_en"):
            if POS_MAP.get(e["pos"]):
                L.simple[e["w"].lower()].append(e)
                for fm in e.get("forms", []):
                    if " " not in fm:
                        L.forms[e["w"].lower()].add(fm.lower())
    return L


def lemma_ranking(L):
    heads = set(L.entries) | set(L.en_entries)
    score = collections.Counter()
    for form, cnt in L.freq:
        if form in heads:
            score[form] += cnt
        else:
            lem = [x for x in L.form_of.get(form, ()) if x in heads]
            for x in lem:
                score[x] += cnt / len(lem)
    return [w for w, _ in score.most_common()]


# ---------------------------------------------------------------- translations

def own_translations(entries, target):
    out = []
    for e in entries:
        seen = set()
        for t in e.get("tr", []):
            if t["l"] != target or sense_bad(t):
                continue
            w = clean_tr(t["w"])
            if not w or w.lower() in seen or len(w) > MAX_TR:
                continue
            seen.add(w.lower())
            out.append((w, 10 - min(sense_rank(t.get("i")) - 1, 5) - 0.3 * len(seen)))
    return out


def english_glosses(entries):
    """short English glosses of en.wiktionary entries, best first"""
    out = []
    for e in entries:
        for k, s in enumerate(e.get("senses", [])):
            if not usable_sense(s):
                continue
            for part in re.split(r"[;,]\s*", re.sub(r"\([^)]*\)", "", s["g"])):
                part = clean_tr(part)
                if part and len(part) <= 28 and len(part.split()) <= 3 and (not part[:1].isupper() or part == "I"):
                    out.append((part, 9 - min(k, 6) - (0.5 if out else 0)))
    return out


def build_reverse(langs):
    """rev[L][lemma][N] -> Counter of (N word, part of speech) whose entries list the L lemma as a translation"""
    rev = {l: collections.defaultdict(lambda: collections.defaultdict(collections.Counter)) for l in LANGS}
    for n, Ln in langs.items():
        for es in Ln.entries.values():
            for e in es:
                if entry_bad(e):
                    continue
                for t in e.get("tr", []):
                    if t["l"] in rev and t["l"] != n and not sense_bad(t):
                        tw = low(clean_tr(t["w"]), t["l"])
                        if tw:
                            rev[t["l"]][tw][n][(e["w"], POS_MAP[e["pos"]])] += 1.0 / sense_rank(t.get("i"))
        if n != "en":  # en.wiktionary glosses link n words to English words
            for es in Ln.en_entries.values():
                for e in es:
                    if entry_bad(e):
                        continue
                    for k, s in enumerate(e.get("senses", [])[:3]):
                        if not usable_sense(s):
                            continue
                        for part in re.split(r"[;,]\s*", re.sub(r"\([^)]*\)", "", s["g"])):
                            part = clean_tr(part).lower()
                            if part.startswith("to "):
                                part = part[3:]
                            if part and len(part.split()) <= 2:
                                rev["en"][part][n][(e["w"], POS_MAP[e["pos"]])] += 0.6 / (k + 1)
    return rev


def candidates(langs, rev, L, lw, pos, own, ens, n):
    c = collections.Counter()
    for w, s in own_translations(own, n):
        c[w] += s
    if n == "en" and L.code != "en":
        for w, s in english_glosses(ens):
            c[w] += s
    for (w, p), k in rev[L.code].get(lw, {}).get(n, {}).items():
        if p == pos:  # a reverse link counts only between the same parts of speech
            c[w] += 4 + 2 * min(k, 2)
    if not c and L.code != "en" and n != "en":
        eng = [w for w, _ in sorted(english_glosses(ens), key=lambda x: -x[1])[:3]]
        eng += [w for w, _ in own_translations(own, "en")[:3] if w not in eng]
        piv = collections.Counter()
        for k, ew in enumerate(eng[:4]):
            key = ew.lower()
            key = key[3:] if key.startswith("to ") else key
            if key in STOP:
                continue
            for e in langs["en"].entries.get(key, []):
                if POS_MAP.get(e["pos"]) == pos and not entry_bad(e):
                    for w, _ in own_translations([e], n)[:3]:
                        piv[w] += 1.0 / (k + 1)
            for (w, p), x in rev["en"].get(key, {}).get(n, {}).items():
                if p == pos:
                    piv[w] += 0.5 / (k + 1)
        for w, s in piv.items():
            if s >= 1.2:
                c[w] += s
    return c


def expand(text, Ln, n):
    """the words of a sentence and the lemmas they are forms of"""
    ts = set(low(t, n) for t in WORD_RE.findall(text))
    for t in list(ts):
        ts |= Ln.form_of.get(t, set())
    return ts


def support(cand, toksets, Ln, n):
    """share of the aligned sentences (expanded word sets) in language n that contain the candidate"""
    if not toksets:
        return 0.0
    words = [low(x, n) for x in WORD_RE.findall(cand) if low(x, n) not in STOP]
    if not words:
        return 0.0
    return sum(1 for ts in toksets if all(w in ts for w in words)) / len(toksets)


def pick_translation(c, toksets, Ln, n, pos):
    if not c:
        return ""
    scored = collections.Counter()
    for w, s in c.items():
        scored[w] = s + 12 * support(w, toksets, Ln, n)
    best = scored.most_common()
    top = [best[0][0]]
    for w, s in best[1:4]:
        if len(top) == 2:
            break
        if len(toksets) >= 5 and support(w, toksets, Ln, n) < 0.05:
            continue  # a second meaning must show up in the sentences too
        if s >= best[0][1] * 0.6 and w.lower() not in (t.lower() for t in top) and len(", ".join(top + [w])) <= MAX_TR:
            top.append(w)
    if n == "en" and pos == "verb":
        top = [t if t.startswith("to ") or " " in t else "to " + t for t in top]
        top = list(dict.fromkeys(top))
    return ", ".join(top)


# ---------------------------------------------------------------- Tatoeba

def load_tatoeba(cache):
    sents = {}
    for l in LANGS:
        d = {}
        with open(os.path.join(cache, f"tatoeba_{l}.tsv"), encoding="utf-8") as f:
            for line in f:
                p = line.rstrip("\n").split("\t")
                if len(p) >= 3:
                    d[int(p[0])] = p[2]
        sents[l] = d
    links = {l: collections.defaultdict(dict) for l in LANGS}  # links[a][id][b] = shortest translation id in b
    for a in LANGS:
        for b in LANGS:
            if a < b:
                with open(os.path.join(cache, f"links_{a}_{b}.tsv"), encoding="utf-8") as f:
                    for line in f:
                        p = line.split()
                        if len(p) != 2:
                            continue
                        ia, ib = int(p[0]), int(p[1])
                        if ia in sents[a] and ib in sents[b]:
                            if b not in links[a][ia] or len(sents[b][ib]) < len(sents[b][links[a][ia][b]]):
                                links[a][ia][b] = ib
                            if a not in links[b][ib] or len(sents[a][ia]) < len(sents[a][links[b][ib][a]]):
                                links[b][ib][a] = ia
    return sents, links


def index_sentences(L, sents, links, wanted, bad_words):
    """lemma -> [(score, id)]: short sentences with translations that contain the lemma or a form starting with it"""
    idx = collections.defaultdict(list)
    first = collections.defaultdict(list)
    for lw in wanted:
        first[lw[:2]].append(lw)
    for sid, text in sents[L.code].items():
        if not (10 <= len(text) <= 80) or sid not in links[L.code] or re.search(r"\d|[\"“”«»]|https?:", text) or SENSITIVE.search(text):
            continue
        toks = [low(t, L.code) for t in WORD_RE.findall(text)]
        if not toks or len(toks) > 13 or any(t in bad_words for t in toks):
            continue
        rare = sum(1 for t in toks if L.rank.get(t, 99999) > 8000)
        score = len(links[L.code][sid]) * 10 - len(text) / 6 - rare * 8
        if L.code == "tr":  # suffixes after an apostrophe are not words
            toks = [low(t, "tr") for t in re.findall(r"(?<!['’])\b[^\W\d_]+", text)]
        hit = set()
        for t in toks:
            for lw in first.get(t[:2], ()):
                if lw not in hit and (t == lw or (t.startswith(lw) and t in L.forms.get(lw, ()))):
                    hit.add(lw)
                    idx[lw].append((score, sid))
    for lw in idx:
        idx[lw].sort(reverse=True)
        del idx[lw][40:]
    return idx


EXP = {}


def aligned_tokens(L, lw, sidx, sents, links, langs):
    out = collections.defaultdict(list)
    for _, sid in sidx.get(lw, []):
        for n, tid in links[L.code][sid].items():
            if (n, tid) not in EXP:
                EXP[(n, tid)] = expand(sents[n][tid], langs[n], n)
            out[n].append(EXP[(n, tid)])
    return out


# ---------------------------------------------------------------- per word

def pos_groups(L, lw, want_pos):
    """{pos: (own entries, en.wiktionary entries)}; None when the word as a whole is vulgar or offensive"""
    fp = L.first_pos.get(lw)
    if fp and not POS_MAP.get(fp) and fp not in ("name",):
        return None  # first of all a particle, suffix, abbreviation …
    own_all, en_all = L.entries.get(lw, []), L.en_entries.get(lw, [])
    own = [e for e in own_all if not entry_bad(e)]
    ens = [e for e in en_all if not entry_bad(e)]
    if (own_all and not own) or (en_all and not ens):
        return None
    g = collections.OrderedDict()
    for e in own + ens:
        p = POS_MAP[e["pos"]]
        if want_pos and p != want_pos:
            continue
        g.setdefault(p, ([], []))
        (g[p][0] if e in own else g[p][1]).append(e)
    return g


def weight(entries):
    return sum(1 for e in entries for s in e.get("senses", []) if usable_sense(s)) + sum(len(e.get("tr", [])) for e in entries) / 4


def definition(L, lw, word, pos, own):
    forms = L.forms.get(lw, set())
    srcs = [e for e in L.simple.get(lw, []) if POS_MAP.get(e["pos"]) == pos] if L.code == "en" else []
    for group in (srcs, own):
        best = None
        for e in group:
            trs = e.get("tr", [])
            for k, s in enumerate(e.get("senses", [])):
                if not usable_sense(s):
                    continue
                g = clean(s["g"])
                g = re.sub(r"^\(([^)]*)\)\s*", "", g) if len(g) > 40 else g
                g = g.strip(" ;:")
                if len(g) < 6 or re.search(r"approfondimento|citazioni|Wikipedia|Wiktionary|^\W*$|^[\"“„«'‚]", g, re.I) or low(g, L.code) == lw:
                    continue
                m = mask_all(cut(g, MAX_DEF), word, forms, L.code)
                if len(re.sub(r"\{[^}]*\}", "", m).strip(" ,.;:()")) < 5:
                    continue
                # senses with translations attached are the main ones
                n_tr = sum(1 for t in trs if (t.get("i") and str(t["i"]) == str(s.get("i"))) or (t.get("s") and t["s"][:20].lower() in g.lower()))
                key = (n_tr > 0, -k if group is srcs else n_tr, -k)
                if best is None or key > best[0]:
                    best = (key, m)
            if best and group is srcs:
                break
        if best:
            return best[1]
    return ""


def gender(entries):
    for e in entries:
        if e.get("art") in ("der", "die", "das"):
            return {"der": "m", "die": "f", "das": "n"}[e["art"]]
        tags = set(e.get("tags", []))
        for s in e.get("senses", [])[:2]:
            tags |= set(s.get("tags", []))
        if "masculine" in tags and "feminine" in tags:
            continue
        for t, g in (("masculine", "m"), ("feminine", "f"), ("neuter", "n")):
            if t in tags:
                return g
    return None


def article(lang, pos, g, word):
    if pos != "noun" or not g:
        return ""
    if lang == "de":
        return {"m": "der", "f": "die", "n": "das"}[g]
    if lang == "fr":
        return {"m": "un", "f": "une"}.get(g, "")  # indefinite: no elision to guess
    if lang == "es":
        if g == "f" and (word[:1].lower() in "aá" or word[:2].lower() in ("ha", "há")):
            return ""  # el agua, el hacha …: stress decides
        return {"m": "el", "f": "la"}.get(g, "")
    if lang == "pt":
        return {"m": "o", "f": "a"}.get(g, "")
    return ""


def wiki_example(L, word, lw, entries):
    for e in entries:
        for s in e.get("senses", []):
            if not usable_sense(s):
                continue
            for x in s.get("ex", []):
                t = clean(x["t"])
                if 12 <= len(t) <= MAX_EX and (not x.get("ref") or len(t) <= 70) and not SENSITIVE.search(t):
                    m = mark(t, word, L.forms.get(lw, set()), L.code)
                    if m:
                        tr = {"en": cut(clean(x["en"]), MAX_EX + 20)} if x.get("en") and L.code != "en" else {}
                        return m, tr
    return "", {}


def english_levels(cache):
    lv = {}
    for name in ("cefrj.csv", "octanove.csv"):
        with open(os.path.join(cache, name), encoding="utf-8") as f:
            for row in csv.DictReader(f):
                level = {"A1": 1, "A2": 2, "B1": 3, "B2": 4, "C1": 5, "C2": 6}.get(row["CEFR"].strip())
                pos = CEFRJ_POS.get(row["pos"].strip())
                if level and pos:
                    for hw in row["headword"].split("/"):
                        hw = hw.strip().lower()
                        if hw and re.fullmatch(r"[a-z][a-z']*", hw):
                            lv[(hw, pos)] = min(lv.get((hw, pos), 9), level)
    return lv


LEVEL_NOTE = {
    "en": "CEFR-J Wordlist 1.5 (A1–B2) and Octanove Vocabulary Profile C1/C2 1.0",
}


def write_sources(outdir, cache, stats):
    fetched = {}
    if os.path.exists(os.path.join(cache, "FETCHED")):
        with open(os.path.join(cache, "FETCHED"), encoding="utf-8") as f:
            for line in f:
                p = line.rstrip("\n").split("\t")
                if len(p) == 3:
                    fetched[p[0]] = (p[1], p[2])
    lines = [
        "Kelime Öğren — word list sources and licences",
        "",
        "The word lists (<lang>.txt) were built by tools/kelime/fetch.py and tools/kelime/build-words.py from the",
        "sources below. They are a derivative of Wiktionary and Tatoeba and are distributed under the Creative Commons",
        "Attribution-ShareAlike 4.0 licence (https://creativecommons.org/licenses/by-sa/4.0/); example sentences from",
        "Tatoeba are CC BY 2.0 FR (https://creativecommons.org/licenses/by/2.0/fr/), © their Tatoeba contributors.",
        "",
        "* Wiktionary (https://www.wiktionary.org), © Wiktionary contributors, CC BY-SA 4.0 and GFDL, as extracted by",
        "  Wiktextract (Tatu Ylonen, \"Wiktextract: Wiktionary as Machine-Readable Structured Data\", LREC 2022) and",
        "  published at https://kaikki.org: definitions, translations, parts of speech, genders, inflected forms and",
        "  usage examples, from the English, Simple English, German, French, Spanish, Italian, Portuguese and Turkish",
        "  editions. Texts were shortened and the word itself is marked {like this}.",
        "* Tatoeba (https://tatoeba.org), CC BY 2.0 FR: example sentences and their translations (per-language exports).",
        "* FrequencyWords by Hermit Dave (https://github.com/hermitdave/FrequencyWords), OpenSubtitles 2018 frequency",
        "  lists, content CC BY-SA 4.0: which words are taught and in which order.",
        "* CEFR-J Wordlist Version 1.5, compiled by Yukio Tono, Tokyo University of Foreign Studies",
        "  (http://www.cefr-j.org/download.html; via https://github.com/openlanguageprofiles/olp-en-cefrj), free for",
        "  research and commercial use with this citation: the A1–B2 levels of the English words.",
        "* Octanove Vocabulary Profile C1/C2 1.0, Octanove Labs, CC BY-SA 4.0 (same repository): English C1/C2 levels.",
        "",
        "LEVELS. English levels are the CEFR-J / Octanove levels. For Turkish, German, French, Spanish, Italian and",
        "Portuguese no open CEFR-graded list exists, so their levels are an APPROXIMATION from word frequency: the",
        f"most frequent {BANDS[0]} usable lemmas are A1, then up to rank {BANDS[1]} A2, {BANDS[2]} B1, {BANDS[3]} B2,",
        f"{BANDS[4]} C1 and {BANDS[5]} C2. Vulgar, offensive and derogatory words and senses were left out.",
        "",
        "Downloads:",
    ]
    for k in sorted(fetched):
        lines.append(f"  {k}: {fetched[k][0]} {fetched[k][1]}")
    lines += ["", "Contents (words per level A1..C2; definitions in the language itself; examples; with a blank; translations):"]
    for l, st in stats.items():
        lv = " ".join(str(st["levels"].get(str(i), st["levels"].get(i, 0))) for i in range(1, 7))
        tr = " ".join(f"{n}:{c}" for n, c in st["translations"].items())
        lines.append(f"  {l}.txt: {st['words']} words ({lv}); definitions {st['definitions']}; examples {st['examples']}"
                     f" ({st['cloze']} with a blank); translations {tr}")
    with open(os.path.join(outdir, "SOURCES"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cache = sys.argv[1]
    outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "apps", "kelime", "data")
    only = sys.argv[3].split(",") if len(sys.argv) > 3 else LANGS
    os.makedirs(outdir, exist_ok=True)
    langs = {}
    for l in LANGS:
        print("loading", l, flush=True)
        langs[l] = load_lang(cache, l)
    rev = build_reverse(langs)
    bad_words = set()
    for Ln in langs.values():
        for table in (Ln.entries, Ln.en_entries):
            for lw, es in table.items():
                if es and all(entry_bad(e) for e in es):
                    bad_words.add(lw)
    print(len(bad_words), "excluded words; loading Tatoeba", flush=True)
    sents, links = load_tatoeba(cache)
    en_lv = english_levels(cache)
    stats = {}
    for l in only:
        L = langs[l]
        ranking = lemma_ranking(L)
        if l == "en":
            pool = [lw for lw in ranking if any((lw, p) in en_lv for p in POS_PREF)]
        else:
            pool = ranking[: BANDS[-1] * 3]
        pool = [lw for lw in pool if lw not in bad_words and len(lw) >= 2 and WORD_RE.fullmatch(lw) and not SENSITIVE.search(lw)]
        sidx = index_sentences(L, sents, links, set(pool), bad_words)
        used_sent, rows, per_level = set(), [], collections.Counter()
        for lw in pool:
            level, want = None, None
            if l == "en":
                level, _, want = min((en_lv[(lw, p)], POS_PREF.index(p), p) for p in POS_PREF if (lw, p) in en_lv)
                if per_level[level] >= ENGLISH_MAX[level]:
                    continue
            groups = pos_groups(L, lw, want)
            if not groups:
                continue
            al = aligned_tokens(L, lw, sidx, sents, links, langs)
            best = None
            for pos, (own, ens) in groups.items():
                cands = {n: candidates(langs, rev, L, lw, pos, own, ens, n) for n in LANGS if n != l}
                sup = sum(max([support(w, al.get(n, []), langs[n], n) for w, _ in c.most_common(4)] or [0]) for n, c in cands.items())
                key = (round(sup * 2) / 2, POS_MAP.get(L.first_pos.get(lw, "")) == pos, weight(own) + weight(ens) / 2, -POS_PREF.index(pos))
                if best is None or key > best[0]:
                    best = (key, pos, own, ens, cands)
            _, pos, own, ens, cands = best
            word = next((e["w"] for e in own + ens if low(e["w"], l) == lw), lw)
            if l == "de" and pos == "noun":
                word = word[:1].upper() + word[1:]
            trs = {}
            for n, c in cands.items():
                t = pick_translation(c, al.get(n, []), langs[n], n, pos)
                if t and not SENSITIVE.search(t):
                    trs[n] = t
            defn = definition(L, lw, word, pos, own)
            if not trs or (not defn and len(trs) < 2):
                continue
            ex, extr = "", {}
            meant = {n: [w.strip() for w in t.split(",")] for n, t in trs.items()}

            def shows_meaning(sid):
                for n, tid in links[l][sid].items():
                    if n in meant:
                        ts = [EXP.get((n, tid)) or expand(sents[n][tid], langs[n], n)]
                        if any(support(w, ts, langs[n], n) > 0 for w in meant[n]):
                            return True
                return False

            for _, sid in sidx.get(lw, []):
                if sid in used_sent or not shows_meaning(sid):
                    continue
                m = mark(clean(sents[l][sid]), word, L.forms.get(lw, set()), l)
                if m:
                    ex = m
                    used_sent.add(sid)
                    extr = {n: cut(clean(sents[n][tid]), MAX_EX + 20) for n, tid in links[l][sid].items()}
                    break
            if not ex:
                ex, extr = wiki_example(L, word, lw, own + ens)
            if l != "en":
                k = len(rows)
                level = next((i + 1 for i, b in enumerate(BANDS) if k < b), None)
                if not level:
                    break
            art = article(l, pos, gender(own + ens), word)
            per_level[level] += 1
            rows.append((level, word, art, pos, defn, ex, "|".join(f"{n}={trs[n]}" for n in LANGS if n in trs),
                         "|".join(f"{n}={extr[n]}" for n in LANGS if n in extr and n != l)))
        rows.sort(key=lambda r: r[0])  # stable: by level, most frequent first
        path = os.path.join(outdir, f"{l}.txt")
        with open(path, "w", encoding="utf-8") as f:
            f.write(f"# Kelime Öğren word list: {NAMES[l]} ({l}) words={len(rows)} built={datetime.date.today().isoformat()}\n")
            f.write("# Sources: Wiktionary via kaikki.org (CC BY-SA 4.0), Tatoeba (CC BY 2.0 FR), FrequencyWords (CC BY-SA 4.0)"
                    + (", CEFR-J Wordlist 1.5 and Octanove C1/C2 (CC BY-SA 4.0)" if l == "en" else "; levels approximated from frequency")
                    + ". See SOURCES.\n")
            f.write("# word\tarticle\tpos\tlevel\tdefinition\texample\ttranslations\texample-translations\n")
            for r in rows:
                f.write("\t".join([r[1], r[2], r[3], str(r[0]), r[4], r[5], r[6], r[7]]) + "\n")
        stats[l] = {"levels": dict(sorted(per_level.items())), "words": len(rows), "definitions": sum(1 for r in rows if r[4]),
                    "cloze": sum(1 for r in rows if "{" in r[5]), "examples": sum(1 for r in rows if r[5]),
                    "translations": {n: sum(1 for r in rows if f"{n}=" in r[6]) for n in LANGS if n != l}, "bytes": os.path.getsize(path)}
        print(l, json.dumps(stats[l]), flush=True)
    with open(os.path.join(cache, "BUILD_STATS.json"), "w") as f:
        json.dump(stats, f, indent=1)
    if set(only) == set(LANGS):
        write_sources(outdir, cache, stats)


if __name__ == "__main__":
    main()
