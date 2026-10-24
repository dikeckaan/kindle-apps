"""Shared by build-recipes.py (the bundled offline set) and build-shards.py (the online recipe library): polite
MediaWiki API access with an on-disk cache, and the wikitext → recipe conversion (the app's tarif_wiki_record does
the same for pages fetched on the device).

Recipe text format (UTF-8), one recipe per "@R" line, then its lines:
  @R<TAB>lang<TAB>title<TAB>cuisine (Turkish country name)<TAB>tags<TAB>servings (0 = unknown)<TAB>url<TAB>licence
  =intro sentence(s)        (optional)
  +ingredient group heading
  -ingredient
  *step
"""
import hashlib, html, json, os, re, sys, time, urllib.error, urllib.parse, urllib.request

UA = "Tarifler/1.0 (https://kaandikec.com/kindle-apps/; ai@kaandikec.com)"
TR, EN, DE = "tr.wikibooks.org", "en.wikibooks.org", "de.wikibooks.org"
LICENCE = "CC BY-SA 4.0"
CACHE = None  # directory for API answers (keyed by the request URL), None = no cache

# ---- MediaWiki API: one request at a time, maxlag respected, retried with backoff ----

def _fetch(url):
    delay = 5
    for attempt in range(8):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA, "Accept-Encoding": "identity"}), timeout=120) as r:
                data = json.load(r)
            err = data.get("error", {}) if isinstance(data, dict) else {}
            if err.get("code") == "maxlag":  # the servers are lagging: wait as asked and try again
                time.sleep(max(delay, 5))
                delay = min(delay * 2, 120)
                continue
            if err:
                raise RuntimeError(f"API error {err.get('code')}: {err.get('info')}")
            return data
        except urllib.error.HTTPError as e:
            if e.code not in (429, 500, 502, 503, 504):
                raise
            wait = e.headers.get("Retry-After")
            time.sleep(int(wait) if wait and wait.isdigit() else delay)
            delay = min(delay * 2, 120)
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            time.sleep(delay)
            delay = min(delay * 2, 120)
    raise RuntimeError("giving up on " + url)


def api_url(host, params):
    return f"https://{host}/w/api.php?" + urllib.parse.urlencode(dict(params, format="json", formatversion="2"))


def get(host, params, cache=True):
    """one API answer; cached by URL (without maxlag) when CACHE is set and cache is True"""
    url = api_url(host, params)
    p = None
    if CACHE and cache:
        p = os.path.join(CACHE, hashlib.sha1(url.encode()).hexdigest() + ".json")
        if os.path.exists(p):
            with open(p, encoding="utf-8") as f: return json.load(f)
    data = _fetch(url + "&maxlag=5")
    if p:
        with open(p + ".tmp", "w", encoding="utf-8") as f: json.dump(data, f)
        os.replace(p + ".tmp", p)
    return data


def query_all(host, params, cache=True):
    """every page of a continued query (generator or list), merged: yields each answer"""
    cont = {}
    while True:
        d = get(host, dict(params, **cont), cache)
        yield d
        if "continue" not in d: return
        cont = d["continue"]


def all_pages(host, ns):
    out = []
    for d in query_all(host, dict(action="query", list="allpages", apnamespace=ns, aplimit=500, apfilterredir="nonredirects")):
        out += [p["title"] for p in d["query"]["allpages"]]
    return out


def members(host, cat):
    out = []
    for d in query_all(host, dict(action="query", list="categorymembers", cmtitle=cat, cmlimit=500, cmnamespace=102 if host == EN else 100)):
        out += [m["title"] for m in d["query"]["categorymembers"]]
    return out


def contents(host, titles):
    """title → (revid, wikitext) for existing pages, 40 titles per request"""
    res = {}
    for i in range(0, len(titles), 40):
        d = get(host, dict(action="query", prop="revisions", rvprop="ids|content", rvslots="main", titles="|".join(titles[i:i + 40])))
        for p in d["query"]["pages"]:
            if "revisions" in p:
                rv = p["revisions"][0]
                res[p["title"]] = (rv["revid"], rv["slots"]["main"]["content"])
    return res


# ---- wikitext cleaning ----

def strip_templates(s):
    out, depth, i = [], 0, 0
    while i < len(s):
        if s.startswith("{{", i): depth += 1; i += 2; continue
        if s.startswith("}}", i) and depth: depth -= 1; i += 2; continue
        if not depth: out.append(s[i])
        i += 1
    return "".join(out)


def clean(s):
    s = re.sub(r"<ref[^>/]*/>", "", s)
    s = re.sub(r"<ref[^>]*>.*?</ref>", "", s, flags=re.S)
    s = re.sub(r"<!--.*?-->", "", s, flags=re.S)
    s = re.sub(r"\[\[(?:File|Image|Dosya|Resim|Datei|Bild|Category|Kategori|Kategorie):[^\[\]]*(?:\[\[[^\]]*\]\][^\[\]]*)*\]\]", "", s, flags=re.I)
    s = re.sub(r"\[\[[^\]|]*\|([^\]]*)\]\]", r"\1", s)
    s = re.sub(r"\[\[([^\]]*)\]\]", lambda m: m.group(1).split(":")[-1], s)
    s = re.sub(r"\[https?://\S+ ([^\]]*)\]", r"\1", s)
    s = re.sub(r"\[https?://\S+\]", "", s)
    s = re.sub(r"'{2,}", "", s)
    s = re.sub(r"__[A-Za-z]+__", "", s)
    s = re.sub(r"<br\s*/?>", " ", s)
    s = re.sub(r"<[^>]+>", "", s)
    s = html.unescape(s).replace(" ", " ").replace("\t", " ")
    return re.sub(r"\s+", " ", s).strip()


ING_RE = re.compile(r"malzeme|gerekli|ihtiya|çindekiler|ingredient|zutat", re.I)
STEP_RE = re.compile(r"hazırlan|yapılış|yapımı|tarif|pişir|procedure|direction|method|preparation|instruction|steps|zubereitung|anleitung", re.I)
BOLD_ONLY = re.compile(r"^'{3,}[^']{1,60}'{3,}:?$")


def parse(text):
    """→ (intro, [('+'|'-', text)], [steps], servings) from a recipe page's wikitext"""
    text = re.sub(r"\{\|.*?\|\}", "", text, flags=re.S)  # tables
    serv = 0
    m = (re.search(r"servings\s*=\s*(\d+)", text) or re.search(r"(\d+)\s*kişilik", text) or re.search(r"[Ss]erves (\d+)", text)
         or re.search(r"Portionen\s*=\s*(\d+)", text))
    if m and 0 < int(m.group(1)) <= 30: serv = int(m.group(1))
    text = strip_templates(text)
    intro, ings, steps, sect = [], [], [], "intro"
    plain = gap = False  # plain: the section began at a "Malzeme:" line, not a heading; gap: a blank line since
    for line in text.split("\n"):
        h = re.match(r"^(={2,})\s*(.*?)\s*\1\s*$", line.strip())
        if h:
            title, level = clean(h.group(2)), len(h.group(1))
            if ING_RE.search(title) and (level == 2 or sect != "ing"): sect = "ing"
            elif STEP_RE.search(title) and (level == 2 or sect != "step"): sect = "step"
            elif sect == "ing" and level > 2: ings.append(("+", title.rstrip(":")))
            elif level == 2 or sect != "ing": sect = "other"
            plain = False
            continue
        raw = line.strip()
        if not raw:
            gap = True
            continue
        bullet = re.match(r"^([*#:;]+)\s*(.*)$", raw)
        body = clean(bullet.group(2) if bullet else raw)
        if not body: continue
        if not bullet and len(body) < 40:  # pages without headings: "Malzeme:" / "Yapılışı:" label lines (as tarif_wiki_record)
            label = body.rstrip(": ")
            if len(label.split()) <= 3 and sect != "ing" and ING_RE.search(label):
                sect, plain, gap = "ing", True, False
                continue
            if len(label.split()) <= 3 and (body.endswith(":") or raw.startswith("'''")) and STEP_RE.search(label):
                sect = "step"
                continue
        # after the ingredient list and a blank line, a paragraph of sentences is the method (pages without its heading)
        if (sect == "ing" and gap and not bullet and sum(k == "-" for k, _ in ings) >= 2 and len(body) >= 80
                and (". " in body or (plain and body.endswith(".")))):
            sect = "step"
        gap = False
        if sect == "intro" and (body.startswith(("<", "|")) or "listesine dön" in body): continue
        if sect == "intro" and not bullet and len(" ".join(intro)) < 400: intro.append(body)
        elif sect == "ing":
            if bullet: ings.append(("-", body))
            elif re.match(r"^\d+\s*kişilik:?$|^serves", body, re.I): continue
            elif body.endswith(":") and len(body) < 60: ings.append(("+", body.rstrip(": ")))
            else: ings += [("-", p.strip()) for p in re.split(r",\s*(?![^()]*\))", body) if p.strip()]
        elif sect == "step":
            if not bullet and BOLD_ONLY.match(raw): continue  # "'''Teig'''": a part name, not a step
            if bullet: steps.append(body)
            else: steps += split_paragraph(body)
    ings = [(k, v.rstrip(",;. ")[:300]) for k, v in ings if len(v.rstrip(",;. ")) > 1]
    return " ".join(intro)[:500], ings, [s[:1200] for s in steps if len(s) > 2], serv


def split_paragraph(p):
    """a long method paragraph → steps of about two sentences (one per screen in cooking mode)"""
    if len(p) < 320: return [p]
    sents = re.split(r"(?<=[.!?])\s+(?=[A-ZÇĞİÖŞÜÄ0-9])", p)
    out, cur = [], ""
    for s in sents:
        if cur and len(cur) + len(s) > 280: out.append(cur); cur = s
        else: cur = (cur + " " + s).strip()
    if cur: out.append(cur)
    return out


def tr_lower(s):
    return s.replace("İ", "i").replace("I", "ı").lower()


_FOLD = str.maketrans("çğıöşüâîûéèêëáàäãåíìïóòõøúùñ", "cgiosuaiueeeeaaaaaiiioooouun")


def fold(s):
    """lower case by Turkish rules, accents folded to ASCII, runs of other characters → one space (like tarif_fold)"""
    s = tr_lower(s).replace("i̇", "i").replace("ß", "ss").translate(_FOLD)
    return re.sub(r"[^a-z0-9]+", " ", s).strip()


# ---- filters and tags ----

MEAT = ("et ", "eti", "kıyma", "kuşbaşı", "tavuk", "piliç", "hindi", "kuzu", "dana", "sucuk", "pastırma", "salam", "sosis", "ciğer",
        "balık", "balığ", "levrek", "hamsi", "somon", "ton ", "karides", "midye", "kalamar", "işkembe", "paça", "dil ", "beyin", "et suyu", "tavuk suyu",
        "jelatin", "beef", "pork", "chicken", "lamb", "mutton", "veal", "bacon", "ham", "sausage", "fish", "shrimp", "prawn", "anchov",
        "tuna", "salmon", "turkey", "duck", "goat meat", "meat", "stock", "broth", "gelatin", "lard", "oyster", "crab", "squid", "mussel")
ANIMAL = ("süt", "yoğurt", "peynir", "tereyağ", "krema", "kaymak", "yumurta", "bal", "lor", "kaşar", "beyaz peynir", "milk", "yogurt",
          "yoghurt", "cheese", "butter", "cream", "egg", "honey", "ghee", "paneer", "mayonnaise")
SKIP_CATS = ("alcoholic", "cocktail", "kokteyl", "pet recipes", "disambiguation", "alkollü", "alkoholische", "cocktails")
ALCOHOL = ("aperol", "prosecco", "votka", "vodka", " rom ", " rum", " cin ", " gin", "şarap", "wine", "bira", "beer", "likör", "liqueur",
           "rakı", "whisk", "viski", "tekila", "tequila", "brandy", "konyak", "cognac", "champagne", "şampanya", "vermut", "vermouth",
           "bourbon", "sake", "kahlua", "cointreau", "triple sec", "sherry", "port wine", "marsala", "cider")
DE_ALCOHOL = re.compile(r"\b(wein|weißwein|rotwein|weisswein|bier|rum|schnaps|likör|sekt|weinbrand|kirschwasser|cognac|wodka|grappa|"
                        r"portwein|sherry|amaretto|calvados|marsala|obstler|korn|whisky|whiskey|gin|eierlikör|cidre|madeira|champagner)\b", re.I)


def has_alcohol(ings, lang="tr"):
    words = " " + " ".join(tr_lower(v) for k, v in ings if k == "-") + " "
    if lang == "de": return bool(DE_ALCOHOL.search(words))
    return any(w in words for w in ALCOHOL)


def diet_tags(ings, cats_tags, steps):
    words = " " + " ".join(tr_lower(v) for k, v in ings if k == "-") + " "
    t = ""
    meat = any(w in words or w in " " + tr_lower(" ".join(steps)) for w in MEAT)
    if "v" in cats_tags or (not meat and not any(re.search(r"(?<![a-zçğıöşü])" + re.escape(w), words) for w in ANIMAL)): t += "vg"
    elif "g" in cats_tags or not meat: t += "g"
    return t


def tags_from(cats, table):
    t = ""
    for c in cats:
        for key, tag in table:
            if key.lower() in c.lower() and tag not in t: t += tag
    return t


def categories(text):
    return [c.strip() for c in re.findall(r"\[\[\s*(?:Category|Kategori|Kategorie)\s*:\s*([^\]|]+)", text, flags=re.I)]


def sort_tags(tags):
    return "".join(sorted(set(tags), key="catshkivg".index))


def record(lang, title, cuisine, tags, serv, url, intro, ings, steps):
    out = [f"@R\t{lang}\t{title}\t{cuisine}\t{sort_tags(tags)}\t{serv}\t{url}\t{LICENCE}"]
    if intro: out.append("=" + intro)
    out += [k + v for k, v in ings]
    out += ["*" + s for s in steps]
    return "\n".join(out)


def page_url(host, title):
    return f"https://{host}/wiki/" + urllib.parse.quote(title.replace(" ", "_"), safe=":/(),'")


def set_cache(path):
    global CACHE
    CACHE = path
    if path: os.makedirs(path, exist_ok=True)


def log(*a):
    print(*a, file=sys.stderr, flush=True)
