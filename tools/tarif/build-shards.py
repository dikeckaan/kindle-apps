#!/usr/bin/env python3
"""Builds the online recipe library of Tarifler ("Çevrimiçi tarif kütüphanesi"), served from GitHub Pages next to the
package repo: <site>/tarif/v1/{index.tsv, manifest.txt, LICENSE.txt, s/NNNN.txt}.

Sources (all CC BY-SA 4.0, text by the Wikibooks contributors; every recipe keeps its page URL and revision):
  * Turkish Wikibooks (Vikikitap), every page of the "Yemek:" namespace;
  * English Wikibooks, every page in Category:Recipes ("Cookbook:" namespace);
  * German Wikibooks, every "Kochbuch/ …" page (German text, shown as "Almanca" in the app).
The wikitext → recipe conversion is the bundled set's (wikirecipe.py): pages without an ingredient list and a step
(index and article pages), drinks with alcohol and pet food are skipped.

API use: one request at a time, User-Agent with contact address, maxlag=5 with backoff. Page lists are fetched fresh
(a few dozen requests); page texts are cached on disk by page id and revision (<cache>/rev/<host>/<pageid>.json), so a
rerun only downloads the pages edited since.

Output (UTF-8):
  index.tsv     "#tarifler-index<TAB>1<TAB>build<TAB>count" then one line per recipe:
                id<TAB>title<TAB>lang<TAB>cuisine<TAB>tags<TAB>keywords<TAB>shard
                keywords: main ingredients as folded Turkish words ("domates sogan et"), for "Elimde ne var?";
                ids are 0.., sorted Turkish first, then English, then German, each by title.
  s/NNNN.txt    the recipes of shard NNNN in the app's recipes.txt format (see wikirecipe.py), at most 40 recipes and
                100 KB; '#' lines on top: what it is, and a SOURCES line (url, revision, authors, licence) per recipe.
  manifest.txt  key=value: build, date, recipes, per-language counts, shards, bytes.
  LICENSE.txt   attribution and licence.

Usage: tools/tarif/build-shards.py [outdir (default build/tarif-lib)] [--cache DIR (default build/tarif-cache)] [--only tr,en,de]
"""
import datetime, hashlib, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wikirecipe as W  # noqa: E402
from wikirecipe import TR, EN, DE, LICENCE, parse, has_alcohol, diet_tags, tags_from, record, page_url, fold, log  # noqa: E402

SHARD_MAX_RECIPES = 40
SHARD_MAX_BYTES = 100 * 1024
MAX_ING = 60
LANG_ORDER = {"tr": 0, "en": 1, "de": 2}
AUTHORS = {TR: "Vikikitap katkıcıları", EN: "Wikibooks contributors", DE: "Wikibooks-Autoren"}

# ---- cuisines (Turkish country names, as in the app) ----

EN_CUISINES = {
    "Turkish": "Türkiye", "Ottoman": "Türkiye", "Italian": "İtalya", "French": "Fransa", "Indian": "Hindistan", "Chinese": "Çin", "Japanese": "Japonya",
    "Mexican": "Meksika", "Thai": "Tayland", "Greek": "Yunanistan", "Spanish": "İspanya", "German": "Almanya", "Austrian": "Avusturya",
    "Egyptian": "Mısır", "Ethiopian": "Etiyopya", "Eritrean": "Eritre", "Nigerian": "Nijerya", "Ghanaian": "Gana", "Moroccan": "Fas",
    "Algerian": "Cezayir", "Tunisian": "Tunus", "Lebanese": "Lübnan", "Syrian": "Suriye", "Iraqi": "Irak", "Jordanian": "Ürdün",
    "Saudi Arabian": "Suudi Arabistan", "Yemeni": "Yemen", "Kuwaiti": "Kuveyt", "Emirati": "BAE", "Omani": "Umman",
    "Iranian": "İran", "Persian": "İran", "Israeli": "İsrail", "Palestinian": "Filistin", "Afghan": "Afganistan", "Armenian": "Ermenistan",
    "Azerbaijani": "Azerbaycan", "Georgian": "Gürcistan", "Korean": "Kore", "Vietnamese": "Vietnam", "Indonesian": "Endonezya",
    "Filipino": "Filipinler", "Malaysian": "Malezya", "Singaporean": "Singapur", "Burmese": "Myanmar", "Cambodian": "Kamboçya",
    "Laotian": "Laos", "Mongolian": "Moğolistan", "Tibetan": "Tibet", "Taiwanese": "Tayvan", "Pakistani": "Pakistan", "Nepali": "Nepal",
    "Nepalese": "Nepal", "Bengali": "Bangladeş", "Bangladeshi": "Bangladeş", "Sri Lankan": "Sri Lanka", "Uzbek": "Özbekistan",
    "Kazakh": "Kazakistan", "Russian": "Rusya", "Ukrainian": "Ukrayna", "Belarusian": "Belarus", "Polish": "Polonya",
    "Hungarian": "Macaristan", "Bulgarian": "Bulgaristan", "Romanian": "Romanya", "Albanian": "Arnavutluk", "Serbian": "Sırbistan",
    "Croatian": "Hırvatistan", "Bosnian": "Bosna-Hersek", "Slovenian": "Slovenya", "Slovak": "Slovakya", "Slovakian": "Slovakya",
    "Macedonian": "Kuzey Makedonya", "Montenegrin": "Karadağ", "Lithuanian": "Litvanya", "Latvian": "Letonya", "Estonian": "Estonya",
    "Swedish": "İsveç", "Norwegian": "Norveç", "Danish": "Danimarka", "Finnish": "Finlandiya", "Icelandic": "İzlanda",
    "Dutch": "Hollanda", "Belgian": "Belçika", "Luxembourgish": "Lüksemburg", "Swiss": "İsviçre", "Portuguese": "Portekiz",
    "Maltese": "Malta", "Czech": "Çekya", "Cypriot": "Kıbrıs", "British": "İngiltere", "English": "İngiltere", "Scottish": "İskoçya",
    "Irish": "İrlanda", "Welsh": "Galler", "American": "ABD", "United States": "ABD", "Cajun": "ABD", "Southern United States": "ABD",
    "Hawaiian": "ABD", "Tex-Mex": "ABD", "Canadian": "Kanada", "Brazilian": "Brezilya", "Argentine": "Arjantin", "Argentinian": "Arjantin",
    "Chilean": "Şili", "Peruvian": "Peru", "Bolivian": "Bolivya", "Ecuadorian": "Ekvador", "Colombian": "Kolombiya",
    "Venezuelan": "Venezuela", "Uruguayan": "Uruguay", "Paraguayan": "Paraguay", "Cuban": "Küba", "Jamaican": "Jamaika",
    "Puerto Rican": "Porto Riko", "Dominican": "Dominik", "Haitian": "Haiti", "Trinidadian": "Trinidad ve Tobago", "Guatemalan": "Guatemala",
    "Honduran": "Honduras", "Salvadoran": "El Salvador", "Nicaraguan": "Nikaragua", "Costa Rican": "Kosta Rika", "Panamanian": "Panama",
    "Australian": "Avustralya", "New Zealand": "Yeni Zelanda", "Kenyan": "Kenya", "Tanzanian": "Tanzanya", "South African": "Güney Afrika",
    "Libyan": "Libya", "Sudanese": "Sudan", "Somali": "Somali", "Rwandan": "Ruanda", "Ugandan": "Uganda", "Cameroonian": "Kamerun",
    "Gambian": "Gambiya", "Liberian": "Liberya", "Zambian": "Zambiya", "Zimbabwean": "Zimbabve", "Senegalese": "Senegal",
    "Malian": "Mali", "Ivorian": "Fildişi Sahili", "Congolese": "Kongo", "Malagasy": "Madagaskar", "Mozambican": "Mozambik",
    "Sierra Leonean": "Sierra Leone", "Jewish": "Yahudi mutfağı",
}
DE_CUISINES = {
    "Türkische": "Türkiye", "Deutsche": "Almanya", "Bayerische": "Almanya", "Schwäbische": "Almanya", "Westfälische": "Almanya",
    "Badische": "Almanya", "Fränkische": "Almanya", "Sächsische": "Almanya", "Hessische": "Almanya", "Pfälzer": "Almanya",
    "Pfälzische": "Almanya", "Norddeutsche": "Almanya", "Berliner": "Almanya", "Thüringer": "Almanya", "Rheinische": "Almanya",
    "Saarländische": "Almanya", "Schlesische": "Polonya", "Ostpreußische": "Almanya", "Mecklenburgische": "Almanya",
    "Österreichische": "Avusturya", "Wiener": "Avusturya", "Schweizer": "İsviçre", "Italienische": "İtalya", "Französische": "Fransa",
    "Griechische": "Yunanistan", "Spanische": "İspanya", "Portugiesische": "Portekiz", "Ungarische": "Macaristan", "Böhmische": "Çekya",
    "Tschechische": "Çekya", "Polnische": "Polonya", "Russische": "Rusya", "Ukrainische": "Ukrayna", "Chinesische": "Çin",
    "Japanische": "Japonya", "Thailändische": "Tayland", "Indische": "Hindistan", "Indonesische": "Endonezya", "Vietnamesische": "Vietnam",
    "Koreanische": "Kore", "Mexikanische": "Meksika", "Englische": "İngiltere", "Britische": "İngiltere", "Schottische": "İskoçya",
    "Irische": "İrlanda", "Finnische": "Finlandiya", "Schwedische": "İsveç", "Norwegische": "Norveç", "Dänische": "Danimarka",
    "Niederländische": "Hollanda", "Belgische": "Belçika", "Slowenische": "Slovenya", "Kroatische": "Hırvatistan", "Serbische": "Sırbistan",
    "Bulgarische": "Bulgaristan", "Rumänische": "Romanya", "Chilenische": "Şili", "Argentinische": "Arjantin", "Brasilianische": "Brezilya",
    "Peruanische": "Peru", "Amerikanische": "ABD", "US-amerikanische": "ABD", "Cajun-": "ABD", "Gambia-": "Gambiya",
    "Marokkanische": "Fas", "Tunesische": "Tunus", "Ägyptische": "Mısır", "Äthiopische": "Etiyopya", "Libanesische": "Lübnan",
    "Persische": "İran", "Iranische": "İran", "Israelische": "İsrail", "Jüdische": "Yahudi mutfağı", "Arabische": "Arabistan",
    "Syrische": "Suriye", "Afghanische": "Afganistan", "Georgische": "Gürcistan", "Armenische": "Ermenistan", "Kanadische": "Kanada",
    "Australische": "Avustralya", "Elsässische": "Fransa", "Südtiroler": "İtalya", "Kärntner": "Avusturya", "Tiroler": "Avusturya",
    "Steirische": "Avusturya", "Ostfriesische": "Almanya", "Schleswig-Holsteinische": "Almanya", "Hamburger": "Almanya",
    "Niedersächsische": "Almanya", "Brandenburgische": "Almanya", "Erzgebirgische": "Almanya", "Oberpfälzer": "Almanya",
    "Allgäuer": "Almanya", "Harzer": "Almanya", "Liechtensteiner": "Lihtenştayn", "Sudetendeutsche": "Çekya", "Siebenbürgische": "Romanya", "Banater": "Romanya",
}
TR_FOREIGN = {
    "Fettuccine Alfredo": "İtalya", "Fish and chips": "İngiltere", "BLT Sandviç": "ABD", "Kulüp sandviç": "ABD", "Hint Omleti": "Hindistan",
    "Mafaldine, Kuşkonmaz Soslu": "İtalya", "Cazuela de Mariscos": "Kolombiya", "Bandeja Paisa": "Kolombiya", "Arepa": "Kolombiya",
    "Basmati pirinç": "Hindistan",
}
TR_CUISINE_FIX = {"Türk": "Türkiye", "Türkiye": "Türkiye", "Amerikan": "ABD", "Amerikan (ABD)": "ABD", "Fransız": "Fransa", "İtalyan": "İtalya",
                  "İspanyol": "İspanya", "Hırvat": "Hırvatistan", "Eston": "Estonya", "Japonya": "Japonya", "Yunan": "Yunanistan",
                  "Japon": "Japonya", "Alman": "Almanya", "Rus": "Rusya", "Macar": "Macaristan", "Arap": "Arabistan"}

# ---- kinds: c soup, a main dish, t dessert, s salad / meze, h pastry / bread, k breakfast, i drink, v vegan, g vegetarian ----

EN_TAGS = [("Recipes for soup", "c"), ("Soup recipes", "c"), ("Recipes for stew", "a"), ("Main course recipes", "a"), ("Dinner recipes", "a"),
           ("Recipes for curry", "a"), ("Recipes for casserole", "a"), ("Recipes for pasta", "a"), ("Recipes for risotto", "a"),
           ("Recipes for dessert", "t"), ("Dessert recipes", "t"), ("Recipes for cake", "t"), ("Recipes for cookies", "t"),
           ("Recipes for pudding", "t"), ("Recipes for confections", "t"), ("Recipes for ice cream", "t"), ("Recipes for frozen desserts", "t"),
           ("Recipes for pies and tarts", "t"), ("Recipes for custard", "t"), ("Recipes for salad", "s"), ("Appetizer recipes", "s"),
           ("Recipes for dip", "s"), ("Side dish recipes", "s"), ("Recipes for bread", "h"), ("Recipes for pastries", "h"),
           ("Recipes for dough", "h"), ("Recipes for pizza", "h"), ("Recipes for muffins", "h"), ("Recipes for scones", "h"),
           ("Breakfast recipes", "k"), ("Recipes for omelettes", "k"), ("Recipes for porridge", "k"), ("Recipes for beverages", "i"),
           ("Vegan recipes", "v"), ("Vegetarian recipes", "g")]
TR_TAGS = [("çorba", "c"), ("et yemek", "a"), ("tavuk", "a"), ("balık", "a"), ("ana yemek", "a"), ("sebze yemek", "a"), ("pilav", "a"),
           ("makarna", "a"), ("köfte", "a"), ("kebap", "a"), ("dolma", "a"), ("tatlı", "t"), ("kurabiye", "t"), ("kek", "t"), ("pasta", "t"),
           ("reçel", "t"), ("marmelat", "t"), ("helva", "t"), ("dondurma", "t"), ("salata", "s"), ("meze", "s"), ("börek", "h"),
           ("hamur", "h"), ("ekmek", "h"), ("kahvaltı", "k"), ("hoşaf", "i"), ("içecek", "i"), ("sandviç", "k"), ("tost", "k"),
           ("omlet", "k"), ("menemen", "k"), ("cacık", "s"), ("humus", "s"), ("ezme", "s"), ("piyaz", "s"), ("kısır", "s"), ("turşu", "s"),
           ("vegan", "v"), ("vejetaryen", "g")]
DE_TAGS = [("Suppe", "c"), ("Eintopf", "c"), ("Eintöpfe", "c"), ("Fleischgericht", "a"), ("Fischgericht", "a"), ("Geflügel", "a"),
           ("Aufläufe", "a"), ("Auflauf", "a"), ("Hauptgericht", "a"), ("Nudelgericht", "a"), ("Gemüsegericht", "a"), ("Wildgericht", "a"),
           ("Kartoffelgericht", "a"), ("Reisgericht", "a"), ("Dessert", "t"), ("Kuchen", "t"), ("Torte", "t"), ("Süßigkeit", "t"),
           ("Süßspeise", "t"), ("Weihnachtsgebäck", "t"), ("Kekse", "t"), ("Plätzchen", "t"), ("/ Eis", "t"), ("Salat", "s"),
           ("Vorspeise", "s"), ("Beilage", "s"), ("Dips", "s"), ("Brot", "h"), ("Gebäck", "h"), ("Pizza", "h"), ("Frühstück", "k"),
           ("Getränk", "i"), ("Tee", "i"), ("Vegane", "v"), ("Vegetarische", "g")]

# ---- main ingredients for "Elimde ne var?": canonical folded Turkish name ← tr | en | de patterns (folded) ----
# A plain pattern matches at a word start, "=x" only the whole word, "~x" anywhere (German compounds). Longer patterns are
# matched first and consume their text ("olive oil" is not also "oil", "Kichererbse" not also "Erbse").
KEYWORDS = {
    "domates": ("domates", "tomato", "tomate"),
    "salca": ("salca", "tomato paste, tomato puree, tomato concentrate", "tomatenmark, ~tomatenmark"),
    "sogan": ("sogan", "onion, shallot, scallion", "zwiebel, ~zwiebel, schalotte"),
    "sarimsak": ("sarimsak", "garlic", "knoblauch, ~knoblauch"),
    "patates": ("patates", "potato", "kartoffel, ~kartoffel"),
    "havuc": ("havuc", "carrot", "karotte, ~karotte, mohre, ~mohre"),
    "biber": ("biber, sivri, dolmalik", "bell pepper, green pepper, red pepper, peppers, capsicum, chili, chile, chilli, jalapeno, paprika",
              "paprika, ~paprika, peperoni, chili, ~chili, peperoncini"),
    "karabiber": ("karabiber, kara biber", "black pepper, white pepper, =pepper, peppercorn", "pfeffer, ~pfeffer"),
    "biberiye": ("biberiye", "rosemary", "rosmarin"),
    "patlican": ("patlican", "eggplant, aubergine, brinjal", "aubergine, ~aubergine"),
    "kabak": ("kabak, bal kabag", "zucchini, courgette, squash, butternut, pumpkin", "zucchini, ~zucchini, kurbis, ~kurbis"),
    "ispanak": ("ispanak", "spinach", "spinat, ~spinat"),
    "mercimek": ("mercimek", "lentil, =dal", "linse, ~linsen"),
    "nohut": ("nohut", "chickpea, chick pea, garbanzo", "kichererbse, ~kichererbse"),
    "fasulye": ("fasulye, barbunya, borulce", "bean", "bohne, ~bohne"),
    "pirinc": ("pirinc", "rice, basmati, arborio", "=reis, ~milchreis, ~basmatireis, ~langkornreis, ~risottoreis, risotto"),
    "bulgur": ("bulgur", "bulgur, bulghur, burghul", "bulgur"),
    "un": ("=un, =unu, =undan", "flour", "mehl, ~mehl"),
    "irmik": ("irmik", "semolina", "griess, ~griess"),
    "nisasta": ("nisasta", "cornstarch, corn starch, cornflour, corn flour, starch", "starke, ~starke"),
    "seker": ("seker, toz seker", "sugar", "zucker, ~zucker"),
    "yumurta": ("yumurta", "egg", "=ei, eier, eigelb, eiweiss, ~eigelb, ~eiweiss"),
    "sut": ("=sut, =sutu, sutu", "milk, buttermilk", "milch, ~milch"),
    "yogurt": ("yogurt", "yogurt, yoghurt", "joghurt, ~joghurt"),
    "peynir": ("peynir, kasar, =lor, beyaz peynir", "cheese, cream cheese, feta, mozzarella, parmesan, cheddar, ricotta, paneer, mascarpone, halloumi",
               "kase, ~kase, feta, mozzarella, parmesan, ricotta, mascarpone, quark"),
    "tereyag": ("tereyag", "butter, ghee", "butter, ~butter"),
    "krema": ("krema, kaymak", "cream, heavy cream, sour cream, whipping cream", "sahne, ~sahne, schmand, creme fraiche"),
    "zeytinyag": ("zeytinyag, zeytin yag", "olive oil", "olivenol, ~olivenol"),
    "yag": ("yag, aycicek yag, sivi yag", "oil", "=ol, ~speiseol, ~pflanzenol, ~sonnenblumenol, ~rapsol, ~bratol"),
    "zeytin": ("zeytin", "olive", "olive, ~oliven"),
    "tavuk": ("tavuk, pilic", "chicken", "hahnchen, ~hahnchen, huhn, ~huhn, hendl"),
    "hindi": ("hindi", "turkey", "pute, ~pute, truthahn"),
    "ordek": ("ordek", "duck", "ente, entenbrust"),
    "kiyma": ("kiyma", "ground beef, ground lamb, ground meat, ground pork, ground turkey, minced meat, minced beef, minced lamb, beef mince, lamb mince, hamburger", "hackfleisch, ~hackfleisch, hack"),
    "dana": ("dana", "beef, veal, steak", "rind, ~rind, kalb, ~kalb"),
    "kuzu": ("kuzu", "lamb, mutton", "lamm, ~lamm, hammel"),
    "et": ("=et, =eti, kusbasi", "meat", "fleisch, ~fleisch"),
    "domuz": ("domuz", "pork, bacon, =ham, prosciutto, pancetta", "schwein, ~schwein, speck, ~speck, schinken, ~schinken"),
    "sucuk": ("sucuk, sosis, salam", "sausage, chorizo, salami, pepperoni", "wurst, ~wurst, salami"),
    "balik": ("balik, levrek, hamsi, cupra, palamut, uskumru, alabalik", "fish, cod, haddock, tilapia, trout, sardine, anchov, mackerel",
              "fisch, ~fisch, forelle, kabeljau, seelachs, hering, makrele"),
    "somon": ("somon", "salmon", "lachs, ~lachs"),
    "ton": ("ton balig", "tuna", "thunfisch, ~thunfisch"),
    "karides": ("karides", "shrimp, prawn", "garnele, ~garnele, krabbe, shrimps"),
    "limon": ("limon", "lemon, lime", "zitrone, ~zitrone, limette, ~limette"),
    "portakal": ("portakal", "orange", "orange, ~orange, apfelsine"),
    "elma": ("elma", "apple", "apfel, ~apfel"),
    "armut": ("armut", "pear", "birne, ~birne"),
    "muz": ("muz", "banana", "banane, ~banane"),
    "cilek": ("cilek", "strawberr", "erdbeer, ~erdbeer"),
    "ahududu": ("ahududu", "raspberr", "himbeer, ~himbeer"),
    "kiraz": ("kiraz, visne", "cherr", "kirsch, ~kirsch"),
    "kayisi": ("kayisi", "apricot", "aprikose, ~aprikose, marille"),
    "seftali": ("seftali", "peach, nectarine", "pfirsich, ~pfirsich"),
    "erik": ("erik", "plum, prune", "pflaume, ~pflaume, zwetschge, ~zwetsch"),
    "uzum": ("uzum", "grape, raisin, sultana", "traube, ~traube, rosine, ~rosine"),
    "incir": ("incir", "=fig, =figs", "feige, ~feige"),
    "nar": ("=nar", "pomegranate", "granatapfel, ~granatapfel"),
    "ananas": ("ananas", "pineapple", "ananas"),
    "hindistancevizi": ("hindistan cevizi, hindistancevizi", "coconut", "kokos, ~kokos"),
    "ceviz": ("ceviz", "walnut", "walnuss, ~walnuss"),
    "findik": ("findik", "hazelnut", "haselnuss, ~haselnuss"),
    "badem": ("badem", "almond", "mandel, ~mandel"),
    "fistik": ("fistik", "pistachio, peanut, peanut butter", "pistazie, erdnuss, ~erdnuss"),
    "kestane": ("kestane", "chestnut", "marone, kastanie, ~kastanie"),
    "susam": ("susam", "sesame", "sesam, ~sesam"),
    "tahin": ("tahin", "tahini, tahina", "tahin"),
    "bal": ("=bal, =bali", "honey", "honig, ~honig"),
    "cikolata": ("cikolata", "chocolate", "schokolade, ~schokolade, kuvertur"),
    "kakao": ("kakao", "cocoa", "kakao"),
    "vanilya": ("vanilya, vanilin", "vanilla", "vanille, ~vanille"),
    "tarcin": ("tarcin", "cinnamon", "zimt, ~zimt"),
    "kimyon": ("kimyon", "cumin", "kreuzkummel, kummel"),
    "zencefil": ("zencefil", "ginger", "ingwer, ~ingwer"),
    "nane": ("nane", "mint", "minze, ~minze"),
    "maydanoz": ("maydanoz", "parsley", "petersilie, ~petersilie"),
    "dereotu": ("dereotu", "dill", "dill"),
    "feslegen": ("feslegen", "basil", "basilikum"),
    "kekik": ("kekik", "thyme, oregano", "thymian, oregano"),
    "mantar": ("mantar", "mushroom", "pilz, ~pilz, champignon, ~champignon"),
    "misir": ("misir", "=corn, sweet corn, corn kernel, cornmeal, polenta, maize", "mais, ~mais, polenta"),
    "bezelye": ("bezelye", "=pea, =peas, green peas, snow pea", "erbse, ~erbse"),
    "lahana": ("lahana", "cabbage, sauerkraut, kale", "kohl, ~kohl, sauerkraut, ~kraut, wirsing"),
    "karnabahar": ("karnabahar, karnibahar", "cauliflower", "blumenkohl, ~blumenkohl"),
    "brokoli": ("brokoli", "broccoli", "brokkoli, ~brokkoli"),
    "salatalik": ("salatalik", "cucumber", "gurke, ~gurke"),
    "marul": ("marul, kivircik", "lettuce", "kopfsalat, eisbergsalat, feldsalat, ~blattsalat"),
    "pirasa": ("pirasa", "leek", "lauch, ~lauch, porree"),
    "kereviz": ("kereviz", "celery, celeriac", "sellerie, ~sellerie"),
    "pancar": ("pancar", "beet", "rote bete, rote beete, ~rote bete"),
    "turp": ("turp", "radish", "rettich, radieschen"),
    "bamya": ("bamya", "okra", "okra"),
    "enginar": ("enginar", "artichoke", "artischocke"),
    "avokado": ("avokado", "avocado", "avocado"),
    "makarna": ("makarna, spagetti, eriste", "pasta, noodle, spaghetti, macaroni, penne, fettuccine, linguine, lasagn, tagliatelle, vermicelli",
                "nudel, ~nudel, spaghetti, makkaroni, penne, tagliatelle, lasagne, spatzle, ~spatzle"),
    "ekmek": ("ekmek, pide, lavas, galeta", "bread, baguette, pita, tortilla, breadcrumb", "brot, ~brot, ~brotchen, toast, baguette, semmel, ~semmel"),
    "kuskus": ("kuskus", "couscous", "couscous"),
    "yulaf": ("yulaf", "=oat, =oats, oatmeal, rolled oats", "hafer, ~hafer"),
    "maya": ("maya", "yeast", "hefe, ~hefe"),
    "sirke": ("sirke", "vinegar", "essig, ~essig"),
    "hardal": ("hardal", "mustard", "senf, ~senf"),
    "kahve": ("kahve", "coffee, espresso", "kaffee, ~kaffee, espresso"),
    "tofu": ("tofu", "tofu", "tofu"),
    "kinoa": ("kinoa", "quinoa", "quinoa"),
    "arpa": ("arpa", "barley", "gerste, graupen"),
    "_": ("kabartma tozu, karbonat", "cream of tartar, baking powder, baking soda", "backpulver, natron, weinstein"),  # only consumed
}
KEYWORD_SKIP = {"karabiber", "_"}  # only there to keep "black pepper" from counting as a pepper, "cream of tartar" as cream
KEYWORD_IMPLIES = {"kiyma": "et", "dana": "et", "kuzu": "et"}
MAX_KEYWORDS = 10


def _patterns(lang):
    col = {"tr": 0, "en": 1, "de": 2}[lang]
    out = []
    for canon, cols in KEYWORDS.items():
        for p in cols[col].split(","):
            p = p.strip()
            if not p: continue
            mode = p[0] if p[0] in "=~" else ""
            p = fold(p.lstrip("=~"))
            out.append((len(p), p, mode, canon))
    out.sort(key=lambda t: -t[0])
    return [(p, mode, canon) for _, p, mode, canon in out]


PATTERNS = {lang: _patterns(lang) for lang in ("tr", "en", "de")}


def keywords(lang, ings):
    """main ingredients of a recipe as canonical folded Turkish words, in ingredient order"""
    found = []
    for k, v in ings:
        if k != "-": continue
        s = " " + fold(v) + " "
        hits = []
        for p, mode, canon in PATTERNS[lang]:
            if mode == "~": rx = re.escape(p)
            elif mode == "=": rx = r"(?<= )" + re.escape(p) + r"(?= )"
            else: rx = r"(?<= )" + re.escape(p)
            for m in list(re.finditer(rx, s)):
                hits.append((m.start(), canon))
                s = s[:m.start()] + "#" * (m.end() - m.start()) + s[m.end():]
        for _, canon in sorted(hits):
            for c in (canon, KEYWORD_IMPLIES.get(canon)):
                if c and c not in KEYWORD_SKIP and c not in found: found.append(c)
    return found[:MAX_KEYWORDS]


# ---- page lists and texts ----

def list_pages(host, params):
    """[(pageid, title, lastrevid)] of a generator query, fresh from the API"""
    out = []
    for d in W.query_all(host, dict(params, action="query", prop="info"), cache=False):
        out += [(p["pageid"], p["title"], p["lastrevid"]) for p in d.get("query", {}).get("pages", []) if "missing" not in p]
    return out


def fetch_texts(host, pages, cache_dir):
    """pageid → {title, revid, content, categories}: from the revision cache, the rest from the API 50 pages at a time"""
    d = os.path.join(cache_dir, "rev", host)
    os.makedirs(d, exist_ok=True)
    res, todo = {}, []
    for pid, title, rev in pages:
        p = os.path.join(d, f"{pid}.json")
        if os.path.exists(p):
            with open(p, encoding="utf-8") as f: c = json.load(f)
            if c.get("revid") == rev:
                res[pid] = c
                continue
        todo.append(pid)
    log(f"{host}: {len(pages)} pages, {len(pages) - len(todo)} cached, fetching {len(todo)}")
    for i in range(0, len(todo), 50):
        batch = todo[i:i + 50]
        got = {}
        for ans in W.query_all(host, dict(action="query", pageids="|".join(map(str, batch)), prop="revisions|categories",
                                          rvprop="ids|content", rvslots="main", cllimit="max", clshow="!hidden"), cache=False):
            for p in ans.get("query", {}).get("pages", []):
                g = got.setdefault(p["pageid"], {"title": p["title"], "categories": []})
                if p.get("revisions"):
                    rv = p["revisions"][0]
                    g["revid"] = rv["revid"]
                    g["content"] = rv["slots"]["main"].get("content", "")
                g["categories"] += [c["title"].split(":", 1)[1] for c in p.get("categories", [])]
        for pid, g in got.items():
            if "revid" not in g: continue
            res[pid] = g
            with open(os.path.join(d, f"{pid}.json.tmp"), "w", encoding="utf-8") as f: json.dump(g, f, ensure_ascii=False)
            os.replace(os.path.join(d, f"{pid}.json.tmp"), os.path.join(d, f"{pid}.json"))
        log(f"  {min(i + 50, len(todo))}/{len(todo)}")
    return res


# ---- one site → recipes ----

def convert(host, lang, name, page, cuisine_of, tag_table):
    text, cats = page["content"], page["categories"]
    if any(s in c.lower() for c in cats for s in W.SKIP_CATS): return None
    if lang == "de" and re.search(r"\|\s*Alkohol\s*=\s*Ja", text): return None
    intro, ings, steps, serv = parse(text)
    n_ing = sum(1 for k, _ in ings if k == "-")
    # vinegars are not drinks: "apple cider vinegar", "red wine vinegar" do not make a recipe one with alcohol
    dry = [(k, re.sub(r"(cider|wine)\s+vinegar", "vinegar", v, flags=re.I)) for k, v in ings]
    if n_ing < 2 or not steps or n_ing > MAX_ING or has_alcohol(dry, lang): return None
    cuisine = cuisine_of(name, cats)
    tags = tags_from(cats + ([name] if lang == "tr" else []), tag_table)
    if lang == "de":
        m = re.search(r"\|\s*Vegetarisch\s*=\s*(Ja|Vegan)", text)
        if m: tags += "vg" if m.group(1) == "Vegan" else "g"
        if "v" in tags: tags += "g"
    else:
        tags += diet_tags(ings, tags, steps)
    if "c" not in tags and re.search(r"çorba|soup|suppe", name, re.I): tags += "c"
    url = page_url(host, page["title"])
    return dict(lang=lang, title=name, cuisine=cuisine, tags=W.sort_tags(tags), url=url, rev=page["revid"], authors=AUTHORS[host],
                text=record(lang, name, cuisine, tags, serv, url, intro, ings, steps), keywords=keywords(lang, ings))


def tr_cuisine(name, cats):
    c = TR_FOREIGN.get(name, "Türkiye")
    for cat in cats:
        m = re.match(r"(.+?) Mutfağı$", cat)
        if m: c = TR_CUISINE_FIX.get(m.group(1), m.group(1))
    return c


UNMAPPED = {}


def en_cuisine(name, cats):
    for cat in cats:
        m = re.match(r"(.+?) recipes$", cat)
        if m and m.group(1) in EN_CUISINES: return EN_CUISINES[m.group(1)]
    return "Dünya"


def de_cuisine(name, cats):
    for cat in cats:
        m = re.match(r"Kochbuch/\s*(.+?)\s*-?\s*Küche$", cat)
        if not m: continue
        adj = m.group(1)
        if adj in DE_CUISINES: return DE_CUISINES[adj]
        if adj + "-" in DE_CUISINES: return DE_CUISINES[adj + "-"]
        UNMAPPED[adj] = UNMAPPED.get(adj, 0) + 1
    return "Dünya"


SITES = {
    "tr": (TR, dict(generator="allpages", gapnamespace=100, gapfilterredir="nonredirects", gaplimit=500),
           lambda t: t.split(":", 1)[1], tr_cuisine, TR_TAGS),
    "en": (EN, dict(generator="categorymembers", gcmtitle="Category:Recipes", gcmnamespace=102, gcmtype="page", gcmlimit=500),
           lambda t: t.split(":", 1)[1], en_cuisine, EN_TAGS),
    "de": (DE, dict(generator="allpages", gapnamespace=0, gapprefix="Kochbuch/", gapfilterredir="nonredirects", gaplimit=500),
           lambda t: re.sub(r"^Kochbuch/\s*", "", t), de_cuisine, DE_TAGS),
}


def build_site(lang, cache_dir):
    host, gen, name_of, cuisine_of, tag_table = SITES[lang]
    pages = list_pages(host, gen)
    if lang == "de":  # navigation, templates and other sub pages of the cookbook are not recipes
        pages = [p for p in pages if "/" not in re.sub(r"^Kochbuch/\s*", "", p[1])]
    texts = fetch_texts(host, pages, cache_dir)
    out, seen = [], set()
    for pid, title, _ in pages:
        if pid not in texts: continue
        name = name_of(title).strip()
        if not name or "\t" in name or name.lower() in seen: continue
        r = convert(host, lang, name, texts[pid], cuisine_of, tag_table)
        if r:
            seen.add(name.lower())
            out.append(r)
    log(f"{lang}: {len(out)} recipes from {len(pages)} pages")
    return out


# ---- output ----

def write(outdir, recipes):
    recipes.sort(key=lambda r: (LANG_ORDER[r["lang"]], fold(r["title"]), r["title"]))
    v1 = os.path.join(outdir, "v1")
    sdir = os.path.join(v1, "s")
    os.makedirs(sdir, exist_ok=True)
    for f in os.listdir(sdir):
        if f.endswith(".txt"): os.remove(os.path.join(sdir, f))
    head = ("# Tarifler online recipe library, shard {n:04d}; generated by tools/tarif/build-shards.py — do not edit by hand.\n"
            "# Text from Wikibooks (tr: Vikikitap Yemek:, en: Cookbook, de: Kochbuch) by the Wikibooks contributors, " + LICENCE + ";\n"
            "# wiki markup removed and lists/steps re-arranged. Licence: https://creativecommons.org/licenses/by-sa/4.0/\n"
            "# SOURCES: page, revision, authors, licence of every recipe below:\n")
    shards, cur, size = [], [], 0
    def src(r): return f"# {r['url']}\trev {r['rev']}\t{r['authors']}\t{LICENCE}\n"
    for r in recipes:
        need = len((src(r) + r["text"] + "\n").encode())
        if need + len(head.encode()) + 16 > SHARD_MAX_BYTES:
            log(f"skipped (too long for a shard): {r['url']}")
            r["skip"] = True
            continue
        if cur and (len(cur) >= SHARD_MAX_RECIPES or size + need > SHARD_MAX_BYTES):
            shards.append(cur); cur, size = [], 0
        if not cur: size = len(head.format(n=len(shards)).encode())
        cur.append(r); size += need
    if cur: shards.append(cur)
    lines, total = [], 0
    rid = 0
    for n, sh in enumerate(shards):
        body = head.format(n=n) + "".join(src(r) for r in sh) + "".join(r["text"] + "\n" for r in sh)
        data = body.encode()
        assert len(data) <= SHARD_MAX_BYTES
        with open(os.path.join(sdir, f"{n:04d}.txt"), "wb") as f: f.write(data)
        total += len(data)
        for r in sh:
            lines.append(f"{rid}\t{r['title']}\t{r['lang']}\t{r['cuisine']}\t{r['tags']}\t{' '.join(r['keywords'])}\t{n}")
            rid += 1
    body = "\n".join(lines) + "\n"
    build = hashlib.sha1(body.encode() + b"".join(open(os.path.join(sdir, f"{n:04d}.txt"), "rb").read() for n in range(len(shards)))).hexdigest()[:12]
    index = f"#tarifler-index\t1\t{build}\t{rid}\n" + body
    with open(os.path.join(v1, "index.tsv"), "w", encoding="utf-8") as f: f.write(index)
    count = {l: sum(1 for r in recipes if r["lang"] == l and not r.get("skip")) for l in LANG_ORDER}
    sizes = [os.path.getsize(os.path.join(sdir, f"{n:04d}.txt")) for n in range(len(shards))]
    man = {"build": build, "date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), "recipes": rid,
           "tr": count["tr"], "en": count["en"], "de": count["de"], "shards": len(shards), "index_bytes": len(index.encode()),
           "shard_bytes": total, "shard_max_bytes": max(sizes), "licence": LICENCE}
    with open(os.path.join(v1, "manifest.txt"), "w", encoding="utf-8") as f:
        f.write("".join(f"{k}={v}\n" for k, v in man.items()))
    with open(os.path.join(v1, "LICENSE.txt"), "w", encoding="utf-8") as f:
        f.write("Tarifler online recipe library\n\n"
                "The recipes in s/*.txt are adapted from Wikibooks — Turkish Wikibooks (Vikikitap, tr.wikibooks.org, \"Yemek:\" pages),\n"
                "the English Wikibooks Cookbook (en.wikibooks.org) and the German Wikibooks Kochbuch (de.wikibooks.org) — written by the\n"
                "Wikibooks contributors. Wiki markup was removed and ingredient lists and steps re-arranged. They are shared under the\n"
                "Creative Commons Attribution-ShareAlike 4.0 International licence: https://creativecommons.org/licenses/by-sa/4.0/\n"
                "The source page, revision and authors of every recipe are listed at the top of its shard file; the page history\n"
                "on Wikibooks lists the individual authors.\n")
    return man


def main():
    args = sys.argv[1:]
    if any(a in ("-h", "--help") for a in args):
        print(__doc__)
        return
    cache, only = "build/tarif-cache", ["tr", "en", "de"]
    if "--cache" in args:
        i = args.index("--cache"); cache = args[i + 1]; del args[i:i + 2]
    if "--only" in args:
        i = args.index("--only"); only = args[i + 1].split(","); del args[i:i + 2]
    out = args[0] if args else "build/tarif-lib"
    os.makedirs(cache, exist_ok=True)
    recipes = []
    for lang in only: recipes += build_site(lang, cache)
    man = write(out, recipes)
    if UNMAPPED: log("unmapped German cuisines: " + ", ".join(f"{k} ({v})" for k, v in sorted(UNMAPPED.items())))
    print(" ".join(f"{k}={v}" for k, v in man.items()))


if __name__ == "__main__":
    main()
