#!/usr/bin/env python3
"""Downloads the Kelime sources into a cache directory, keeping only what tools/kelime/build-words.py needs.

The Wiktionary dumps are several GB each, so they are streamed and filtered on the fly: an entry is kept when its
headword (lower-cased) is among the FREQ_KEEP most frequent forms of its language, and only the fields used later
(part of speech, gender, inflected forms, glosses, examples, translations into the app's languages) are written to
<cache>/<name>.jsonl.gz. Everything else is downloaded as is.

Sources (all fetched over HTTPS; the dates of the downloads are written to <cache>/FETCHED):
  * Wiktextract extracts of Wiktionary by kaikki.org (Tatu Ylonen), CC BY-SA 4.0 / GFDL like Wiktionary itself:
      - each language's own Wiktionary edition (de, fr, es, it, pt, tr): monolingual definitions, examples,
        translation tables, genders and inflected forms
      - Simple English Wiktionary: learner-friendly English definitions
      - English Wiktionary: English entries (translation tables, definitions, examples) and, for every other
        language, its entries with English glosses (examples with English translations, "form of" links)
  * FrequencyWords by Hermit Dave (OpenSubtitles 2018 frequency lists), content CC BY-SA 4.0
  * CEFR-J Wordlist 1.5 (Tono Laboratory, TUFS; free for research and commercial use with citation) and the
    Octanove Vocabulary Profile C1/C2 1.0 (CC BY-SA 4.0), via openlanguageprofiles/olp-en-cefrj
  * Tatoeba sentences and translation links (CC BY 2.0 FR), per-language exports
  * pronunciations: the IPA transcriptions ("sounds") of every language's words in English Wiktionary (ipa_<lang>)

Usage: tools/kelime/fetch.py <cache dir> [name ...]   (no names: everything; names are listed by --list)
"""
import bz2, datetime, gzip, io, json, os, sys, urllib.parse, urllib.request

LANGS = ["tr", "en", "de", "fr", "es", "it", "pt"]
FREQ_KEEP = 40000
UA = "kindle-apps-kelime/1.0 (https://github.com/dikeckaan/kindle-apps; tools/kelime/fetch.py)"
FREQ_URL = "https://raw.githubusercontent.com/hermitdave/FrequencyWords/master/content/2018/{0}/{0}_50k.txt"
CEFRJ_URL = "https://raw.githubusercontent.com/openlanguageprofiles/olp-en-cefrj/master/cefrj-vocabulary-profile-1.5.csv"
OCTANOVE_URL = "https://raw.githubusercontent.com/openlanguageprofiles/olp-en-cefrj/master/octanove-vocabulary-profile-c1c2-1.0.csv"
NATIVE = {  # language -> (edition, language name in that edition)
    "de": ("dewiktionary", "Deutsch"), "fr": ("frwiktionary", "Français"), "es": ("eswiktionary", "Español"),
    "it": ("itwiktionary", "Italiano"), "pt": ("ptwiktionary", "Português"), "tr": ("trwiktionary", "Türkçe"),
}
ENGLISH_NAMES = {"tr": "Turkish", "de": "German", "fr": "French", "es": "Spanish", "it": "Italian", "pt": "Portuguese", "en": "English"}
TATOEBA = {"tr": "tur", "en": "eng", "de": "deu", "fr": "fra", "es": "spa", "it": "ita", "pt": "por"}


def kaikki_url(edition, name):
    q = urllib.parse.quote(name)
    base = "https://kaikki.org/dictionary" if edition == "enwiktionary" else "https://kaikki.org/" + edition
    return f"{base}/{q}/kaikki.org-dictionary-{q}.jsonl"


def open_url(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=120)


def freq_words(cache, lang):
    out = set()
    with open(os.path.join(cache, f"freq_{lang}.txt"), encoding="utf-8") as f:
        for i, line in enumerate(f):
            if i >= FREQ_KEEP:
                break
            out.add(line.split(" ")[0])
    return out


def slim(d, keep_langs):
    """The fields build-words.py reads, from one wiktextract entry."""
    e = {"w": d.get("word", ""), "pos": d.get("pos", ""), "tags": d.get("tags", [])}
    if d.get("pos_title"):
        e["pt"] = d["pos_title"]
    forms = []
    for f in d.get("forms", [])[:120]:
        fm = f.get("form")
        if fm and fm not in forms and not any(t in ("table-tags", "inflection-template") for t in f.get("tags", [])):
            forms.append(fm)
            if f.get("article") and "art" not in e and "nominative" in f.get("tags", []) and "singular" in f.get("tags", []):
                e["art"] = f["article"]
    if forms:
        e["forms"] = forms[:80]
    senses = []
    for s in d.get("senses", [])[:8]:
        o = {"g": " ".join(s.get("glosses", [])[-1:]) if s.get("glosses") else ""}
        if s.get("tags"):
            o["tags"] = s["tags"]
        if s.get("topics"):
            o["topics"] = s["topics"]
        if s.get("sense_index"):
            o["i"] = s["sense_index"]
        if s.get("form_of"):
            o["of"] = [x.get("word", "") for x in s["form_of"]]
        if s.get("alt_of"):
            o["alt"] = [x.get("word", "") for x in s["alt_of"]]
        exs = []
        for x in s.get("examples", [])[:4]:
            t = x.get("text", "")
            if not t:
                continue
            xo = {"t": t}
            tr = x.get("translation") or x.get("english")
            if tr:
                xo["en"] = tr
            if x.get("ref"):
                xo["ref"] = 1
            exs.append(xo)
        if exs:
            o["ex"] = exs
        senses.append(o)
    e["senses"] = senses
    trs = []
    for t in d.get("translations", []):
        lc = t.get("lang_code") or t.get("code")
        if lc in keep_langs and t.get("word"):
            o = {"l": lc, "w": t["word"]}
            if t.get("sense_index"):
                o["i"] = t["sense_index"]
            if t.get("sense"):
                o["s"] = t["sense"][:80]
            if t.get("tags"):
                o["tags"] = t["tags"]
            trs.append(o)
    if trs:
        e["tr"] = trs
    return e


def slim_ipa(d, keep_langs=None):
    """headword, part of speech and up to four IPA transcriptions (with their accent tags); None without any"""
    ipa = []
    for snd in d.get("sounds", []):
        t = snd.get("ipa")
        tags = [x for x in snd.get("tags", []) if isinstance(x, str)][:4]
        if t and len(t) <= 60 and [t, tags] not in ipa:
            ipa.append([t, tags])
    return {"w": d.get("word", ""), "pos": d.get("pos", ""), "ipa": ipa[:4]} if ipa else None


def fetch_kaikki(cache, name, url, lang, keep_langs, want_lang_code=None, slimmer=slim):
    words = freq_words(cache, lang)
    out_path = os.path.join(cache, name + ".jsonl.gz")
    n_in = n_out = 0
    with open_url(url) as r, gzip.open(out_path + ".part", "wt", encoding="utf-8") as out:
        for raw in io.TextIOWrapper(r, encoding="utf-8"):
            n_in += 1
            try:
                d = json.loads(raw)
            except ValueError:
                continue
            if want_lang_code and d.get("lang_code") != want_lang_code:
                continue
            w = d.get("word", "")
            if w.lower() not in words:
                continue
            e = slimmer(d, keep_langs)
            if e is None:
                continue
            out.write(json.dumps(e, ensure_ascii=False) + "\n")
            n_out += 1
            if n_out % 20000 == 0:
                print(f"{name}: {n_in} read, {n_out} kept", flush=True)
    os.replace(out_path + ".part", out_path)
    return f"{url} ({n_in} entries read, {n_out} kept)"


def fetch_plain(cache, name, url, decompress=False):
    with open_url(url) as r:
        data = r.read()
    if decompress:
        data = bz2.decompress(data)
    with open(os.path.join(cache, name), "wb") as f:
        f.write(data)
    return f"{url} ({len(data)} bytes)"


def jobs():
    j = {}
    for l in LANGS:
        j[f"freq_{l}"] = lambda c, l=l: fetch_plain(c, f"freq_{l}.txt", FREQ_URL.format(l))
    j["cefrj"] = lambda c: fetch_plain(c, "cefrj.csv", CEFRJ_URL)
    j["octanove"] = lambda c: fetch_plain(c, "octanove.csv", OCTANOVE_URL)
    for l, (ed, nm) in NATIVE.items():
        j[f"native_{l}"] = lambda c, l=l, ed=ed, nm=nm: fetch_kaikki(c, f"native_{l}", kaikki_url(ed, nm), l, set(LANGS), l)
    j["native_en"] = lambda c: fetch_kaikki(c, "native_en", kaikki_url("enwiktionary", "English"), "en", set(LANGS), "en")
    j["simple_en"] = lambda c: fetch_kaikki(c, "simple_en", kaikki_url("simplewiktionary", "English"), "en", set(LANGS))
    for l in LANGS:
        if l != "en":
            j[f"enwikt_{l}"] = lambda c, l=l: fetch_kaikki(c, f"enwikt_{l}", kaikki_url("enwiktionary", ENGLISH_NAMES[l]), l, set(LANGS), l)
    for l in LANGS:
        j[f"ipa_{l}"] = lambda c, l=l: fetch_kaikki(c, f"ipa_{l}", kaikki_url("enwiktionary", ENGLISH_NAMES[l]), l, set(LANGS), l, slim_ipa)
    for l in LANGS:
        t = TATOEBA[l]
        j[f"tatoeba_{l}"] = lambda c, t=t, l=l: fetch_plain(
            c, f"tatoeba_{l}.tsv", f"https://downloads.tatoeba.org/exports/per_language/{t}/{t}_sentences.tsv.bz2", True)
    for a in LANGS:
        for b in LANGS:
            if a < b:
                ta, tb = TATOEBA[a], TATOEBA[b]
                j[f"links_{a}_{b}"] = lambda c, ta=ta, tb=tb, a=a, b=b: fetch_plain(
                    c, f"links_{a}_{b}.tsv", f"https://downloads.tatoeba.org/exports/per_language/{ta}/{ta}-{tb}_links.tsv.bz2", True)
    return j


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == "--list":
        print(" ".join(jobs()))
        return
    cache = sys.argv[1]
    os.makedirs(cache, exist_ok=True)
    all_jobs = jobs()
    names = sys.argv[2:] or list(all_jobs)
    # frequency lists first: the Wiktionary filters need them
    names.sort(key=lambda n: (not n.startswith("freq_"), n))
    for n in names:
        if n.startswith(("native_", "simple_", "enwikt_")):
            lang = n.split("_")[1]
            if not os.path.exists(os.path.join(cache, f"freq_{lang}.txt")):
                all_jobs[f"freq_{lang}"](cache)
        note = all_jobs[n](cache)
        with open(os.path.join(cache, "FETCHED"), "a", encoding="utf-8") as f:
            f.write(f"{n}\t{datetime.date.today().isoformat()}\t{note}\n")
        print(n, "ok:", note, flush=True)


if __name__ == "__main__":
    main()
