# Plan 3 — Ağ uygulamaları: Hava durumu, Masa saati, Döviz & Altın, Gazete

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Spec §2 hedef 4, 6 (Masa saati, Döviz & Altın) ve 8'i (Gazete) kpm paketleri olarak yayınlamak.

**Architecture:** Ortak ağ katmanı `core/net` (curl alt süreci, sınırlı tampon, zaman aşımı; küçük XML/HTML
ayrıştırıcı; gömülü cJSON) ve `kui`'ye metin kaydırma + Türkçe tarih yardımcıları eklenir. Her uygulamanın
saf mantığı (ayrıştırma, biçimlendirme, kayıtlı veri) ayrı dosyada ve host testli; `main.c` yalnızca çizim/olay.

**Tech Stack:** C11, FBInk, cJSON 1.7.18 (MIT, `core/vendor/cjson`), `/usr/bin/curl` (cihazda TLS çalışıyor, spec §5).

**Spec:** `docs/superpowers/specs/2026-10-18-kindle-apps-design.md` (§2 hedef 4/6/8, §6.2, §6.3 Hava/Saat/Döviz).

## Global Constraints

- Ekran kuralı: hamle/güncellemede tam flaş yok; yalnızca değişen bölge (`kui_mark_dirty` + `kui_flush(0)`).
- Açık renkli metin yalnızca `kui_text_on` ile (kui.h notu, Plan 2 Task 6 kararı).
- Veriler `/mnt/us/kindle-apps/data/<app>/`; çevrimdışıyken son veri zaman damgasıyla gösterilir.
- Ağ isteği: `curl -fsSL --max-time 20 --connect-timeout 10`, kabuk yok (execv), yanıt ≤ 2 MB; UI asla süresiz beklemez.
- Gazete: yalnızca RSS'in verdiği içerik + kaynak bağlantısı; görsel indirilmez.
- Paket sürümü `[2026, 10, 6]`; kimlikler `kindle-apps-weather` (order 30), `-clock` (31), `-currency` (32), `-news` (33).

## Kararlar (benim; spec builder'a bıraktı)

- Altın kaynağı: `https://finans.truncgil.com/today.json` (anahtarsız; gram, çeyrek, yarım, tam, cumhuriyet,
  ons). Erişilemezse altın bölümü gizlenir (spec). Döviz TCMB'den.
- Gazete kaynakları: **ikisi birden** (AA ve TRT Haber sekmesi). Kategoriler: AA güncel/gündem/dünya/ekonomi/spor/
  bilim-teknoloji/sağlık/kültür; TRT manşet/gündem/dünya/ekonomi/spor/bilim_teknoloji/sağlık/kultur_sanat.
  Metin: TRT `content:encoded` varsa HTML'den düz metne, yoksa `description`.
- Hava ikonları DejaVu glifleri: ☀ ☁ ☂ ❄ ⚡ ☽; ⛅ fontta yok → ☀ + ☁ üst üste.
- Saat dilimi: `localtime()`; cihazda saat dilimi tanımsızsa (gmtoff = 0 ve TZ boş) ilk şehrin Open-Meteo
  `utc_offset_seconds` değeri kullanılır.
- Masa saati uyku engelini önceki değeri kaydederek koyar (`/var/local/kindle-apps/prevent_ss.restore`);
  `kui-run.sh` geri getirme adımı bu dosya varsa değeri geri yazar (çökmede de pil tükenmez).

## Review Focus

1. Ağ yok / DNS hatası / 20 sn'de yanıt yok → çökme ya da donma değil, önbellek + "çevrimdışı" notu.
2. Bozuk/kesik JSON-XML (yarım indirilmiş akış) → ayrıştırıcı sınır dışı okumaz, eksik alan "—".
3. Uzun Türkçe metin (çok uzun kelime, URL) kaydırmada taşmaz, sayfalar arası kayıp/tekrar satır yok.
4. Şehir aramasında Türkçe karakterler URL'de doğru kodlanır (İ, ş, ğ), sonuç yoksa mesaj.
5. Saat uygulaması uyku sırasında ekrana çizmez, uyanınca doğru saati gösterir, çıkınca uyku ayarı eski hâline döner.

---

### Task 1: `core/net` — HTTP, XML/HTML, cJSON; `kui` metin kaydırma ve Türkçe tarih

**Files:** Create `core/net/net.h`, `core/net/http.c`, `core/net/xml.c`, `core/kui/wrap.c`, `core/kui/trtime.c`,
`core/vendor/cjson/*`; Modify `core/kui/kui.h`, `Makefile`. Tests: `tests/host/test_http.c` (+ `tests/host/fake_curl.sh`),
`test_xml.c`, `test_wrap.c`, `test_trtime.c`.

**Interfaces (Produces):**
- `int net_get(const char *url, char **body, int *len, int timeout_s)` — 0 ok (body malloc'lu, NUL ile biter), -1 hata.
  curl yolu `NET_CURL` ortam değişkeni (test), varsayılan `/usr/bin/curl`. Yanıt 2 MB'ı aşarsa -1.
- `void net_urlencode(const char *in, char *out, int cap)` (RFC 3986 unreserved dışında %XX, UTF-8 baytları).
- `const char *xml_next(const char *p, const char *end, const char *tag, XmlEl *el)` — `XmlEl { const char *attrs, *attrs_end, *inner, *inner_end; }`;
  `<tag ...>…</tag>` ve `<tag/>`; döner: elemanın sonrası ya da NULL. `int xml_attr(const XmlEl*, const char *name, char *out, int cap)`.
  `int xml_text(const char *s, const char *e, char *out, int cap)` — CDATA açar, entity çözer (`&amp; &lt; &gt; &quot; &apos; &#N; &#xH; &nbsp;`), kırpar.
  `int html_to_text(const char *s, const char *e, char *out, int cap)` — etiketleri atar; `p, br, h1-h6, li, div` → satır sonu,
  boşlukları teke indirir, `script/style/figure/footer` içeriğini atar.
- `int kui_wrap(const char *text, int width, int (*measure)(const char *s, int n, void *ctx), void *ctx, KuiLine *out, int max)` —
  `KuiLine { int off, len; }`; `\n` zorunlu satır, boşlukta kır, sığmayan kelimeyi kod noktasında kır; döner satır sayısı.
  `int kui_wrap_text(const char *text, int width, KuiFont f, int px, KuiLine *out, int max)` cihaz sürümü (FBInk ölçümü).
- `const char *kui_tr_weekday(int wday)` ("Pazar"…), `kui_tr_month(int mon0)` ("Ocak"…), `kui_tr_weekday_short` ("Paz"…),
  `void kui_tr_date(const struct tm*, char *out, int cap)` → "18 Ekim 2026, Pazar".
- `void kui_fmt_num(double v, int decimals, char *out, int cap)` → "6.632,39" (Türkçe binlik nokta, ondalık virgül; negatif "-").

- [ ] Step 1: Testleri yaz: http (başarı gövdesi, curl çıkış 22 → -1, fake 30 sn uyur + timeout 1 → ≤ 3 sn'de -1, 3 MB → -1, argv'de
  `--max-time`, URL tek argüman), urlencode ("Maltepe İstanbul" → "Maltepe%20%C4%B0stanbul"), xml (TCMB örneği, CDATA, kendinden
  kapanan, kesik belge → NULL, iç içe aynı ad), html_to_text, wrap (sabit genişlikli ölçüyle: boşluk kırma, `\n`, uzun kelime,
  UTF-8 bölünmez, max sınırı), trtime, fmt_num. `make test` → derleme hatasıyla FAIL.
- [ ] Step 2: Uygula; `make test` → hepsi OK. cJSON `-Werror` dışında derlenir.
- [ ] Step 3: Commit `feat(net): curl HTTP, XML/HTML text, cJSON; kui text wrap and Turkish dates`.

### Task 2: Hava durumu

**Files:** Create `apps/weather/wx.c/h`, `apps/weather/main.c`, `apps/weather/pkg/*`; Test `tests/host/test_wx.c` + `tests/host/fixtures/om_*.json`.

**Interfaces (Produces, Task 3 kullanır):**
- `typedef struct { char name[64], admin[64]; double lat, lon; } WxCity;` `int wx_cities_load(const char *path, WxCity *c, int max)`
  (dosya yoksa İstanbul/Maltepe 40.935, 29.155 tek kayıt), `int wx_cities_save(path, c, n)`; satır biçimi `ad|bölge|lat|lon`.
- `typedef struct { int ok; long fetched; int utc_offset; double temp, feels, wind; int hum, wind_dir, code, is_day;
  int n_hours; struct { char hh[6]; double t; int code, pop; } hour[12]; int n_days;
  struct { char date[11]; int code; double tmax, tmin; int pop; char sunrise[6], sunset[6]; } day[7]; } Wx;`
- `int wx_parse(const char *json, Wx *out)`, `void wx_forecast_url(const WxCity*, char *out, int cap)`,
  `int wx_geocode_parse(const char *json, WxCity *out, int max)`, `void wx_geocode_url(const char *query, char *out, int cap)`,
  `const char *wx_code_text(int code)` (WMO → Türkçe: "Açık", "Parçalı bulutlu", "Sisli", "Çisenti", "Yağmurlu", "Kar", "Sağanak", "Gök gürültülü"…),
  `int wx_code_icon(int code, int is_day)` (WX_ICON_SUN, _MOON, _PARTLY, _CLOUD, _FOG, _RAIN, _SNOW, _STORM),
  `const char *wx_wind_dir(int deg)` ("K","KD","D","GD","G","GB","B","KB").
- Önbellek: `data/weather/city<N>.json` (ham yanıt) + `fetched` için dosya mtime; ilk şehir `city0.json` (Task 3 okur).

- [ ] Step 1: test_wx: om fixture (gerçek yanıt) → temp 13.2, code 61 "Yağmurlu", 12 saat, 7 gün, utc_offset 10800; eksik `daily` →
  ok ama n_days 0; bozuk → -1; geocode fixture → Maltepe/İstanbul; cities load/save gidiş-dönüş, `|` içeren ad reddedilir;
  wind 0→"K", 225→"GB", 359→"K". FAIL.
- [ ] Step 2: wx.c uygula → OK.
- [ ] Step 3: main.c — ekran: üstte şehir adı (◀ ▶ ile şehir değiştir), büyük ikon + sıcaklık + durum, hissedilen/rüzgâr/nem/
  yağış, bugün en yüksek/düşük, 12 saatlik şerit, 7 gün listesi, alt satır "Güncellendi 16:30" veya "Çevrimdışı — son veri …".
  Üst çubuk sağ düğme "Şehirler": liste (dokun = seç, "Sil" uzun basma ile onay diyaloğu, "+ Şehir ekle" → klavyeyle arama →
  sonuç listesi), en fazla 10 şehir. "Yenile" düğmesi.
- [ ] Step 4: Commit `feat(weather): Open-Meteo weather with saved cities`.

### Task 3: Masa saati

**Files:** Create `apps/clock/main.c`, `apps/clock/pkg/*`; Modify `core/kui-run.sh` (prevent_ss.restore); Test `tests/device/test_clock_restore.sh`.

- [ ] Step 1: Uygula: büyük SS:DD (bold ~300 px), tarih (`kui_tr_date`), altta ilk şehrin hava özeti (wx_parse `city0.json`),
  dakikada bir yalnız saat bölgesi kısmi yenileme, her saat başı tam yenileme, 30 dk'da bir hava yenileme (sessiz, başarısızsa eski).
  Açılışta `preventScreenSaver` eski değeri dosyaya yazılır ve 1 yapılır; çıkışta geri; `kui-run.sh` restore da yapar.
- [ ] Step 2: Commit `feat(clock): desk clock with weather summary`.

### Task 4: Döviz & Altın

**Files:** Create `apps/currency/fx.c/h`, `apps/currency/main.c`, `apps/currency/pkg/*`; Test `tests/host/test_fx.c` + fixtures.

**Interfaces:** `typedef struct { char code[4], name[48]; int unit; double buy, sell; } FxRate;`
`int fx_parse_tcmb(const char *xml, FxRate *out, int max, char date[11])` (ForexBuying/Selling; boşsa Banknote; JPY unit 100 dahil
birim başına çevrilmez, unit saklanır); `typedef struct { char key[24], label[32]; double buy, sell; } FxGold;`
`int fx_parse_gold(const char *json, FxGold *out, int max)` (Türkçe sayı "6.632,39" → 6632.39; anahtarlar gram-altin, ceyrek-altin,
yarim-altin, tam-altin, cumhuriyet-altini, ons); `double fx_parse_trnum(const char*)`; `double fx_convert(double amount, const FxRate *from, const FxRate *to)`
(TRY sahte kur 1/1; satış kuruyla TRY'ye, alış kuruyla yabancıya).

- [ ] Step 1: test_fx: TCMB fixture → USD 49.3573/49.4463, JPY unit 100, tarih "16.10.2026"; truncgil fixture → gram 6632.39; bozuk → 0 kayıt;
  100 USD→TRY = 4944.63; fx_parse_trnum("%0,90") yok sayılır. FAIL.
- [ ] Step 2: fx.c → OK.
- [ ] Step 3: main.c — liste: USD, EUR, GBP, CHF, JPY, (diğerleri "Tümü" sayfasında); altın bölümü; "Çevirici" düğmesi: kaynak/hedef
  para seç, tutar ekran klavyesiyle (rakam sayfası), sonuç. Önbellek `data/currency/tcmb.xml`, `gold.json`.
- [ ] Step 4: Commit `feat(currency): TCMB rates, gold prices and converter`.

### Task 5: Gazete

**Files:** Create `apps/news/news.c/h`, `apps/news/main.c`, `apps/news/pkg/*`; Test `tests/host/test_news.c` + RSS fixtures.

**Interfaces:** `typedef struct { char title[256], link[256], date[32]; char *text; } NewsItem;`
`int news_parse_rss(const char *xml, int len, NewsItem *out, int max)` (text malloc'lu; `content:encoded` > `description`; img yok);
`void news_free(NewsItem*, int n)`; `typedef struct { const char *source, *label, *url; } NewsFeed; const NewsFeed *news_feeds(int *n)`;
`void news_date_short(const char *rfc822, char *out, int cap)` → "18 Eki 16:04".

- [ ] Step 1: test_news: AA fixture → 30 öğe, ilk başlık "Türk Yıldızları, Kayseri'de gösteri uçuşu yaptı"; TRT fixture → content:encoded
  düz metin, `<img` yok, © satırı korunur; kesik XML → tam öğeler; date_short. FAIL.
- [ ] Step 2: news.c → OK.
- [ ] Step 3: main.c — üstte kaynak sekmeleri AA | TRT, kategori düğmeleri; başlık listesi (sayfa ◀ ▶, kaydırma); dokununca okuma
  ekranı: başlık, tarih, kaydırılmış metin sayfaları (◀ ▶ / kaydır), en altta "Kaynak: <link>". Önbellek `data/news/<feed>.xml`.
- [ ] Step 4: Commit `feat(news): AA and TRT Haber RSS reader`.

### Task 6: Paketleme ve yayın

- [ ] `PKGS`'e weather clock currency news; test_pack 10 paket; README/THIRD_PARTY (cJSON MIT, Open-Meteo CC-BY 4.0 atıf,
  TCMB, Truncgil, AA/TRT); depo betiği; sürüm `[2026,10,6]`; `make build dist` OK; cihaz erişilebilirse `make deploy` + ekran görüntüleri,
  değilse kullanıcı dönüşünde. `main`'e merge, `v2026.10.6`.
