# Kindle Apps — Tasarım Belgesi

Tarih: 2026-10-18 · Sahibi: Kaan Dikeç (GitHub: `dikeckaan`)

## 1. Amaç ve neden

Jailbreak'li bir Kindle (11. nesil, MT8110 "Bellatrix", FW 5.19.2, `kindlehf` ABI) için,
**Kindle ana ekranından (kitaplıktan) açılan**, KOReader'a bağımlı olmayan, e-ink'i yormayan
sade uygulamalardan oluşan bir paket. Hepsi tek bir GitHub deposunda durur ve Kindle'a
KMC'nin paket yöneticisi `kpm` ile tek komutla kurulabilir.

Mevcut topluluk uygulamaları (GNOME Games vb.) ya bu cihazda çalışmıyor (libGL yok), ya
X/GTK üzerinden çiziliyor ve ekran yenilemesi kontrol edilemiyor, ya da KOReader içinde
yaşıyor. Kullanıcı: "ana ekrandan açılsın", "mümkün olduğunca az refreshli olsun".

## 2. Hedefler

Kullanıcıyla kararlaştırılanlar (kullanıcının kararı):

1. **Satranç** — standart kurallar. Açılışta mod sorar: *Tek oyunculu* (bilgisayara karşı) veya
   *İki oyunculu* (aynı cihazda, çevrimdışı). Tek oyunculuda zorluk sorar; "kaliteli"
   zorluk seviyeleri olmalı.
2. **2048** — klasik 4×4 kurallar.
3. **Türkçe Wordle** — klasik 5 harf, 6 tahmin, **sınırsız oyun** (bitince yenisi).
4. **Hava durumu** — anahtarsız, herkese açık ama sağlam veri. **Kayıtlı şehirler**; ilk açılışta
   varsayılan **İstanbul / Maltepe**.
5. **SSH sunucusu** — arka planda çalışır; **kitaplıktan aç/kapa** yeterli (açılışta
   otomatik başlama YOK).
6. Kullanıcının onayladığı ek fikirler (hepsi):
   - **Masa saati** (büyük saat + tarih + hava özeti, dakikada bir titreşimsiz güncelleme)
   - **Döviz & altın** (TCMB günlük kurları + basit çevirici)
   - **Mayın Tarlası**
   - **Solitaire** (Klondike)
   - **Sudoku** (3 zorluk, ipucu/hata gösterme)
   - **Sistem bilgisi** (pil, boş alan, IP, sıcaklık vb.)
   - **Wi-Fi ile dosya yükleme** (tarayıcıdan Kindle'a kitap atma)
7. **Tek GitHub reposu**, Kindle'a kurulabilir (kpm deposu olarak).
8. **Gazete** (kullanıcının sonradan eklediği): Anadolu Ajansı veya TRT Haber'den haberler
   (herkese açık RSS; anahtarsız). Kategoriler, başlık listesi, dokununca okunabilir metin,
   çevrimdışı okumak için önbellek. Kaynak seçimi (AA/TRT, ikisi birden) ve metin çıkarımı
   builder'a bırakıldı; telif için yalnızca kaynakların RSS'te verdiği içerik + kaynak bağlantısı
   gösterilir, görsel indirilmez.
10. **Uyku Ayarı** (kullanıcının sonradan eklediği: "sleep modunu kapatabileceğimiz (uzatabileceğimiz)
    bir araç"): Kindle'ın ekran koruyucu/uyku süresini seçtirir — 5 dk, 10 dk, 30 dk, 1 saat,
    2 saat, Hiç uyuma. powerd `screenSaverTimeout` (rw, sn; FW 5.19.2'de varsayılan 600) ve
    `preventScreenSaver` (rw) lipc özellikleriyle. Mevcut değeri gösterir; "Hiç uyuma"da pil
    uyarısı. Yeniden başlatmada kalıcı mı — spike'ta ölçülür, kalıcı değilse ekranda yazılır
    (kalıcı yapmak için rootfs'e yazılmaz).
9. **Tek uygulama çatısı — "Uygulamalar"** (kullanıcının kararı: "KOReader gibi bir app içinde
   toplayalım"): kitaplıkta **tek giriş**. Açınca simge ızgaralı ana menü; bir uygulamadan
   çıkınca menüye, menüden çıkınca Kindle'a dönülür. Kindle arayüzü bu süre boyunca duraklatılmış
   kalır (uygulama geçişlerinde ana ekran gidip gelmez). SSH aç/kapa da menüdeki bir uygulamadır.
   Uygulamalar menüye kayıt dosyasıyla (`/mnt/us/kindle-apps/apps/<id>/app.ini`: ad, sıra,
   ikili) eklenir; kurulu olmayan görünmez — benim kararım.

**Ortak ekran kuralı (kullanıcının kararı, tüm uygulamalar):** her hamlede tam ekran yenileme
(siyah flaş) YOK. Yalnızca değişen bölge hızlı/flaşsız kipte güncellenir. Tam yenileme sadece
yeni oyunda/ekran değişiminde ve gölgelenme (ghosting) birikince nadiren yapılır. Eşik ayarlardan
değiştirilebilir/kapatılabilir.

## 3. Hedef olmayanlar

- KOReader eklentisi olmak (kullanıcı açıkça istemedi).
- Çevrimiçi çok oyunculu (satranç dahil); Lichess vb.
- Açılışta kendiliğinden başlayan servisler; rootfs'e (`/`, `/etc`, `/usr`) kalıcı yazma.
- Başka Kindle modelleri/ABI'ler için garanti (kindlehf dışı "olursa iyi", hedef değil).
- Animasyon, gri tonlu geçişler, ses.

## 4. Anti-hedefler (çalışsa bile başarısızlık sayılır)

- **Cihazı brick etmek ya da kullanıcıyı donmuş ekranla bırakmak.** Uygulama çökse bile Kindle
  arayüzü geri gelmeli.
- Rootfs'i yazılabilir yapmak, sistem dosyalarını değiştirmek.
- Her dokunuşta ekranın yanıp sönmesi.
- Ağdaki herkese parolasız root erişimi açan bir SSH (kullanıcı bilgilendirilmeden).
- Uygulamadan çıkış yolunun belirsiz olması.

## 5. Kısıtlar ve cihaz gerçekleri (keşifle doğrulandı)

- Cihaz: `Linux 4.9.77-lab126 armv7l`, `/lib/ld-linux-armhf.so.3` (hard-float), ekran
  1072×1448, 8 bpp gri, stride 1072 (`virtual_size 1072,2896`). Rootfs `ro`, %96 dolu.
- Kullanıcı alanı `/mnt/us` (FAT benzeri `fuse.fsp`: **symlink yok**, Unix izinleri güvenilmez).
- Jailbreak araçları: KMC (`/var/local/kmc/bin/{kpm,fbink,su,...}`), `kpm v0.2.2`.
  Scriptletler `/mnt/us/documents/*.sh` olarak kitaplıkta görünür; root için
  `/var/local/kmc/bin/su -c` kullanılır (bkz. `/var/local/kmc/sbin/kpm.sh`).
- Kindle arayüzü X (Xorg + `awesome` WM + `pillow`) üzerinde çalışır. KOReader tam ekran
  çizmek için: pillow'u kapatır, `awesome`'u `SIGSTOP` eder, `statusbar` işini durdurur,
  `volumd`'u `SIGSTOP` eder; çıkışta tersini yapar ve `app://com.lab126.booklet.home`'u
  başlatır (kaynak: `koreader/platform/kindle/koreader.sh`). Aynı yaklaşım izlenecek.
- Güvenlik duvarı: varsayılan `INPUT DROP`; açılacak portlar için çalışma anında
  `iptables -I INPUT ... -j ACCEPT` eklenip çıkışta silinir.
- Sistemde `curl`, `wget` var; TLS çalışıyor. `python3` yok.
- Geliştirme makinesi macOS arm64 + Docker; araç zinciri `koxtoolchain 2026.08 kindlehf`
  (x86_64 Linux, Docker `--platform linux/amd64` içinde).

## 6. Nasıl yapılacak — kararlar

Kullanıcı "nasıl"ı bana bıraktı; aşağıdakiler **benim kararım** (kullanıcıya bildirildi:
"C + FBInk, uygulama açıkken Kindle arayüzünü duraklat").

### 6.1 Teknoloji

- **Dil: C (C11)**, `kindlehf` araç zinciriyle derlenir; mümkün olduğunca statik.
- **Ekran: FBInk** (NiLuJe, master — MTK/Bellatrix desteği master'da), statik kütüphane,
  `KINDLE=1`, OpenType açık. Dokunmatik giriş: `/dev/input/event*` doğrudan okunur
  (çoklu dokunma protokol B), `EVIOCGRAB` ile Kindle arayüzünden alınır.
- **Font:** Türkçe karakterleri ve satranç glifleri (U+2654–265F) içeren açık lisanslı TTF
  (ör. DejaVu Sans / DejaVu Sans Bold) pakete gömülür.
- **JSON:** cJSON (MIT) depoya gömülü. **HTTP:** sistemin `/usr/bin/curl`'ü alt süreçle.
- **Lisans:** FBInk GPLv3+ ve Stockfish GPLv3 olduğundan **depo GPLv3**.

### 6.2 Ortak çatı: `kui` kütüphanesi + başlatıcı

Tüm uygulamalar ortak bir C kütüphanesi (`kui`) ve ortak bir başlatıcı betiği kullanır.

- **Başlatıcı (`kui-run.sh`)**: Kindle arayüzünü duraklatır (6. bölümdeki KOReader adımları),
  uygulamayı çalıştırır, **her durumda** (normal çıkış, çökme, sinyal) `trap` ile arayüzü
  geri getirir. Çökmede ekrana kısa hata mesajı basar ve günlüğü
  `/mnt/us/kindle-apps/logs/<app>.log`'a yazar.
- **Ekran yenileme politikası (kui içinde merkezi):**
  - Bölgesel güncelleme: değişen dikdörtgen(ler) için hızlı, flaşsız dalga formu
    (siyah-beyaz içerik için DU/A2 benzeri; gri içerik için GL16 benzeri — MTK'de hangi
    modların geçerli olduğu spike ile doğrulanacak).
  - Tam yenileme (GC16 flaş): ekran değişiminde/yeni oyunda ve sayaç eşiğinde
    (varsayılan 25 bölgesel güncelleme; ayarlanabilir, 0 = asla).
- **Bileşenler:** üst çubuk (başlık + her zaman sol üstte **Çıkış/✕**, sağda menü),
  düğme, ızgara, liste, diyalog (evet/hayır, seçim), **Türkçe Q ekran klavyesi**
  (Ç Ğ I İ Ö Ş Ü dahil; Türkçe büyük/küçük harf kuralları: i↔İ, ı↔I), ayarlar ekranı.
- **Güç düğmesi / uyku:** `lipc-wait-event` ile `com.lab126.powerd` olayları dinlenir;
  uyanınca (`outOfScreenSaver`) ekran tam yeniden çizilir. Saat uygulaması açıkken uyku
  engellenir (`com.lab126.powerd preventScreenSaver`), çıkışta geri alınır.
- **Kalıcı veri:** `/mnt/us/kindle-apps/data/<app>/` altında düz metin/JSON
  (kayıtlı oyun, istatistik, ayar, şehirler).

### 6.3 Uygulamalar

**Satranç**
- Tam kurallar: rok, geçerken alma, terfi (seçim diyaloğu), şah/mat/pat, 50 hamle,
  üçlü tekrar, yetersiz materyal. Geri al. Kapatınca yarım oyun kaydedilir, açınca
  "Devam / Yeni oyun" sorar. Dokun-seç, dokun-oynat; seçili taş ve son hamle belirgin.
- İki oyunculu modda tahta döndürme seçeneği.
- Motor: **Stockfish**, kaynaktan `kindlehf` için derlenir. Boyut/hız için NNUE'siz klasik
  değerlendirmeli bir sürüm (Stockfish 11) tercih edilir. 6 seviye (Türkçe adlar):
  Çok Kolay, Kolay, Orta, İyi, Zor, Usta. Alt seviyeler `UCI_LimitStrength`/`UCI_Elo` ve
  `Skill Level` ile insan benzeri hatalar yapar; Usta tam güç, süre sınırlı.
  Seviye→ayar eşlemesi builder'a bırakıldı; hedef: Çok Kolay yeni başlayana yenilebilir,
  Usta güçlü kulüp oyuncusunu zorlar, her hamle ≤ ~5 sn.

**2048** — klasik kurallar; kaydırma ve/veya ekran okları; skor, en yüksek skor, geri al
(1 adım); oyun durumu kaydedilir.

**Türkçe Wordle**
- 5 harf, 6 tahmin, sınırsız. Türkçe Q klavyesi; harf durumları e-ink'e uygun desenle:
  doğru yer = siyah zemin/beyaz harf, yanlış yer = çizgili/gri desen, yok = boş/üstü çizili;
  klavyede de aynı işaretleme.
- **Yalnızca yalın (sözlük kökü) kelimeler** geçerlidir — hem tahminler hem cevaplar
  (kullanıcının kararı; ekli hâller ör. "evler", "gitti" kabul edilmez).
  Geçerli tahmin listesi = 5 harfli sözlük kökleri; cevap listesi = bunların yaygın olanları
  (frekans listesinde yalın hâliyle geçenler, özel adlar/kısaltmalar hariç).
  Şapkalı harfler düz harfe katlanır (â→a, î→i, û→u) — benim kararım.
  İstatistik: oynanan, kazanma %, seri, tahmin dağılımı.
- **Kelime listesi lisanslı kaynaklardan derlenir** (lisanssız/izinsiz listeler kullanılmaz):
  ör. LibreOffice/Hunspell `tr_TR` sözlüğü (MPL-2.0) kökleri ve hermitdave
  *FrequencyWords* Türkçe frekans listesi (CC-BY-SA 4.0). Derleme betiği depoda;
  atıflar `THIRD_PARTY.md`'de.

**Hava durumu**
- Kaynak: **Open-Meteo** (anahtarsız; forecast + geocoding API). Türkçe arayüz.
- Kayıtlı şehirler; ilk açılış: İstanbul / Maltepe (yaklaşık 40.935 K, 29.155 D).
  Şehir ekle (klavyeyle ara → listeden seç), sil, sırala.
- Ekran: şu anki sıcaklık + durum (büyük), hissedilen, rüzgâr, nem, bugün en yüksek/düşük,
  yağış olasılığı, 12 saatlik şerit, 7 günlük tahmin; sade siyah-beyaz ikonlar.
- Açılışta ve "Yenile"de çeker; çevrimdışıysa son veriyi zaman damgasıyla gösterir.

**SSH Sunucusu** (aç/kapa uygulaması)
- Dokununca durumu gösterir (açık/kapalı, IP:port) ve aç/kapa yapar. Sunucu uygulamadan
  bağımsız arka planda çalışır; yeniden başlatmada kapalı başlar.
- Dropbear kaynaktan derlenir; host anahtarları `/mnt/us/kindle-apps/data/ssh/`.
- **Kimlik doğrulama:** ağdaki herkese parolasız root olmaması için ilk açılışta uygulamaya
  özel bir parola belirlenir (ekran klavyesi). Rootfs'teki `/etc/shadow`'a dokunulmaz;
  bu nedenle parolanın dropbear'a nasıl doğrulatılacağı (uygulamaya özel parola dosyası için
  küçük bir dropbear yaması ya da benzeri) builder'a bırakıldı. `authorized_keys`
  (anahtarla giriş) de desteklenir. Port: 22 boşsa 22, değilse 2222 (KOReader'ın SSH'ı
  22'yi kullanıyor olabilir). Gerekli iptables kuralı açarken eklenir, kapatırken silinir.

**Masa saati** — büyük saat (SS:DD), tarih (Türkçe gün/ay), altında kayıtlı ilk şehrin hava
özeti (hava uygulamasının verisini paylaşır). Dakikada bir yalnızca saat bölgesi
flaşsız güncellenir; saatte bir tam yenileme. Açıkken uyku engellenir.

**Döviz & Altın** — kaynak: TCMB `https://www.tcmb.gov.tr/kurlar/today.xml` (resmî,
anahtarsız). USD, EUR, GBP, CHF, JPY vb. alış/satış; altın için anahtarsız güvenilir bir
kaynak builder tarafından seçilir (yoksa altın satırı gizlenir). Basit çevirici
(sayı tuş takımı). Son veri önbelleklenir, tarihiyle gösterilir.

**Mayın Tarlası** — 3 zorluk; dokun = aç, uzun bas = bayrak (veya bayrak kipi düğmesi);
ilk dokunuş güvenli.

**Solitaire (Klondike)** — 1/3 kart çekme seçeneği, dokun-seç/dokun-bırak, geri al,
otomatik tamamlama; kart sembolleri yüksek kontrastlı.

**Sudoku** — Kolay/Orta/Zor, üreteç tek çözümlü bulmaca üretir; not (kalem) kipi,
hata gösterme açılıp kapanabilir, geri al, kayıt.

**Sistem bilgisi** — pil % ve şarj durumu, boş/toplam alan (`/mnt/us`), Wi-Fi IP, firmware,
model, çalışma süresi, sıcaklık (varsa), güvenlik duvarı durumu. "Yenile" düğmesi.

**Wi-Fi ile dosya yükleme** — açıkken küçük bir HTTP sunucusu (ör. port 8080) çalıştırır,
ekranda `http://<ip>:8080` ve tek kullanımlık 4-6 haneli PIN gösterir. Tarayıcıdan dosya
seçilip yüklenir → `/mnt/us/documents/`. Yalnızca uygulama açıkken çalışır; iptables kuralı
açarken eklenir, çıkışta silinir. Desteklenen uzantılar: epub, pdf, mobi, azw3, txt, cbz vb.;
yol geçişine (`../`) karşı korumalı.

### 6.4 Paketleme ve dağıtım

- Depo: `github.com/dikeckaan/kindle-apps` (**herkese açık** — kpm'in indirebilmesi için;
  benim kararım).
- Her uygulama bir **kpm paketi** (`.kpkg` = tar.gz: `manifest.json` (v2), `install.sh`,
  `uninstall.sh`, `launch.sh`), `supported_platforms: ["kindlehf"]`; uygulama paketleri
  scriptlet koymaz, `apps/<id>/app.ini` ile menüye kaydolur.
  Ortak çalışma zamanı (font, `kui-run.sh`, **"Uygulamalar" menüsü** ve tek scriptlet
  `/mnt/us/documents/uygulamalar.sh`) çekirdek paket `kindle-apps-core`'dur; diğerleri ona
  bağımlıdır. Kurulum yalnızca `/mnt/us` altına yazar.
- Depo manifesti (`manifest.json`, KMC v2 biçimi — bkz. `repo.kindlemodding.org/manifest.v2.json`)
  ve paketler **GitHub Pages** üzerinden yayınlanır; **GitHub Actions** her etikette
  Docker içinde kindlehf araç zinciriyle derler, paketler ve Pages'e yükler.
- Kullanıcı kurulumu: `kpm add-repo https://dikeckaan.github.io/kindle-apps/manifest.json`,
  `kpm update`, `kpm install <paketler>`; ayrıca kitaplıktan çalıştırılabilen bir
  "depoyu ekle ve hepsini kur" scriptlet'i Releases'te yayınlanır. README Türkçe.
- Yerel geliştirme: `make` (Docker ile derle), `make deploy KINDLE=192.168.2.90`
  (SSH ile kopyala).

## 7. Doğrulama

- Saf mantık (satranç kuralları — perft testleri, 2048, Wordle değerlendirme, Sudoku üreteci,
  Mayın, Solitaire kuralları, JSON/XML ayrıştırma) **macOS/Linux üzerinde birim testleriyle**
  test edilir (çizimden bağımsız katman).
- Cihazda: her uygulama açılır, ekran görüntüsü (`/dev/fb0` dökümü) ile doğrulanır,
  çıkışta Kindle arayüzünün geri geldiği kontrol edilir; çökme senaryosunda başlatıcının
  arayüzü geri getirdiği test edilir.
- Yenileme politikası cihazda gözle + günlükle doğrulanır.

## 8. İlk adım: risk spike'ı

Tasarımın dayandığı üç varsayım ilk iş cihazda doğrulanır (atılabilir spike):
1. FBInk master `kindlehf`/MTK'de çiziyor; hangi dalga formları flaşsız çalışıyor.
2. Dokunmatik aygıt yolu, koordinat dönüşümü, `EVIOCGRAB` çalışıyor.
3. Arayüzü duraklat/geri getir dizisi FW 5.19.2'de sorunsuz (çökme senaryosu dahil).
Sonuçlar bu belgeye eklenir; bir varsayım çökerse tasarım güncellenip kullanıcıya bildirilir.

### Spike sonuçları (2026-10-18)

- **FBInk** master `886f25f` (`tools/deps.lock`), statik `libfbink.a`, `KINDLE=1`, tam özellik
  (OpenType dahil). Tanıma: "Kindle Basic 4 (2AP → 0x957, Cava on Bellatrix)", `is_mtk=1`,
  fb `hwtcon_v2`, 1072×1448 Y8, rota 3, 300 dpi. Türkçe + satranç glifleri DejaVu Sans ile doğru.
- **Dalga formları:** `WFM_DU, A2, GL16, AUTO, REAGL` hepsi `fbink_refresh` ret=0 döndü.
  Kullanıcı flaş gözlemi alınamadı → **karar (benim):** siyah-beyaz bölgesel = `WFM_DU`,
  gri içerikli bölgesel = `WFM_GL16`, ikisi de `is_flashing=false`; tam yenileme =
  `WFM_GC16` + `is_flashing=true`. 2048 cihaz testinde gözle doğrulanacak.
- **Dokunmatik:** `/dev/input/event1` adı `pt_mt`, çoklu dokunma protokol B
  (`ABS_MT_TRACKING_ID`, `ABS_MT_POSITION_X/Y`), aralık X 0..1071, Y 0..1447 = **ekran
  koordinatıyla birebir** (döndürme/takas yok; FBInk `touch_swap/mirror` = 0).
  Koordinatlar `SYN_REPORT`'ta okunmalı (TRACKING_ID önce gelir). `EVIOCGRAB` çalışıyor —
  **ancak KOReader açıksa aygıtı o tutar (GRAB=-1)**; kui başlarken GRAB başarısızsa
  kullanıcıya "KOReader'ı kapatın" mesajı göstermeli.
- **Güç tuşu:** `/dev/input/event0` `bd71828-pwrkey` (KEY_POWER) — uyku powerd'de kalır,
  biz yalnız lipc olaylarını dinleriz.
- **Duraklat/geri getir:** pillow disable + `SIGSTOP awesome` + `stop statusbar` +
  `SIGSTOP volumd` ve tersi FW 5.19.2'de sorunsuz; çıkışta ana ekran geri geldi.
- **Aygıt erişimi notu:** KOReader kapalıyken SSH yalnızca geçici bağımsız dropbear
  (port 2222, RAM'de) üzerinden.

## 9. Builder'a bırakılanlar

Kod yapısı ve modül ayrımı; bileşen görünüm detayları ve yerleşim; Stockfish seviye
eşlemesi (6.3'teki hedeflerle); ikon çizimi; altın veri kaynağı; SSH parola doğrulama
mekanizması (6.3'teki kısıtlarla); paket adları; test çatısı; CI ayrıntıları.
