#!/usr/bin/env python3
"""Builds the Şiir Defteri anthology (apps/siir/data/poems.txt + SOURCES) from Turkish Wikisource.

Only poets whose death year (read from their Kişi: page on tr.wikisource.org) is more than 70 years ago are used:
in Türkiye (FSEK art. 27) a work enters the public domain 70 years after the end of the year its author died.
A page is skipped when it is a translation (çevirmen set), carries a copyright/deletion template, is scan-based
with any transcluded page not yet proofread (quality < 3, "transcription in progress"), is an image, is longer than
MAX_LINES lines or shorter than MIN_LINES, or still contains characters outside modern Turkish spelling (scholarly
transliterations such as ḳ ḫ ñ ʿ) or wiki markup after cleaning.

Output format (UTF-8, one record per "@" line, tab-separated fields):
  @A <author id> <name> <years> <note>
  @P <author id> <title> <source url>
  <poem lines; an empty line is a stanza break>

Usage: tools/siir/build-poems.py [outdir] [--cache DIR]   (needs network; --cache keeps the API answers)
"""
import datetime, hashlib, html, json, os, re, sys, urllib.parse, urllib.request

API = "https://tr.wikisource.org/w/api.php"
UA = "kindle-apps-siir/1.0 (https://github.com/dikeckaan/kindle-apps; build-poems.py)"
MAX_LINES, MIN_LINES = 120, 4
PD_YEAR = datetime.date.today().year  # works of authors who died before PD_YEAR - 70 are public domain

# Wikisource categories to read; each page's author comes from its own header (eser sahibi / yazar).
CATEGORIES = [
    "Yunus Emre", "Karacaoğlan türküleri", "Karacaoğlan koşmaları", "Karacaoğlan varsağıları", "Karacaoğlan", "Karacaoğlan eserleri",
    "Pir Sultan Abdal eserleri", "Pir Sultan Abdal'ın deyişleri", "Kaygusuz Abdal şiirleri", "Şah Hatayi gazelleri", "Şah Hatayi'nin deyişleri",
    "Kul Himmet'in deyişleri", "Fuzûlî gazelleri", "Bâkî gazelleri", "Nedim gazelleri", "Nefi gazelleri", "Nabi gazelleri", "Şeyhî gazelleri",
    "Mihrî Hatun gazelleri", "Seyyid Nesimi eserleri", "Eşrefoğlu Rumî şiirleri", "Eşrefoğlu Rûmî şiirleri", "Niyâzî-i Mısrî şiirleri",
    "Erzurumlu Emrah şiirleri", "Dadaloğlu türküleri", "Ruhsati deyişleri", "Ruhsati'nin deyişleri", "Sümmânî türküleri", "Namık Kemal şiirleri",
    "Ziya Paşa Şiirleri", "Ziya Paşa gazelleri", "Şinâsî şiirleri", "Tevfik Fikret şiirleri", "Abdülhak Hamit Tarhan şiirleri",
    "Ahmet Haşim şiirleri", "Mehmet Âkif Ersoy şiirleri", "Cenab Şahabeddin şiirleri", "Mehmet Emin Yurdakul şiirleri",
    "Ali Ekrem Bolayır şiirleri", "Celâl Sahir Erozan şiirleri", "Ziya Gökalp Şiirleri", "Ömer Seyfettin şiirleri",
    "Rıza Tevfik Bölükbaşı Şiirleri", "Neyzen Tevfik şiirleri", "Orhan Veli şiirleri", "Sabahattin Ali şiirleri", "Rüştü Onur şiirleri",
    "Muhyiddin Abdal eserleri", "Sürûrî'nin şiirleri", "Virani eserleri", "Yemini eserleri", "Harabi eserleri", "Sıdkı Baba eserleri",
    "Agahi eserleri", "Şiirler", "Gazeller", "Koşmalar", "Semailer", "Türkçe şiirler", "Nefesler", "Kasideler", "Şarkılar (Divan şiiri)",
    "Bektaşi Şairleri ve Nefesleri", "Kul Nesimi'nin deyişleri", "Kul Hüseyin'in deyişleri", "Abdürrezzâk Nevres şiirleri",
    "Vatandan bahseden şiirler", "İstanbul'dan bahseden şiirler", "Ölümden bahseden şiirler", "Aşktan bahseden şiirler",
    "Tevhide Hanım gazelleri", "Rubailer",
]

# id, display name, Kişi page, header spellings, cap (poems kept), note (written for the app: who, era)
AUTHORS = [
    ("yunus", "Yunus Emre", "Kişi:Yunus Emre", ["Yunus Emre"], 20, "Anadolu'da Türkçe tasavvuf şiirinin öncüsü; ilahileri sade bir halk diliyle yazdı."),
    ("kaygusuz", "Kaygusuz Abdal", "Kişi:Kaygusuz Abdal", ["Kaygusuz Abdal"], 10, "Abdal Musa'nın dervişi; Bektaşî şiirinin ilk büyük adlarından."),
    ("nesimi", "Seyyid Nesîmî", "Kişi:Seyyid Nesimi", ["Seyyid Nesimi", "Nesimi", "Nesîmî"], 8, "Hurufî şair; Azerbaycan Türkçesiyle yazan divan şiirinin erken ustası."),
    ("seyhi", "Şeyhî", "Kişi:Şeyhî", ["Şeyhî", "Şeyhi"], 8, "15. yüzyıl başında Germiyan ve Osmanlı saraylarında yaşayan divan şairi ve hekim."),
    ("esrefoglu", "Eşrefoğlu Rûmî", "Kişi:Eşrefoğlu Rumî", ["Eşrefoğlu Rumî", "Eşrefoğlu Rûmî"], 8, "Kadirî tarikatının Anadolu'daki kurucusu; Yunus yolunda ilahiler yazdı."),
    ("necati", "Necâtî", "Kişi:Necati", ["Necati", "Necâtî"], 6, "15. yüzyıl divan şairi; atasözleri ve halk deyişleriyle bezeli gazelleriyle tanınır."),
    ("hatayi", "Şah Hatâyî", "Kişi:Şah Hatayi", ["Şah Hatayi", "Hatayi", "Hatâyî"], 8, "Safevî hükümdarı Şah İsmail'in mahlası; Alevî-Bektaşî deyişlerinin ustası."),
    ("pirsultan", "Pir Sultan Abdal", "Kişi:Pir Sultan Abdal", ["Pir Sultan Abdal", "Pir Sultan"], 12, "Sivaslı Alevî ozanı; deyişleri hâlâ sazla söylenir."),
    ("fuzuli", "Fuzûlî", "Kişi:Fuzûlî", ["Fuzûlî", "Fuzuli", "Fuzûli"], 14, "Bağdat'ta yaşayan 16. yüzyıl divan şairi; aşk ve ıstırap şiirinin zirvesi."),
    ("kanuni", "Muhibbî (Kanuni Sultan Süleyman)", "Kişi:Kanuni Sultan Süleyman", ["Kanuni Sultan Süleyman"], 6, "Osmanlı padişahı I. Süleyman; Muhibbî mahlasıyla gazeller yazdı."),
    ("baki", "Bâkî", "Kişi:Bâkî", ["Bâkî", "Baki"], 12, "16. yüzyılın 'sultanü'ş-şuarâ'sı; İstanbul Türkçesinin zarif divan şairi."),
    ("karacaoglan", "Karacaoğlan", "Kişi:Karacaoğlan", ["Karacaoğlan"], 18, "17. yüzyıl Toroslu halk ozanı; doğa ve sevgiliyi yalın koşmalarla söyledi."),
    ("kulhimmet", "Kul Himmet", "Kişi:Kul Himmet", ["Kul Himmet"], 8, "Pir Sultan geleneğinden Alevî ozanı."),
    ("nefi", "Nef'î", "Kişi:Nefi", ["Nefi", "Nef'î", "Nefî"], 6, "Erzurumlu 17. yüzyıl şairi; kasideleri ve keskin hicivleriyle ünlü."),
    ("nabi", "Nâbî", "Kişi:Nâbi", ["Nâbi", "Nabi", "Nâbî"], 8, "Urfalı 17. yüzyıl şairi; öğüt veren 'hikemî' şiirin temsilcisi."),
    ("nedim", "Nedîm", "Kişi:Nedim", ["Nedim", "Nedîm"], 12, "Lâle Devri'nin şairi; İstanbul'u, eğlenceyi ve şarkıyı divan şiirine taşıdı."),
    ("dadaloglu", "Dadaloğlu", "Kişi:Dadaloğlu", ["Dadaloğlu"], 8, "Avşar boyunun ozanı; göçebe Türkmenlerin sesi."),
    ("emrah", "Erzurumlu Emrah", "Kişi:Erzurumlu Emrah", ["Erzurumlu Emrah"], 8, "Divan şiirinden de beslenen âşık; ince koşmalarıyla bilinir."),
    ("ruhsati", "Ruhsatî", "Kişi:Ruhsatî", ["Ruhsati", "Ruhsatî"], 8, "Sivaslı 19. yüzyıl âşığı; hayatı ve inancı yalın deyişlerle söyledi."),
    ("summani", "Sümmânî", "Kişi:Sümmani", ["Sümmani", "Sümmânî"], 6, "Erzurumlu 19. yüzyıl âşığı; Gülperi hikâyesiyle tanınır."),
    ("sinasi", "İbrahim Şinâsî", "Kişi:İbrahim Şinâsî", ["İbrahim Şinâsî", "Şinasi", "Şinâsî"], 6, "Tanzimat yazarı ve gazeteci; yeni Türk edebiyatının öncülerinden."),
    ("ziyapasa", "Ziya Paşa", "Kişi:Ziya Paşa", ["Ziya Paşa"], 8, "Tanzimat şairi ve devlet adamı; 'Terkib-i Bend'iyle bilinir."),
    ("namikkemal", "Namık Kemal", "Kişi:Namık Kemal", ["Namık Kemal"], 10, "Vatan ve hürriyet şairi; Tanzimat edebiyatının en etkili sesi."),
    ("ekrem", "Recaizade Mahmud Ekrem", "Kişi:Recaizade Mahmut Ekrem", ["Recaizade Mahmut Ekrem", "Recaizade Mahmud Ekrem"], 6, "Tanzimat'ın ikinci kuşağından şair ve eleştirmen; Servet-i Fünûn'un hocası."),
    ("hamit", "Abdülhak Hâmid Tarhan", "Kişi:Abdülhak Hamit Tarhan", ["Abdülhak Hamit Tarhan", "Abdülhak Hâmid Tarhan"], 8, "'Şair-i Âzam'; Tanzimat'tan Cumhuriyet'e uzanan coşkulu bir şiir."),
    ("fikret", "Tevfik Fikret", "Kişi:Tevfik Fikret", ["Tevfik Fikret"], 14, "Servet-i Fünûn'un önderi; aruzu konuşma diline yaklaştıran şair."),
    ("cenab", "Cenab Şahabeddin", "Kişi:Cenab Şahabeddin", ["Cenab Şahabeddin"], 8, "Servet-i Fünûn şairi; renk ve ses imgeleriyle dolu sembolist şiir."),
    ("aliekrem", "Ali Ekrem Bolayır", "Kişi:Ali Ekrem Bolayır", ["Ali Ekrem Bolayır"], 6, "Namık Kemal'in oğlu; Servet-i Fünûn döneminde şiir ve oyun yazdı."),
    ("celalsahir", "Celâl Sahir Erozan", "Kişi:Celâl Sahir Erozan", ["Celâl Sahir Erozan", "Celal Sahir Erozan"], 6, "'Aşk ve kadın şairi' olarak tanınır; Türk Dil Kurumu'nun kurucularından."),
    ("akif", "Mehmet Âkif Ersoy", "Kişi:Mehmet Âkif Ersoy", ["Mehmet Akif Ersoy", "Mehmet Âkif Ersoy"], 18, "İstiklâl Marşı'nın şairi; 'Safahat'ta toplumu konuşur gibi anlatan aruz ustası."),
    ("haşim", "Ahmed Hâşim", "Kişi:Ahmet Haşim", ["Ahmet Haşim", "Ahmed Hâşim"], 14, "Türk sembolizminin şairi; akşamı, havuzu ve renkleri ses gibi yazdı."),
    ("yurdakul", "Mehmet Emin Yurdakul", "Kişi:Mehmet Emin Yurdakul", ["Mehmet Emin Yurdakul"], 8, "'Türk Şairi'; hece ölçüsü ve sade Türkçeyle milli şiirin öncüsü."),
    ("gokalp", "Ziya Gökalp", "Kişi:Ziya Gökalp", ["Ziya Gökalp"], 8, "Sosyolog ve düşünür; Türkçülüğü şiirlerinde de işledi."),
    ("omerseyfettin", "Ömer Seyfettin", "Kişi:Ömer Seyfettin", ["Ömer Seyfettin"], 6, "Hikâyeleriyle tanınan Milli Edebiyat yazarı; az sayıda şiiri de var."),
    ("rustuonur", "Rüştü Onur", "Kişi:Rüştü Onur", ["Rüştü Onur"], 6, "Genç yaşta veremden ölen şair; yoksulluğu ve ölümü yalın bir dille yazdı."),
    ("rizatevfik", "Rıza Tevfik Bölükbaşı", "Kişi:Rıza Tevfik Bölükbaşı", ["Rıza Tevfik Bölükbaşı"], 6, "Filozof ve şair; halk şiiri biçimlerini aydın şiirine taşıdı."),
    ("sabahattinali", "Sabahattin Ali", "Kişi:Sabahattin Ali", ["Sabahattin Ali"], 6, "Romancı ve öykücü; hapishane ve özlem şiirleri türkü olmuştur."),
    ("orhanveli", "Orhan Veli Kanık", "Kişi:Orhan Veli Kanık", ["Orhan Veli Kanık", "Orhan Veli"], 14, "Garip akımının kurucusu; şiiri ölçü ve uyaktan kurtarıp gündelik dile açtı."),
    ("neyzen", "Neyzen Tevfik", "Kişi:Neyzen Tevfik", ["Neyzen Tevfik"], 6, "Ney üstadı ve hiciv şairi; Bodrumlu, dervişane ve aykırı."),
    ("muhyiddin", "Muhyiddin Abdal", "Kişi:Muhyiddin Abdal", ["Muhyiddin Abdal"], 6, "16. yüzyıl Bektaşî şairi; Hacı Bektaş geleneğinden nefesler."),
    ("virani", "Virânî", "Kişi:Virani", ["Virani", "Virânî"], 6, "Bektaşî şairi; Hurufî öğretiyi nefeslerine işledi."),
    ("teslim", "Teslim Abdal", "Kişi:Teslim Abdal", ["Teslim Abdal"], 6, "Bektaşî halk ozanı."),
    ("sururi", "Sürûrî", "Kişi:Sürûrî", ["Sürûrî", "Sururi"], 6, "16. yüzyıl Kızılbaş şairi; Şah İsmail çevresinde yaşadı."),
    ("yemini", "Yeminî", "Kişi:Yemini", ["Yemini", "Yeminî"], 6, "Bektaşî şairi; 'Fazîletnâme'nin yazarı."),
    ("harabi", "Harâbî", "Kişi:Harabi", ["Harabi", "Harâbî"], 8, "19. yüzyıl İstanbullu Bektaşî şairi; aruz ve heceyi birlikte kullandı."),
    ("sidki", "Sıdkı Baba", "Kişi:Sıdkı Baba", ["Sıdkı Baba"], 6, "19.–20. yüzyıl Bektaşî babası ve şairi."),
    ("kulnesimi", "Kul Nesîmî", "Kişi:Kul Nesimi", ["Kul Nesimi"], 6, "Alevî-Bektaşî halk ozanı."),
    ("kulhuseyin", "Kul Hüseyin", "Kişi:Kul Hüseyin", ["Kul Hüseyin"], 6, "Sivas Şarkışlalı Alevî ozanı ve dedesi."),
    ("kulmustafa", "Kul Mustafa", "Kişi:Kul Mustafa", ["Kul Mustafa"], 6, "Halk ozanı; Genç Osman destanıyla bilinir."),
    ("agahi", "Âgâhî", "Kişi:Agahi", ["Agahi", "Âgâhî"], 6, "19. yüzyıl sonu ile 20. yüzyıl başında yaşamış halk şairi."),
    ("kadiburhan", "Kadı Burhaneddin", "Kişi:Kadı Burhaneddin", ["Kadı Burhaneddin"], 6, "14. yüzyıl Sivas hükümdarı ve şairi; tuyuğlarıyla tanınır."),
    ("ahmedpasa", "Ahmed Paşa", "Kişi:Ahmed Paşa", ["Ahmed Paşa"], 6, "Fatih devrinin şairi ve veziri; İran şiirinden nazireleriyle bilinir."),
    ("fitnat", "Fıtnat Hanım", "Kişi:Fıtnat Hanım", ["Fıtnat Hanım"], 6, "18. yüzyıl İstanbullu kadın divan şairi; nükteli gazelleriyle anılır."),
    ("leyla", "Leylâ Hanım", "Kişi:Leylâ Hanım", ["Leylâ Hanım"], 6, "Besteci, yazar ve şair; şarkı güfteleri ve gazelleriyle tanınır."),
    ("tevhide", "Tevhide Hanım", "Kişi:Tevhide Hanım", ["Tevhide Hanım"], 6, "19. yüzyıl kadın divan şairi."),
    ("hayreti", "Hayretî", "Kişi:Hayretî", ["Hayretî"], 6, "16. yüzyıl Vardarlı divan şairi; tasavvufi ve rindane gazeller."),
    ("garip", "Âşık Garip", "Kişi:Âşık Garip", ["Aşık Garip"], 6, "Halk hikâyelerinin âşığı; koşmaları sazla söylenir."),
    ("zati", "Zâtî", "Kişi:Zâtî", ["Zâtî"], 6, "16. yüzyıl Balıkesirli divan şairi; Kanuni devrinin üretken gazelcisi."),
]

# Poets whose Kişi: page gives no death year: the century they lived in (as their Kişi page or the standard
# literary histories date them) and a death year no later than which they are known to have died.
ERA = {
    "kaygusuz": ("14.–15. yüzyıl", 1500), "hatayi": ("1487–1524", 1524), "pirsultan": ("16. yüzyıl", 1600),
    "kulhimmet": ("16. yüzyıl", 1700), "emrah": ("19. yüzyıl", 1900), "virani": ("16.–17. yüzyıl", 1700),
    "yemini": ("16. yüzyıl", 1600), "kulnesimi": ("17. yüzyıl", 1700), "dadaloglu": ("19. yüzyıl", 1900),
    "kulhuseyin": ("19. yüzyıl", 1930), "teslim": ("17. yüzyıl", 1700), "kulmustafa": ("17. yüzyıl", 1700),
}

TR_OK = set("abcçdefgğhıijklmnoöprsştuüvyzâîûôêABCÇDEFGĞHIİJKLMNOÖPRSŞTUÜVYZÂÎÛÔÊqwxQWXéèëäÉ0123456789 .,;:!?'\"()-–—…«»“”‘’/*&")


def api(cache, **p):
    p.update(format="json", formatversion="2")
    key = urllib.parse.urlencode(sorted(p.items()))
    if cache:
        f = os.path.join(cache, hashlib.sha1(key.encode()).hexdigest() + ".json")
        if os.path.exists(f):
            return json.load(open(f, encoding="utf-8"))
    req = urllib.request.Request(API + "?" + key, headers={"User-Agent": UA})
    data = json.load(urllib.request.urlopen(req, timeout=60))
    if cache:
        json.dump(data, open(f, "w", encoding="utf-8"), ensure_ascii=False)
    return data


def category_pages(cache, cat):
    out, cont = [], {}
    while True:
        r = api(cache, action="query", list="categorymembers", cmtitle="Kategori:" + cat, cmlimit=500, cmtype="page", cmnamespace=0, **cont)
        out += [m["title"] for m in r["query"]["categorymembers"]]
        if "continue" not in r:
            return out
        cont = r["continue"]


def fetch_pages(cache, titles, extra="info|templates"):
    res = {}
    for i in range(0, len(titles), 50):
        r = api(cache, action="query", prop="revisions|" + extra, rvprop="content|ids", rvslots="main", tllimit=500, inprop="url",
                titles="|".join(titles[i:i + 50]))
        for p in r["query"]["pages"]:
            if "revisions" in p:
                res[p["title"]] = p
    return res


def template_end(s, i):
    """index just past the {{...}} starting at s[i]"""
    depth = 0
    while i < len(s):
        if s.startswith("{{", i):
            depth += 1
            i += 2
        elif s.startswith("}}", i):
            depth -= 1
            i += 2
            if depth == 0:
                return i
        else:
            i += 1
    return len(s)


def header_fields(text):
    m = re.search(r"\{\{\s*[Ee]ser(?: başlığı)?\s*\|", text)
    if not m:
        return {}, text
    end = template_end(text, m.start())
    body = text[m.start() + 2:end - 2]
    fields = {}
    for part in re.split(r"\|(?![^\[]*\]\])(?![^{]*\}\})", body)[1:]:
        if "=" in part:
            k, v = part.split("=", 1)
            fields[k.strip().lower().replace("_", " ")] = v.strip()
    return fields, text[:m.start()] + text[end:]


def plain_link(s):
    s = re.sub(r"\[\[(?:[^|\]]*\|)?([^\]]*)\]\]", r"\1", s)
    return s.strip()


FORMAT_TPL = {"orta", "center", "ortala", "sola hizala", "sağa hizala", "smaller", "small", "larger", "large", "x-larger", "xx-larger",
              "bb", "büyük", "küçük", "metin girintisi", "sağa yasla", "c", "right", "left", "bold", "nowrap", "sc", "kk", "çsy",
              "çift satır yüksekliği", "font"}


HEADING_TPL = {"orta", "center", "ortala", "sola hizala", "sağa hizala", "sağa yasla", "c", "right", "left"}
HEADING = "\x01"


def expand_templates(s, title):
    out, i = [], 0
    while i < len(s):
        if s.startswith("{{", i):
            j = template_end(s, i)
            inner = s[i + 2:j - 2]
            parts = [p for p in re.split(r"\|(?![^\[]*\]\])", inner)]
            name = parts[0].strip().lower()
            if name == "pagename":
                out.append(title)
            elif name in FORMAT_TPL and len(parts) > 1:
                pos = [p for p in parts[1:] if "=" not in p.split("[[")[0]]
                if name in HEADING_TPL:
                    out.append(HEADING)  # marks a centred or aligned line: a heading or signature, not verse
                out.append(expand_templates(pos[-1] if pos else "", title))
            elif name in ("söz", "poem"):
                out.append(expand_templates("|".join(parts[1:]), title))
            i = j
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def section(text, name, begin, end):
    """the part of a Sayfa page inside section `name` (begin/end markers selected independently)"""
    if begin:
        m = re.search(r'<section\s+begin="?%s"?\s*/>' % re.escape(name), text)
        if m:
            text = text[m.end():]
    if end:
        m = re.search(r'<section\s+end="?%s"?\s*/>' % re.escape(name), text)
        if m:
            text = text[:m.start()]
    return text


def transclude(cache, text, why):
    """replaces <pages index=… from=… to=…/> with the Sayfa pages' text; None if any page is not proofread"""
    tags = list(re.finditer(r"<pages\s+([^>]*?)/?>", text))
    if not tags:
        return text
    need = []
    for m in tags:
        a = dict((k, v.strip('"')) for k, v in re.findall(r'(\w+)\s*=\s*("[^"]*"|[^\s/>]+)', m.group(1)))
        fr, to = int(a.get("from", 0) or 0), int(a.get("to", 0) or a.get("from", 0) or 0)
        if not a.get("index") or not fr or to < fr or to - fr > 12:
            why.append("scan range")
            return None
        need.append((m, a, fr, to))
    titles = ["Sayfa:%s/%d" % (a["index"], n) for _, a, fr, to in need for n in range(fr, to + 1)]
    pages = fetch_pages(cache, titles, extra="proofread")
    out, last = [], 0
    for m, a, fr, to in need:
        chunk = []
        for n in range(fr, to + 1):
            p = pages.get("Sayfa:%s/%d" % (a["index"], n))
            if not p or p.get("proofread", {}).get("quality", 0) < 3:
                why.append("not proofread")
                return None
            t = p["revisions"][0]["slots"]["main"]["content"]
            t = re.sub(r"<noinclude>.*?</noinclude>", "", t, flags=re.S)
            if a.get("onlysection"):
                t = section(t, a["onlysection"], True, True)
            else:
                if n == fr and a.get("fromsection"):
                    t = section(t, a["fromsection"], True, False)
                if n == to and a.get("tosection"):
                    t = section(t, a["tosection"], False, True)
            chunk.append(t)
        out.append(text[last:m.start()])
        out.append("\n".join(chunk))
        last = m.end()
    out.append(text[last:])
    return "".join(out)


def tr_lower(s):
    return s.replace("I", "ı").replace("İ", "i").lower()


def fold(s):
    """letters only, lower case, circumflexes dropped: for comparing titles and first lines"""
    s = tr_lower(s).replace("â", "a").replace("î", "i").replace("û", "u")
    return re.sub(r"[^a-zçğıöşü]", "", s)


TR_ALPHABET = "abcçdefgğhıijklmnoöprsştuüvyz"


def tr_key(s):
    """sort key in Turkish alphabetical order (circumflexes folded, punctuation ignored)"""
    s = tr_lower(s).replace("â", "a").replace("î", "i").replace("û", "u")
    return [TR_ALPHABET.index(c) for c in s if c in TR_ALPHABET]


def tidy_title(t):
    t = re.sub(r"^(?:Gazel|Şarkı|Kaside)\s*:\s*", "", t)
    t = re.sub(r"\s+", " ", t).strip()
    if t == t.upper() and len(t) > 3:  # "GURBET HAPİSHANESİ" → "Gurbet Hapishanesi"
        t = " ".join(w[:1] + tr_lower(w[1:]) for w in t.split(" "))
    return t[:1].replace("i", "İ").upper() + t[1:] if t else t


# a form name standing above the poem ("Gazel", "-Koşma-") is a heading, not verse
FORMS = {"gazel", "kaside", "kaside", "şarkı", "koşma", "nefes", "deyiş", "murabba", "semai", "varsağı", "ilahi", "rubai", "tuyuğ", "kıta", "müfred"}

# pages left out by hand: why
EXCLUDE = {
    "Ben Sana": "Neyzen Tevfik's coarse lampoon; not in keeping with the anthology",
    "Siham-ı Kaza'dan": "Nef'î's invective (hicv) of insults; not in keeping with the anthology",
    "Ey Adem Oğlu": "attributed to Mehmet Âkif on Wikisource but not in Safahat; reads as prose, not his verse",
}


def clean(text, title, aliases, why):
    t = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    t = re.sub(r"<ref[^>/]*/>|<ref[^>]*>.*?</ref>", "", t, flags=re.S)
    t = re.sub(r"<section[^>]*/>", "", t)
    poems = re.findall(r"<poem[^>]*>(.*?)</poem>", t, flags=re.S)
    if poems:
        t = "\n\n".join(poems)
    else:
        m = re.search(r"\{\{\s*söz\s*\|", t, flags=re.I)
        if m:
            t = t[m.start():template_end(t, m.start())]
    t = re.split(r"\n\s*----", t)[0]
    t = re.sub(r"\[\[\s*(?:Kategori|Category|Dosya|File|Resim)\s*:[^\]]*\]\]", "", t, flags=re.I)
    t = expand_templates(t, title)
    t = re.sub(r"<br\s*/?>[ \t]*\n?", "\n", t, flags=re.I)
    t = re.sub(r"</?(?:p|div)[^>]*>", "\n", t, flags=re.I)
    t = re.sub(r"<[^>]+>", "", t)
    t = plain_link(t)
    t = re.sub(r"\[https?://\S+\s+([^\]]*)\]", r"\1", t)
    t = re.sub(r"\[\d+\]", "", t)
    t = t.replace("'''", "").replace("''", "")
    t = html.unescape(t).replace("\u00a0", " ")
    t = re.sub("[\u200b\u200e\u200f\u00ad]", "", t)
    t = re.sub("[\u00b4`\u02bc]", "\u2019", t).replace("\u2015", "\u2014")
    lines, heads = [], []
    for ln in t.split("\n"):
        heads.append(HEADING in ln)
        ln = re.sub(r"^[:*#\s]+", "", ln.replace(HEADING, "")).rstrip()
        ln = re.sub(r"\s+", " ", ln)
        lines.append(ln)
    # drop a repeated title at the top and the poet's signature at the end
    norm = lambda s: re.sub(r"[^\wçğıöşüâîû]", "", s.lower().replace("İ", "i").replace("I", "ı"))

    def heading(i):
        """leading line that is not verse: a centred heading (the title again, a form name such as "— Minbernâme —"),
        an anthology's poem number ("— 4 —"), or the title typed above a blank line"""
        ln = lines[i]
        if not ln or re.fullmatch(r"[-—–\s\dIVXLC.]*|[-—–].*[-—–]", ln) or fold(ln) in FORMS:
            return True
        if heads[i] and (norm(ln) == norm(title) or re.fullmatch(r"[-—–].*[-—–]", ln)):
            return True
        return norm(ln) == norm(title) and i + 1 < len(lines) and not lines[i + 1]

    while lines and heading(0):
        lines.pop(0)
        heads.pop(0)
    # a whole anthology page transcluded without sections may carry the next poem ("— 5 —"): stop there
    for i, ln in enumerate(lines):
        if i and re.fullmatch(r"[-—–]\s*\d+\s*[-—–]", ln):
            del lines[i:]
            break
    while lines and not lines[-1]:
        lines.pop()
    if lines and any(norm(lines[-1]) == norm(a) for a in aliases):
        lines.pop()
    out = []
    for ln in lines:
        if ln or (out and out[-1]):
            out.append(ln)
    while out and not out[-1]:
        out.pop()
    body = "\n".join(out)
    bad = sorted(set(c for c in body if c not in TR_OK and c != "\n"))
    if bad:
        why.append("chars " + "".join(bad[:8]))
        return None
    if any(c in body for c in "{}[]<>|=@\t"):
        why.append("markup")
        return None
    n = sum(1 for ln in out if ln)
    if n > MAX_LINES:
        why.append("long %d" % n)
        return None
    if n < MIN_LINES:
        why.append("short %d" % n)
        return None
    return body


def kisi_years(page):
    t = page["revisions"][0]["slots"]["main"]["content"] if page else ""
    def g(k):
        m = re.search(r"\|\s*%s\s*=\s*([^|\n]*)" % k, t)
        return m.group(1).strip() if m else ""
    return g("doğumyılı"), g("ölümyılı"), "Telifli yazar" in t


def years_text(b, d):
    y = lambda s: re.sub(r"\s+", " ", s.replace("?", "").strip())
    b, d = y(b), y(d)
    if b and d:
        return "%s–%s" % (b, d)
    return "ö. %s" % d if d else ""


def death_number(d):
    m = re.findall(r"\d{3,4}", d)
    return int(m[-1]) if m else None


def main():
    args = sys.argv[1:]
    cache = None
    if "--cache" in args:
        i = args.index("--cache")
        cache = args[i + 1]
        del args[i:i + 2]
        os.makedirs(cache, exist_ok=True)
    outdir = args[0] if args else "apps/siir/data"
    by_alias = {}
    for a in AUTHORS:
        for al in a[3]:
            by_alias[al] = a
    kisi = fetch_pages(cache, [a[2] for a in AUTHORS])
    authors = {}
    for a in AUTHORS:
        b, d, telifli = kisi_years(kisi.get(a[2]))
        dn = death_number(d)
        era = None
        if dn is None and a[0] in ERA:
            era, dn = ERA[a[0]]
        if telifli or dn is None or dn + 70 >= PD_YEAR:
            print("skip author %s: death %r%s" % (a[1], d, " (Telifli yazar)" if telifli else ""), file=sys.stderr)
            continue
        authors[a[0]] = {"a": a, "years": era or years_text(b, d), "death": dn, "era": bool(era), "poems": []}
    titles = []
    for c in CATEGORIES:
        for t in category_pages(cache, c):
            if t not in titles:
                titles.append(t)
    pages = fetch_pages(cache, titles)
    skipped = {}
    for t in titles:
        p = pages.get(t)
        if not p:
            continue
        text = p["revisions"][0]["slots"]["main"]["content"]
        f, rest = header_fields(text)
        who = plain_link(f.get("eser sahibi") or f.get("esersahibi") or f.get("yazar") or "")
        a = by_alias.get(who)
        if not a or a[0] not in authors:
            continue
        why = []
        tpl = " ".join(x["title"] for x in p.get("templates", []))
        if plain_link(f.get("çevirmen", "") or f.get("çeviren", "")):
            why.append("translation")
        elif re.search(r"Telifli|Telif ihlali|Silinme|Hızlı sil|Copyvio", tpl):
            why.append("copyright template")
        elif re.search(r"\.(png|jpe?g|gif)", text, re.I) and "<poem" not in text:
            why.append("image")
        if not why:
            rest = transclude(cache, rest, why)
        title = plain_link(expand_templates(f.get("başlık", ""), t)) or t
        title = re.sub(r"\s*\((?:%s)\)\s*$" % "|".join(re.escape(x) for x in a[3]), "", title).strip()
        title = tidy_title(title)
        if t in EXCLUDE:
            why.append("excluded")
        body = clean(rest, title, a[3], why) if not why else None
        if body is None:
            skipped.setdefault(why[0].split(" ")[0], []).append(t + " [" + why[0] + "]")
            continue
        first_line = lambda b: next((fold(ln) for ln in b.split("\n") if fold(ln)), "")
        bare = re.sub(r"\s*\([^)]*\)\s*$", "", t)
        if fold(bare) and first_line(body).startswith(fold(bare)) and not first_line(body).startswith(fold(title)):
            title = tidy_title(bare)  # the header names another poem (copied from a neighbouring page); the page name is its incipit
        key, lines_set = first_line(body), set(fold(ln) for ln in body.split("\n") if fold(ln))
        def same(q):
            other = set(fold(ln) for ln in q["body"].split("\n") if fold(ln))
            return first_line(q["body"]) == key or len(lines_set & other) * 2 > min(len(lines_set), len(other))
        if any(same(q) for q in authors[a[0]]["poems"]):
            skipped.setdefault("duplicate", []).append(t)
            continue
        lic = re.findall(r"KM-[\wÇçİıĞğÖöŞşÜü-]+|Telif bilgisiz", tpl + " " + text)
        authors[a[0]]["poems"].append({"title": title, "page": t, "url": p.get("fullurl"), "rev": p["revisions"][0]["revid"], "body": body,
                                       "lic": sorted(set(lic))})
    total, out, src = 0, [], []
    out.append("# Şiir Defteri — public-domain Turkish poems from tr.wikisource.org, generated by tools/siir/build-poems.py.")
    out.append("# See SOURCES for each poem's page and revision. Format: @A id name years note / @P author title url / lines.")
    for a in sorted(AUTHORS, key=lambda a: authors[a[0]]["death"] if a[0] in authors else 0):  # by era
        if a[0] not in authors or not authors[a[0]]["poems"]:
            continue
        info = authors[a[0]]
        # a stable, unbiased choice: short and medium poems first (one or two pages), then by a hash of the title
        ps = sorted(info["poems"], key=lambda p: (p["body"].count("\n") > 60, hashlib.sha1(p["title"].encode()).hexdigest()))[:a[4]]
        ps.sort(key=lambda p: tr_key(p["title"]))
        out.append("@A\t%s\t%s\t%s\t%s" % (a[0], a[1], info["years"], a[5]))
        if info["era"]:
            src.append("%s (%s; no death year on Wikisource, died by %d: public domain)" % (a[1], info["years"], info["death"]))
        else:
            src.append("%s (%s; died %d, public domain in Türkiye since %d)" % (a[1], info["years"], info["death"], info["death"] + 71))
        for p in ps:
            out.append("@P\t%s\t%s\t%s" % (a[0], p["title"], p["url"]))
            out.append(p["body"])
            src.append("  %s — %s (revision %d)%s" % (p["title"], p["url"], p["rev"], "; page tags: " + ", ".join(p["lic"]) if p["lic"] else ""))
            total += 1
        print("%-28s %3d of %3d" % (a[1], len(ps), len(info["poems"])), file=sys.stderr)
    for k, v in sorted(skipped.items()):
        print("skipped %-18s %d" % (k, len(v)), file=sys.stderr)
        if os.environ.get("SIIR_VERBOSE"):
            for x in v:
                print("    " + x, file=sys.stderr)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "poems.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(out) + "\n")
    with open(os.path.join(outdir, "SOURCES"), "w", encoding="utf-8") as fh:
        fh.write("Şiir Defteri — poem sources\n\n")
        fh.write("Every poem in poems.txt was taken from Turkish Wikisource (https://tr.wikisource.org) on %s by\n" % datetime.date.today())
        fh.write("tools/siir/build-poems.py and only reformatted (wiki markup removed, stanza breaks kept, spelling as on Wikisource).\n")
        fh.write("All poets died more than 70 years ago (death years from their Kişi: pages on Wikisource), so under Turkish law\n")
        fh.write("(FSEK art. 27: protection ends 70 years after the end of the year of the author's death) the poems are in the\n")
        fh.write("public domain in Türkiye. Translations and pages marked as copyrighted were excluded; scan-based pages were only\n")
        fh.write("used when every transcluded page is proofread. The author notes were written for this app.\n")
        fh.write("Wikisource's own page layout and transcription work is offered under CC BY-SA 4.0: this file credits each page.\n\n")
        fh.write("\n".join(src) + "\n")
    print("%d poems, %d authors" % (total, sum(1 for a in authors.values() if a["poems"])), file=sys.stderr)


if __name__ == "__main__":
    main()
