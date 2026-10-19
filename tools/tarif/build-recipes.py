#!/usr/bin/env python3
"""Builds the offline recipe set of Tarifler (apps/tarif/data/recipes.txt + SOURCES) from Wikibooks.

Sources (both CC BY-SA 4.0, text by the Wikibooks contributors; every recipe keeps its page URL and revision):
  * Turkish Wikibooks (Vikikitap), the "Yemek:" namespace (the Turkish cookbook "Yemek Kitabı") — Turkish text,
    Turkish and world dishes;
  * English Wikibooks Cookbook ("Cookbook:" namespace) — English text: every Turkish recipe plus up to PER_CUISINE
    recipes for each country cuisine in CUISINES.
Wiki markup is removed (links keep their label, templates, references, files and tables are dropped); ingredient
lists and steps are taken from the page's ingredient / method sections. Pages without both an ingredient list and
at least one step, index pages, drinks with alcohol and pet food are skipped. Tags (soup, main, dessert, …) come
from the page categories; vegan / vegetarian also from the ingredient words when the page does not say.

Output format (UTF-8), one recipe per "@R" line, then its lines:
  @R<TAB>lang<TAB>title<TAB>cuisine (Turkish country name)<TAB>tags<TAB>servings (0 = unknown)<TAB>url<TAB>licence
  =intro sentence(s)        (optional)
  +ingredient group heading
  -ingredient
  *step

Usage: tools/tarif/build-recipes.py [outdir] [--cache DIR]   (needs network; --cache keeps the API answers)
"""
import hashlib, html, json, os, re, sys, urllib.parse, urllib.request

UA = "kindle-apps-tarif/1.0 (https://github.com/dikeckaan/kindle-apps; build-recipes.py)"
TR, EN = "tr.wikibooks.org", "en.wikibooks.org"
LICENCE = "CC BY-SA 4.0"
PER_CUISINE = 10
CACHE = None

# English Wikibooks cuisine categories → the cuisine's Turkish name in the app
CUISINES = {
    "Turkish": "Türkiye", "Italian": "İtalya", "French": "Fransa", "Indian": "Hindistan", "Chinese": "Çin", "Japanese": "Japonya",
    "Mexican": "Meksika", "Thai": "Tayland", "Greek": "Yunanistan", "Spanish": "İspanya", "German": "Almanya", "Austrian": "Avusturya",
    "Egyptian": "Mısır", "Ethiopian": "Etiyopya", "Nigerian": "Nijerya", "Moroccan": "Fas", "Algerian": "Cezayir", "Lebanese": "Lübnan",
    "Iranian": "İran", "Israeli": "İsrail", "Palestinian": "Filistin", "Afghan": "Afganistan", "Armenian": "Ermenistan",
    "Azerbaijani": "Azerbaycan", "Georgian": "Gürcistan", "Korean": "Kore", "Vietnamese": "Vietnam", "Indonesian": "Endonezya",
    "Filipino": "Filipinler", "Malaysian": "Malezya", "Pakistani": "Pakistan", "Nepali": "Nepal", "Bengali": "Bangladeş",
    "Russian": "Rusya", "Ukrainian": "Ukrayna", "Polish": "Polonya", "Hungarian": "Macaristan", "Bulgarian": "Bulgaristan",
    "Romanian": "Romanya", "Albanian": "Arnavutluk", "Swedish": "İsveç", "Norwegian": "Norveç", "Danish": "Danimarka",
    "Finnish": "Finlandiya", "Dutch": "Hollanda", "Belgian": "Belçika", "Swiss": "İsviçre", "Portuguese": "Portekiz",
    "English": "İngiltere", "Scottish": "İskoçya", "Irish": "İrlanda", "Welsh": "Galler", "United States": "ABD", "Canadian": "Kanada",
    "Brazilian": "Brezilya", "Argentine": "Arjantin", "Peruvian": "Peru", "Colombian": "Kolombiya", "Venezuelan": "Venezuela",
    "Cuban": "Küba", "Jamaican": "Jamaika", "Puerto Rican": "Porto Riko", "Australian": "Avustralya", "New Zealand": "Yeni Zelanda",
    "Kenyan": "Kenya", "South African": "Güney Afrika", "Libyan": "Libya", "Rwandan": "Ruanda", "Ugandan": "Uganda",
    "Cameroonian": "Kamerun", "Gambian": "Gambiya", "Liberian": "Liberya", "Zambian": "Zambiya", "Senegalese": "Senegal",
    "Czech": "Çekya", "Cypriot": "Kıbrıs",
}

# Turkish Wikibooks: "<X> Mutfağı" categories are taken as they are; titles of foreign dishes without one
TR_FOREIGN = {
    "Fettuccine Alfredo": "İtalya", "Fish and chips": "İngiltere", "BLT Sandviç": "ABD", "Kulüp sandviç": "ABD", "Hint Omleti": "Hindistan",
    "Mafaldine, Kuşkonmaz Soslu": "İtalya", "Cazuela de Mariscos": "Kolombiya", "Bandeja Paisa": "Kolombiya", "Arepa": "Kolombiya",
    "Basmati pirinç": "Hindistan",
}
TR_CUISINE_FIX = {"Türk": "Türkiye", "Türkiye": "Türkiye", "Amerikan": "ABD", "Amerikan (ABD)": "ABD", "Fransız": "Fransa", "İtalyan": "İtalya",
                  "İspanyol": "İspanya", "Hırvat": "Hırvatistan", "Eston": "Estonya", "Japonya": "Japonya", "Yunan": "Yunanistan",
                  "Japon": "Japonya", "Alman": "Almanya", "Rus": "Rusya", "Macar": "Macaristan", "Arap": "Arabistan"}

# tags: c soup, a main dish, t dessert, s salad / meze, h pastry / bread, k breakfast, i drink, v vegan, g vegetarian
EN_TAGS = [("Recipes for soup", "c"), ("Soup recipes", "c"), ("Recipes for stew", "a"), ("Main course recipes", "a"), ("Dinner recipes", "a"),
           ("Recipes for dessert", "t"), ("Dessert recipes", "t"), ("Recipes for cake", "t"), ("Recipes for cookies", "t"),
           ("Recipes for pudding", "t"), ("Recipes for confections", "t"), ("Recipes for salad", "s"), ("Appetizer recipes", "s"),
           ("Recipes for dip", "s"), ("Recipes for bread", "h"), ("Recipes for pastries", "h"), ("Recipes for dough", "h"),
           ("Breakfast recipes", "k"), ("Recipes for beverages", "i"), ("Vegan recipes", "v"), ("Vegetarian recipes", "g")]
TR_TAGS = [("çorba", "c"), ("et yemek", "a"), ("tavuk", "a"), ("balık", "a"), ("ana yemek", "a"), ("sebze yemek", "a"), ("pilav", "a"),
           ("makarna", "a"), ("köfte", "a"), ("kebap", "a"), ("dolma", "a"), ("tatlı", "t"), ("kurabiye", "t"), ("kek", "t"), ("pasta", "t"),
           ("reçel", "t"), ("marmelat", "t"), ("helva", "t"), ("dondurma", "t"), ("salata", "s"), ("meze", "s"), ("börek", "h"),
           ("hamur", "h"), ("ekmek", "h"), ("kahvaltı", "k"), ("hoşaf", "i"), ("içecek", "i"), ("sandviç", "k"), ("tost", "k"), ("omlet", "k"), ("menemen", "k"), ("cacık", "s"), ("humus", "s"), ("ezme", "s"), ("piyaz", "s"),
           ("kısır", "s"), ("turşu", "s"), ("vegan", "v"),
           ("vejetaryen", "g")]
SKIP_CATS = ("alcoholic", "cocktail", "kokteyl", "pet recipes", "disambiguation", "alkollü")
MEAT = ("et ", "eti", "kıyma", "kuşbaşı", "tavuk", "piliç", "hindi", "kuzu", "dana", "sucuk", "pastırma", "salam", "sosis", "ciğer",
        "balık", "balığ", "levrek", "hamsi", "somon", "ton ", "karides", "midye", "kalamar", "işkembe", "paça", "dil ", "beyin", "et suyu", "tavuk suyu",
        "jelatin", "beef", "pork", "chicken", "lamb", "mutton", "veal", "bacon", "ham", "sausage", "fish", "shrimp", "prawn", "anchov",
        "tuna", "salmon", "turkey", "duck", "goat meat", "meat", "stock", "broth", "gelatin", "lard", "oyster", "crab", "squid", "mussel")
ANIMAL = ("süt", "yoğurt", "peynir", "tereyağ", "krema", "kaymak", "yumurta", "bal", "lor", "kaşar", "beyaz peynir", "milk", "yogurt",
          "yoghurt", "cheese", "butter", "cream", "egg", "honey", "ghee", "paneer", "mayonnaise")


def get(host, params):
    params = dict(params, format="json", formatversion="2")
    url = f"https://{host}/w/api.php?" + urllib.parse.urlencode(params)
    if CACHE:
        p = os.path.join(CACHE, hashlib.sha1(url.encode()).hexdigest() + ".json")
        if os.path.exists(p):
            with open(p, encoding="utf-8") as f: return json.load(f)
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=90) as r:
        data = json.load(r)
    if CACHE:
        with open(p, "w", encoding="utf-8") as f: json.dump(data, f)
    return data


def all_pages(host, ns):
    out, cont = [], {}
    while True:
        d = get(host, dict(action="query", list="allpages", apnamespace=ns, aplimit=500, apfilterredir="nonredirects", **cont))
        out += [p["title"] for p in d["query"]["allpages"]]
        if "continue" not in d: return out
        cont = {"apcontinue": d["continue"]["apcontinue"]}


def members(host, cat):
    out, cont = [], {}
    while True:
        d = get(host, dict(action="query", list="categorymembers", cmtitle=cat, cmlimit=500, cmnamespace=102 if host == EN else 100, **cont))
        out += [m["title"] for m in d["query"]["categorymembers"]]
        if "continue" not in d: return out
        cont = {"cmcontinue": d["continue"]["cmcontinue"]}


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


# ---- wikitext cleaning (the app's tarif_wiki_parse does the same for pages fetched online) ----

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
    s = re.sub(r"\[\[(?:File|Image|Dosya|Resim|Category|Kategori):[^\[\]]*(?:\[\[[^\]]*\]\][^\[\]]*)*\]\]", "", s, flags=re.I)
    s = re.sub(r"\[\[[^\]|]*\|([^\]]*)\]\]", r"\1", s)
    s = re.sub(r"\[\[([^\]]*)\]\]", lambda m: m.group(1).split(":")[-1], s)
    s = re.sub(r"\[https?://\S+ ([^\]]*)\]", r"\1", s)
    s = re.sub(r"\[https?://\S+\]", "", s)
    s = re.sub(r"'{2,}", "", s)
    s = re.sub(r"__[A-Za-z]+__", "", s)
    s = re.sub(r"<br\s*/?>", " ", s)
    s = re.sub(r"<[^>]+>", "", s)
    s = html.unescape(s).replace(" ", " ").replace("\t", " ")
    return re.sub(r"\s+", " ", s).strip()


ING_RE = re.compile(r"malzeme|gerekli|ihtiya|ingredient", re.I)
STEP_RE = re.compile(r"hazırlan|yapılış|yapımı|tarif|pişir|procedure|direction|method|preparation|instruction|steps", re.I)


def parse(text):
    """→ (intro, [('+'|'-', text)], [steps], servings) from a recipe page's wikitext"""
    text = re.sub(r"\{\|.*?\|\}", "", text, flags=re.S)  # tables
    serv = 0
    m = re.search(r"servings\s*=\s*(\d+)", text) or re.search(r"(\d+)\s*kişilik", text) or re.search(r"[Ss]erves (\d+)", text)
    if m and 0 < int(m.group(1)) <= 30: serv = int(m.group(1))
    text = strip_templates(text)
    intro, ings, steps, sect = [], [], [], "intro"
    for line in text.split("\n"):
        h = re.match(r"^(={2,})\s*(.*?)\s*\1\s*$", line.strip())
        if h:
            title, level = clean(h.group(2)), len(h.group(1))
            if ING_RE.search(title) and (level == 2 or sect != "ing"): sect = "ing"
            elif STEP_RE.search(title) and (level == 2 or sect != "step"): sect = "step"
            elif sect == "ing" and level > 2: ings.append(("+", title.rstrip(":")))
            elif level == 2 or sect != "ing": sect = "other"
            continue
        raw = line.strip()
        if not raw: continue
        bullet = re.match(r"^([*#:;]+)\s*(.*)$", raw)
        body = clean(bullet.group(2) if bullet else raw)
        if not body: continue
        if sect == "intro" and (body.startswith(("<", "|")) or "listesine dön" in body): continue
        if sect == "intro" and not bullet and len(" ".join(intro)) < 400: intro.append(body)
        elif sect == "ing":
            if bullet: ings.append(("-", body))
            elif re.match(r"^\d+\s*kişilik:?$|^serves", body, re.I): continue
            elif body.endswith(":") and len(body) < 60: ings.append(("+", body.rstrip(":")))
            else: ings += [("-", p.strip()) for p in re.split(r",\s*(?![^()]*\))", body) if p.strip()]
        elif sect == "step":
            if bullet: steps.append(body)
            else: steps += split_paragraph(body)
    ings = [(k, v.rstrip(",;. ")[:300]) for k, v in ings if len(v.rstrip(",;. ")) > 1]
    return " ".join(intro)[:500], ings, [s[:1200] for s in steps if len(s) > 2], serv


def split_paragraph(p):
    """a long method paragraph → steps of about two sentences (one per screen in cooking mode)"""
    if len(p) < 320: return [p]
    sents = re.split(r"(?<=[.!?])\s+(?=[A-ZÇĞİÖŞÜ0-9])", p)
    out, cur = [], ""
    for s in sents:
        if cur and len(cur) + len(s) > 280: out.append(cur); cur = s
        else: cur = (cur + " " + s).strip()
    if cur: out.append(cur)
    return out


def tr_lower(s):
    return s.replace("İ", "i").replace("I", "ı").lower()


ALCOHOL = ("aperol", "prosecco", "votka", "vodka", " rom ", " rum", " cin ", " gin", "şarap", "wine", "bira", "beer", "likör", "liqueur",
           "rakı", "whisk", "viski", "tekila", "tequila", "brandy", "konyak", "cognac", "champagne", "şampanya", "vermut", "vermouth",
           "bourbon", "sake", "kahlua", "cointreau", "triple sec", "sherry", "port wine", "marsala", "cider")


def has_alcohol(ings):
    words = " " + " ".join(tr_lower(v) for k, v in ings if k == "-") + " "
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
    return [c.strip() for c in re.findall(r"\[\[\s*(?:Category|Kategori)\s*:\s*([^\]|]+)", text, flags=re.I)]


def record(lang, title, cuisine, tags, serv, url, intro, ings, steps):
    t = "".join(sorted(set(tags), key="catshkivg".index))
    out = [f"@R\t{lang}\t{title}\t{cuisine}\t{t}\t{serv}\t{url}\t{LICENCE}"]
    if intro: out.append("=" + intro)
    out += [k + v for k, v in ings]
    out += ["*" + s for s in steps]
    return "\n".join(out)


def page_url(host, title):
    return f"https://{host}/wiki/" + urllib.parse.quote(title.replace(" ", "_"), safe=":/(),'")


def build_tr():
    titles = [t for t in all_pages(TR, 100)]
    pages = contents(TR, titles)
    recs, src = [], []
    for title in sorted(pages, key=lambda t: t.lower()):
        rev, text = pages[title]
        cats = categories(text)
        if any(s in c.lower() for c in cats for s in SKIP_CATS): continue
        intro, ings, steps, serv = parse(text)
        if sum(1 for k, _ in ings if k == "-") < 2 or not steps or has_alcohol(ings): continue
        name = title.split(":", 1)[1]
        cuisine = TR_FOREIGN.get(name, "Türkiye")
        for c in cats:
            m = re.match(r"(.+?) Mutfağı$", c)
            if m: cuisine = TR_CUISINE_FIX.get(m.group(1), m.group(1))
        tags = tags_from(cats + [name], TR_TAGS)
        tags += diet_tags(ings, tags, steps)
        url = page_url(TR, title)
        recs.append(record("tr", name, cuisine, tags, serv, url, intro, ings, steps))
        src.append(f"{url}\trev {rev}\tVikikitap katkıcıları\t{LICENCE}")
    return recs, src


def build_en():
    picked = {}
    for adj, tr in CUISINES.items():
        for t in members(EN, f"Category:{adj} recipes"):
            picked.setdefault(t, tr)
    pages = contents(EN, sorted(picked))
    per, recs, src = {}, [], []
    # recipes with more steps first within a cuisine, so the cap keeps the fuller pages
    cand = []
    for title, (rev, text) in pages.items():
        cats = categories(text)
        if any(s in c.lower() for c in cats for s in SKIP_CATS): continue
        intro, ings, steps, serv = parse(text)
        n_ing = sum(1 for k, _ in ings if k == "-")
        if n_ing < 2 or not steps or n_ing > 40 or has_alcohol(ings): continue
        cand.append((picked[title], -min(len(steps), 8) - min(n_ing, 10), title, rev, text, cats, intro, ings, steps, serv))
    for cuisine, _, title, rev, text, cats, intro, ings, steps, serv in sorted(cand):
        cap = 40 if cuisine == "Türkiye" else PER_CUISINE
        if per.get(cuisine, 0) >= cap: continue
        per[cuisine] = per.get(cuisine, 0) + 1
        name = re.sub(r"^Cookbook:", "", title)
        tags = tags_from(cats, EN_TAGS)
        tags += diet_tags(ings, tags, steps)
        url = page_url(EN, title)
        recs.append(record("en", name, cuisine, tags, serv, url, intro, ings, steps))
        src.append(f"{url}\trev {rev}\tWikibooks contributors\t{LICENCE}")
    return recs, src


def main():
    global CACHE
    args = sys.argv[1:]
    if "--cache" in args:
        i = args.index("--cache"); CACHE = args[i + 1]; del args[i:i + 2]
        os.makedirs(CACHE, exist_ok=True)
    out = args[0] if args else "apps/tarif/data"
    os.makedirs(out, exist_ok=True)
    tr, tr_src = build_tr()
    en, en_src = build_en()
    head = ["# Tarifler offline recipe set, generated by tools/tarif/build-recipes.py — do not edit by hand.",
            "# Text from Turkish Wikibooks (Vikikitap, Yemek: namespace) and English Wikibooks (Cookbook:), " + LICENCE + ";",
            "# wiki markup removed and lists/steps re-arranged. The source page and revision of every recipe are in SOURCES."]
    with open(os.path.join(out, "recipes.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(head + tr + en) + "\n")
    with open(os.path.join(out, "SOURCES"), "w", encoding="utf-8") as f:
        f.write("# Tarifler: source page, revision, authors and licence of every bundled recipe.\n"
                "# Licence text: https://creativecommons.org/licenses/by-sa/4.0/ — the recipes in recipes.txt are adapted from these\n"
                "# pages (markup removed, lists and steps re-arranged) and are shared under the same licence.\n")
        f.write("\n".join(tr_src + en_src) + "\n")
    print(f"{len(tr)} Turkish-language recipes, {len(en)} English-language recipes")


if __name__ == "__main__":
    main()
