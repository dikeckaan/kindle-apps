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
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wikirecipe import (TR, EN, LICENCE, all_pages, members, contents, parse, has_alcohol, diet_tags, tags_from, categories,  # noqa: E402
                        record, page_url, set_cache, SKIP_CATS)

PER_CUISINE = 10

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
    args = sys.argv[1:]
    if "--cache" in args:
        i = args.index("--cache"); set_cache(args[i + 1]); del args[i:i + 2]
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
                "# pages (markup removed, lists and steps re-arranged) and are shared under the same licence.\n"
                "# The online recipe library (kaandikec.com/kindle-apps/tarif/v1, tools/tarif/build-shards.py) lists the sources of\n"
                "# its recipes at the top of every shard file.\n")
        f.write("\n".join(tr_src + en_src) + "\n")
    print(f"{len(tr)} Turkish-language recipes, {len(en)} English-language recipes")


if __name__ == "__main__":
    main()
