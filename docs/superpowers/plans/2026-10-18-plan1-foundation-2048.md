# Plan 1 — Temel (kui + başlatıcı + paketleme) ve 2048 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kitaplıktaki tek "Uygulamalar" girişinden açılan menü ve içinde flaşsız güncellenen bir 2048 uygulamasını; bütün diğer uygulamaların
kullanacağı ortak çatı (`kui`), güvenli başlatıcı, Docker derleme ve kpm deposu üzerinden
GitHub'dan kurulabilir hâlde uçtan uca teslim etmek.

**Architecture:** Her uygulama iki katman: saf mantık (`apps/<app>/<app>_logic.c`, host'ta
birim testli) ve ekran katmanı (`apps/<app>/main.c`, `kui` üzerinden FBInk ile çizer, evdev'den
dokunma okur). `core/kui-run.sh` Kindle arayüzünü duraklatıp uygulamayı çalıştırır ve her
çıkışta geri getirir. `tools/` paketleri (`.kpkg`) ve kpm depo manifestini üretir; GitHub Actions
derleyip Pages'e yayınlar.

**Tech Stack:** C11, FBInk (master, statik, `KINDLE=1 OPENTYPE=1`), koxtoolchain 2026.08
`kindlehf` (Docker, `kindle-apps-tc` imajı — `tools/docker/Dockerfile`), DejaVu Sans TTF,
POSIX sh, GitHub Actions + Pages.

**Spec:** `docs/superpowers/specs/2026-10-18-kindle-apps-design.md`

**Sonraki planlar (bu planın dışı):** Plan 2 Satranç (+Stockfish) · Plan 3 Wordle (+Türkçe
klavye) · Plan 4 Hava, Masa saati, Döviz, Gazete · Plan 5 Mayın, Solitaire, Sudoku · Plan 6 SSH,
Sistem bilgisi, Wi-Fi yükleme. Hepsi bu planın `kui` arayüzlerini tüketir.

## Global Constraints

- Hedef: kindlehf (`/lib/ld-linux-armhf.so.3`), ekran 1072×1448, 8 bpp, Kindle FW 5.19.2.
- Kurulum ve çalışma yalnızca `/mnt/us` altına yazar; rootfs (`/`, `/etc`, `/usr`) asla değiştirilmez.
- `/mnt/us` symlink desteklemez — paketlerde symlink olmayacak.
- Kurulum kökü: `/mnt/us/kindle-apps/` ; veri: `/mnt/us/kindle-apps/data/<app>/` ;
  günlük: `/mnt/us/kindle-apps/logs/<app>.log` ; **tek scriptlet** `/mnt/us/documents/uygulamalar.sh`
  (`# Name: Uygulamalar`); uygulamalar menüye `/mnt/us/kindle-apps/apps/<id>/app.ini` ile kaydolur.
- Arayüz dili Türkçe (UTF-8). Her ekranda sol üstte Çıkış (✕) düğmesi.
- Yenileme: hamlelerde tam yenileme YOK; bölgesel flaşsız güncelleme; tam yenileme yalnızca
  ekran değişiminde/yeni oyunda ve 25 bölgesel güncellemede bir (ayar `full_refresh_every`,
  varsayılan 25, 0 = asla).
- Lisans GPLv3 (FBInk GPLv3+). Üçüncü taraf atıfları `THIRD_PARTY.md`.
- Depo: `github.com/dikeckaan/kindle-apps` (public); kpm manifest URL:
  `https://dikeckaan.github.io/kindle-apps/manifest.json`; `supported_platforms: ["kindlehf"]`.
- Commit mesajları şu iki satırla biter:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` ve
  `Claude-Session: https://claude.ai/code/session_01SphMWWECKY7iRhMm3YrMy5`
- Cihaz erişimi: `sshpass -p '' ssh -o PubkeyAuthentication=no root@192.168.2.90` (port 22,
  KOReader SSH'ı açıksa) veya `-p 2222` (geçici bağımsız dropbear). Cihaz uykudaysa kullanıcıdan
  uyandırması istenir.

## Review Focus

1. **Uygulama çökerse / öldürülürse Kindle arayüzü geri gelmeli** — `kui-run.sh` SIGSEGV, SIGKILL
   ve normal çıkışta pillow/awesome/statusbar/volumd'u geri getirir (Task 6 cihaz testi).
2. **Güç düğmesine basılıp uyandırıldığında** ekran ekran koruyucu kalıntısı olmadan tam yeniden
   çizilmeli (Task 5 `KUI_EV_WAKE` + Task 8 doğrulama).
3. **Hızlı ardışık dokunmalar / kaydırma ile dokunma karışması** — kısa hareket dokunma, eşiği aşan
   hareket kaydırma; çift olay üretilmemeli (Task 4 jest testleri).
4. **Bozuk/eksik kayıt dosyası** (yarım yazılmış, elle silinmiş, eski sürüm) uygulamayı çökertmemeli,
   yeni oyunla açılmalı (Task 7 `test_2048_load_corrupt`).
5. **Paket yeniden kurulumu/kaldırılması** kullanıcı verisini (`data/`) silmemeli; kaldırma sadece
   uygulama dosyalarını ve scriptlet'i siler (Task 9 testi).

---

## Dosya Yapısı

```
Makefile                         # host test + docker cross build + paket + deploy
tools/docker/Dockerfile          # (var) kindlehf araç zinciri
tools/fetch-deps.sh              # FBInk (sabit commit), DejaVu fontlar → third_party/
tools/pack.sh                    # pkg dizininden .kpkg üretir
tools/gen-manifest.sh            # dist/*.kpkg → dist/manifest.json (KMC v2)
third_party/                     # (gitignore) indirilen bağımlılıklar
core/kui/kui.h                   # tüm kui genel API'si
core/kui/refresh.c               # yenileme politikası (saf, host testli)
core/kui/gesture.c               # ham dokunma → jest (saf, host testli)
core/kui/layout.c                # dikdörtgen/isabet testi, ızgara yerleşimi (saf)
core/kui/utf8tr.c                # UTF-8 + Türkçe büyük/küçük harf + şapka katlama (saf)
core/kui/store.c                 # veri yolu, atomik dosya yazma, anahtar=değer ayarlar (saf-ish)
core/kui/screen.c                # FBInk sarmalayıcı: init, çizim, metin, güncelleme (cihaz)
core/kui/input.c                 # evdev okuma, EVIOCGRAB, lipc güç olayları (cihaz)
core/kui/widgets.c               # üst çubuk, düğme, diyalog (cihaz; isabet mantığı layout.c'de)
core/kui-run.sh                  # arayüzü duraklat → uygulama → her durumda geri getir
core/pkg/                        # "kindle-apps-core" kpm paketi şablonu
apps/game2048/g2048.c/.h         # 2048 saf mantık
apps/game2048/main.c             # 2048 ekran katmanı
apps/game2048/pkg/               # kpm paketi şablonu (manifest, install/uninstall/launch, app.ini)
core/hub/main.c                  # "Uygulamalar" menüsü: app.ini'leri okur, ızgara, alt süreç başlatır
tests/host/t.h                   # mini test makroları
tests/host/test_*.c              # host birim testleri
.github/workflows/release.yml    # etikette derle + paketle + Pages'e yayınla
README.md, THIRD_PARTY.md, LICENSE (GPLv3)
```

---

### Task 1: Cihaz spike'ı (atılabilir) ve sonuçların belgeye işlenmesi

Amaç tasarımın üç varsayımını doğrulamak (spec §8). Kod `spike/` altında, commit edilir ama
sonra silinir; **asıl çıktı spec'e eklenen "Spike sonuçları" bölümüdür**.

**Files:**
- Create: `spike/spike.c`, `spike/build.sh`, `spike/pause.sh`
- Modify: `docs/superpowers/specs/2026-10-18-kindle-apps-design.md` (§8'e "Spike sonuçları")

- [ ] **Step 1: FBInk'i derle.** `tools/fetch-deps.sh` (bu task'ta yazılır, Task 2 de kullanır):
  FBInk'i `https://github.com/NiLuJe/FBInk` master'dan `--recursive` klonlar, kullanılan commit
  SHA'sını `tools/deps.lock`'a yazar; DejaVu fontlarını (`DejaVuSans.ttf`, `DejaVuSans-Bold.ttf`,
  dejavu-fonts 2.37 sürüm arşivinden) `third_party/fonts/`'a indirir.
  Docker içinde: `make -C third_party/FBInk staticlib KINDLE=1 OPENTYPE=1 CROSS_TC=arm-kindlehf-linux-gnueabihf`
  (hedef adı FBInk Makefile'ında farklıysa ona uyulur; amaç `libfbink.a`).
  Beklenen: `third_party/FBInk/Release/libfbink.a` oluşur.
- [ ] **Step 2: `spike/spike.c` yaz.** Şunları yapar ve her adımı stdout'a yazar:
  `fbink_open`/`fbink_init`; `fbink_get_state` ile ekran boyutu/döndürme/`device_platform` yazdır;
  beyazla doldur + tam yenileme (GC16); DejaVu ile "Merhaba İıĞğŞşÇçÖöÜü ♔♕♖♗♘♙" yaz;
  sonra aynı ekranda 4 küçük dikdörtgeni sırayla siyah doldurup her birini farklı dalga formuyla
  bölgesel güncelle: `WFM_DU`, `WFM_A2`, `WFM_GL16`, `WFM_AUTO` — her birinde flaş olup olmadığı
  kullanıcıya sorulacak/fotoğrafla bakılacak; `/dev/input/event*` aygıtlarını listele
  (`EVIOCGNAME`, `EVIOCGBIT` ile ABS_MT_POSITION_X olanı dokunmatik say), `EVIOCGRAB` uygula,
  10 sn içindeki dokunuşların ham X/Y ve `ABS_MT_*` max değerlerini yazdır.
- [ ] **Step 3: `spike/pause.sh` yaz** — spec §5'teki KOReader dizisi: `lipc-set-prop com.lab126.pillow disableEnablePillow disable`,
  `killall -STOP awesome`, `stop statusbar` (iş varsa), `killall -STOP volumd`; `trap` ile çıkışta
  ters sıra: `killall -CONT volumd`, `start statusbar`, `killall -CONT awesome`,
  `lipc-set-prop com.lab126.pillow disableEnablePillow enable`,
  `lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home`.
- [ ] **Step 4: Cihazda çalıştır.** `scp` ile `/mnt/us/kindle-apps-spike/`'a kopyala,
  `sh pause.sh ./spike` çalıştır. Kullanıcıdan ekrana dokunmasını ve hangi dikdörtgenlerin flaş
  yaptığını söylemesini iste. Ekran görüntüsü: `head -c $((1072*1448)) /dev/fb0` → PGM → PNG.
  Ayrıca `kill -9` ile spike'ı öldürüp arayüzün geri geldiğini doğrula.
- [ ] **Step 5: Sonuçları spec §8 altına "Spike sonuçları" olarak yaz:** flaşsız dalga formu(ları),
  dokunmatik aygıt adı/yolu, ham koordinat aralığı ve ekran koordinatına dönüşüm (eksen
  takası/ters çevirme), `EVIOCGRAB` sonucu, duraklat/geri getir sonucu, güç tuşu aygıtı.
  **Bir varsayım çöktüyse dur ve kullanıcıya bildir.**
- [ ] **Step 6: Commit** — `git add spike tools/fetch-deps.sh tools/deps.lock docs && git commit -m "spike: verify FBInk/MTK refresh, touch input, UI pause on device"`

---

### Task 2: Depo iskeleti, derleme ve host test altyapısı

**Files:**
- Create: `Makefile`, `.gitignore`, `tests/host/t.h`, `tests/host/test_smoke.c`, `LICENSE`, `THIRD_PARTY.md`
- Delete: `spike/` (sonuçlar spec'te)

**Interfaces:**
- Produces (Makefile hedefleri, sonraki tüm task'lar kullanır):
  - `make test` — `tests/host/test_*.c` dosyalarının her birini ilgili `core/kui/*.c` ve
    `apps/*/*_logic`/saf `.c` dosyalarıyla host `cc` ile derleyip çalıştırır; biri başarısızsa çıkış ≠ 0.
    Test→kaynak eşlemesi Makefile'da açık listeyle (`TEST_test_refresh_SRCS := core/kui/refresh.c` gibi).
  - `make build` — Docker'da (`kindle-apps-tc`) tüm cihaz ikililerini `build/kindlehf/<app>` olarak derler.
  - `make dist` — `dist/*.kpkg` + `dist/manifest.json` (Task 9).
  - `make deploy KINDLE=192.168.2.90 [PORT=22]` — `dist/` paketlerini cihaza kopyalayıp kurar (Task 9).
- Produces `tests/host/t.h`: `T_ASSERT(cond)`, `T_EQ_INT(a,b)`, `T_EQ_STR(a,b)`, `T_RUN(fn)`,
  `T_DONE()` (başarısız sayısını döndürür, `main` bununla çıkar).

- [ ] **Step 1:** `tests/host/test_smoke.c` yaz: `T_EQ_INT(1+1, 2)`; `make test` çalıştır → derlenmemeli (Makefile yok) → FAIL.
- [ ] **Step 2:** `Makefile`, `t.h`, `.gitignore` (`third_party/ build/ dist/`) yaz. `make test` → `test_smoke: OK`.
- [ ] **Step 3:** `make build` için Docker çağrısı: `docker run --rm --platform linux/amd64 -v $(PWD):/src kindle-apps-tc make -f Makefile cross`; `cross` hedefi `libfbink.a`'yı (yoksa) derler. Doğrula: `make build` hatasız biter (henüz uygulama yok).
- [ ] **Step 4:** `LICENSE` (GPLv3 tam metin), `THIRD_PARTY.md` (FBInk GPLv3+, DejaVu fonts lisansı); `spike/` sil.
- [ ] **Step 5: Commit** — `chore: repo skeleton, host tests, docker cross build`

---

### Task 3: Saf yardımcılar — yenileme politikası, yerleşim, UTF-8/Türkçe, ayar deposu

**Files:**
- Create: `core/kui/kui.h` (bu task'taki bölümler), `core/kui/refresh.c`, `core/kui/layout.c`, `core/kui/utf8tr.c`, `core/kui/store.c`
- Test: `tests/host/test_refresh.c`, `tests/host/test_layout.c`, `tests/host/test_utf8tr.c`, `tests/host/test_store.c`

**Interfaces (Produces, `kui.h`):**
```c
typedef struct { int x, y, w, h; } KuiRect;
int  kui_rect_contains(KuiRect r, int px, int py);
KuiRect kui_rect_union(KuiRect a, KuiRect b);            /* boş (w==0) olan yok sayılır */
KuiRect kui_grid_cell(KuiRect area, int cols, int rows, int col, int row, int gap);

typedef enum { KUI_UPD_PARTIAL, KUI_UPD_FULL } KuiUpdKind;
typedef struct { int every; int count; } KuiRefreshPolicy;   /* every==0 → asla otomatik tam */
void       kui_policy_init(KuiRefreshPolicy *p, int every);
KuiUpdKind kui_policy_next(KuiRefreshPolicy *p, int screen_changed); /* tam yenilemede count=0 */

/* UTF-8 ve Türkçe */
int  kui_utf8_decode(const char *s, int *cp);          /* bayt uzunluğu; geçersizde 1 ve cp=0xFFFD */
int  kui_utf8_encode(int cp, char out[4]);
int  kui_utf8_len(const char *s);                      /* kod noktası sayısı */
int  kui_tr_upper(int cp);                             /* i→İ, ı→I, ğ→Ğ, ş→Ş, ç→Ç, ö→Ö, ü→Ü, a-z→A-Z */
int  kui_tr_lower(int cp);                             /* İ→i, I→ı, ... */
int  kui_tr_fold_circumflex(int cp);                   /* â→a î→i û→u Â→A Î→İ Û→U */
void kui_tr_upper_str(const char *in, char *out, int outsz);

/* Depo */
const char *kui_data_dir(const char *app);   /* "$KUI_ROOT/data/<app>" — KUI_ROOT env, varsayılan /mnt/us/kindle-apps; mkdir -p yapar */
int  kui_write_atomic(const char *path, const void *buf, int len);  /* tmp + fsync + rename; 0=ok */
int  kui_read_file(const char *path, char *buf, int cap);           /* okunan bayt, yoksa -1 */
int  kui_cfg_get_int(const char *path, const char *key, int def);   /* "key=value" satırları */
int  kui_cfg_set_int(const char *path, const char *key, int val);
```

- [ ] **Step 1: Testleri yaz:**
  - `test_policy_partial_until_threshold`: every=25 → ilk 24 çağrı (screen_changed=0) PARTIAL, 25. FULL, 26. PARTIAL.
  - `test_policy_screen_change_forces_full`: screen_changed=1 → FULL ve sayaç sıfırlanır.
  - `test_policy_never`: every=0 → 1000 çağrı PARTIAL (screen_changed=0).
  - `test_grid_cell`: area {0,0,400,400}, 4×4, gap 10 → hücre (0,0) = {10,10,87,87} ve (3,3) = {303,303,87,87}
    (formül: `cw=(w-gap*(cols+1))/cols`, `x=area.x+gap+col*(cw+gap)`).
  - `test_union_ignores_empty`, `test_contains_edges` (sol/üst dahil, sağ/alt hariç).
  - `test_tr_upper`: `kui_tr_upper('i')==0x130`, `kui_tr_upper(0x131)=='I'`, `kui_tr_lower('I')==0x131`, `kui_tr_lower(0x130)=='i'`;
    `kui_tr_upper_str("çiğdem ışık")` == `"ÇİĞDEM IŞIK"`; `kui_tr_fold_circumflex(0xE2)=='a'`.
  - `test_utf8_invalid`: `"\xFF"` → uzunluk 1, cp 0xFFFD.
  - `test_store_atomic_roundtrip` (KUI_ROOT=geçici dizin), `test_cfg_missing_key_returns_default`, `test_cfg_set_then_get`, `test_read_missing_file_returns_minus1`.
- [ ] **Step 2:** `make test` → bu testler FAIL (fonksiyonlar yok).
- [ ] **Step 3:** Fonksiyonları ilgili dosyalara yaz.
- [ ] **Step 4:** `make test` → hepsi OK.
- [ ] **Step 5: Commit** — `feat(kui): refresh policy, layout, Turkish UTF-8, atomic store`

---

### Task 4: Jest tanıyıcı (saf)

**Files:**
- Create: `core/kui/gesture.c` (+ `kui.h` bölümü)
- Test: `tests/host/test_gesture.c`

**Interfaces (Produces):**
```c
typedef enum { KUI_EV_NONE, KUI_EV_TAP, KUI_EV_LONGPRESS, KUI_EV_SWIPE,
               KUI_EV_WAKE, KUI_EV_QUIT } KuiEvType;
typedef enum { KUI_DIR_NONE, KUI_DIR_LEFT, KUI_DIR_RIGHT, KUI_DIR_UP, KUI_DIR_DOWN } KuiDir;
typedef struct { KuiEvType type; int x, y; KuiDir dir; } KuiEvent;
typedef struct { int down, x0, y0, x, y; long t0_ms; } KuiGesture;
/* ekran koordinatlı ham örnekler: down=1 bas/sürükle, down=0 kaldır */
KuiEvent kui_gesture_feed(KuiGesture *g, int down, int x, int y, long t_ms);
```
Değerler (spec'ten değil, benim kararım — sabit `#define`): kaydırma eşiği 80 px; uzun basış 600 ms
ve hareket < 40 px; yalnızca **kaldırmada** olay üretilir (sürükleme sırasında yok).

- [ ] **Step 1: Testler:** `test_tap` (bas 100,100 t0 → kaldır 105,103 t=120 → TAP @100,100);
  `test_swipe_left` (500,500 → 300,510 → SWIPE LEFT); `test_swipe_dominant_axis` (dx=90, dy=120 → DOWN);
  `test_longpress` (bas, kaldır t=700, hareket 10 → LONGPRESS); `test_move_under_threshold_slow_is_longpress_not_swipe`;
  `test_no_event_while_dragging` (down=1 tekrarları → NONE); `test_release_without_press_is_none`.
- [ ] **Step 2:** `make test` → FAIL. **Step 3:** uygula. **Step 4:** `make test` → OK.
- [ ] **Step 5: Commit** — `feat(kui): gesture recognizer`

---

### Task 5: Cihaz katmanı — ekran, metin, girdi, güç olayları

Spike sonuçlarındaki (spec §8) dalga formu, aygıt yolu ve koordinat dönüşümü kullanılır.

**Files:**
- Create: `core/kui/screen.c`, `core/kui/input.c` (+ `kui.h` bölümü), `apps/kuidemo/main.c` (geçici doğrulama uygulaması, Task 8'de silinir)

**Interfaces (Produces):**
```c
typedef enum { KUI_FONT_REG, KUI_FONT_BOLD } KuiFont;
typedef enum { KUI_ALIGN_LEFT, KUI_ALIGN_CENTER, KUI_ALIGN_RIGHT } KuiAlign;
int  kui_init(const char *app);      /* fbink init, fontlar ($KUI_ROOT/fonts/DejaVuSans*.ttf), dokunmatik aç+GRAB, lipc dinleyici; 0=ok */
void kui_quit(void);                 /* GRAB bırak, dinleyiciyi öldür, fbink kapat */
int  kui_width(void); int kui_height(void);
void kui_fill(KuiRect r, unsigned char gray);              /* 0=siyah 255=beyaz; yalnız buffer */
void kui_frame(KuiRect r, int thickness, unsigned char gray);
void kui_text(KuiRect box, const char *utf8, KuiFont f, int px, KuiAlign a, unsigned char fg); /* dikey ortalı */
int  kui_text_width(const char *utf8, KuiFont f, int px);
void kui_mark_dirty(KuiRect r);       /* bekleyen bölgeye ekler (union) */
void kui_flush(int screen_changed);   /* politikaya göre: PARTIAL → dirty bölge flaşsız dalga formuyla; FULL → tüm ekran GC16 flaş */
void kui_set_refresh_every(int n);    /* ayar: data/<app>/settings → full_refresh_every */
KuiEvent kui_next_event(int timeout_ms); /* jest tanıyıcıdan geçmiş olay; timeout→NONE; güçten uyanınca WAKE; SIGTERM→QUIT */
```
Uygulama: çizim çağrıları FBInk `fbink_fill_rect_gray`/`fbink_print_ot` ile `no_refresh=true`
yapar; tek yenileme noktası `kui_flush`'tır (fbink_refresh + seçilen dalga formu). Güç olayları:
`lipc-wait-event -m com.lab126.powerd goingToScreenSaver,outOfScreenSaver` alt sürecinin
stdout'u `poll` ile okunur; `outOfScreenSaver` → `KUI_EV_WAKE`.

- [ ] **Step 1:** `screen.c`, `input.c` yaz; `make build` derlenir.
- [ ] **Step 2:** `apps/kuidemo/main.c`: beyaz ekran, ortada "Dokun", dokunulan noktaya 40×40 siyah kare çiz
  (`kui_mark_dirty` + `kui_flush(0)`), sola kaydırınca ekran tersine dönsün (`kui_flush(1)`), ✕ bölgesine (sol üst 0,0,120,120) dokununca çık.
- [ ] **Step 3: Cihazda doğrula** (Task 6 başlatıcısı henüz yoksa spike'ın `pause.sh`'ı kopyalanarak):
  dokunulan yerde kare çıkıyor (koordinat doğru), karelerde flaş yok, 25. karede bir tam yenileme oluyor,
  güç tuşuyla uyut/uyandır sonrası ekran yeniden çiziliyor (WAKE). Ekran görüntüsü al.
- [ ] **Step 4: Commit** — `feat(kui): FBInk screen layer, evdev input, power events`

---

### Task 6: Güvenli başlatıcı `kui-run.sh` ve çekirdek paket şablonu

**Files:**
- Create: `core/kui-run.sh`, `core/pkg/manifest.json`, `core/pkg/install.sh`, `core/pkg/uninstall.sh`

**Interfaces (Produces):**
- `kui-run.sh <app-id> <binary> [args...]` — root değilse `/var/local/kmc/bin/su -c` ile kendini yeniden çalıştırır;
  spike'ta doğrulanan duraklatma dizisini uygular; `KUI_ROOT=/mnt/us/kindle-apps` export eder;
  ikiliyi çalıştırır, stdout/stderr → `logs/<app-id>.log` (son 200 KB tutulur);
  `trap` EXIT/INT/TERM/HUP ile geri getirme dizisini **tam bir kez** çalıştırır; ikili sıfırdan farklı çıkarsa
  `fbink` CLI ile "Uygulama hata verdi, günlük: logs/<app>.log" yazar ve 3 sn bekler.
  Eşzamanlı ikinci başlatmayı `/tmp/kui-run.lock` ile engeller.
- Çekirdek paket `kindle-apps-core` → `/mnt/us/kindle-apps/{bin/kui-run.sh,fonts/*.ttf}`; kaldırma `data/`'ya dokunmaz.

- [ ] **Step 1: Cihaz testleri (elle, sonuçları task notuna yaz):**
  (a) `kuidemo`'yu kui-run ile aç, ✕ ile çık → Kindle ana ekranı ve üst durum çubuğu geri geldi;
  (b) çalışırken başka SSH oturumundan `kill -9 <kuidemo pid>` → arayüz geri geldi, hata mesajı göründü;
  (c) `kill -TERM <kui-run pid>` → arayüz geri geldi; (d) iki kez hızlı başlat → ikincisi reddedildi.
- [ ] **Step 2:** Uygula; (a)–(d)'yi cihazda geçir.
- [ ] **Step 3: Commit** — `feat: safe launcher kui-run.sh and core package`

---

### Task 7: 2048 mantığı (saf)

**Files:**
- Create: `apps/game2048/g2048.c`, `apps/game2048/g2048.h`
- Test: `tests/host/test_g2048.c`

**Interfaces (Produces):**
```c
typedef struct { int cell[4][4]; int score, best; int prev[4][4]; int prev_score; int can_undo; unsigned rng; } G2048;
void g2048_new(G2048 *g, unsigned seed);            /* 2 rastgele taş; best korunur */
int  g2048_move(G2048 *g, KuiDir d);                /* hareket olduysa 1 (ve yeni taş eklenir), yoksa 0 */
int  g2048_can_move(const G2048 *g);
int  g2048_max_tile(const G2048 *g);
int  g2048_undo(G2048 *g);                          /* 1 adım; başarılıysa 1 */
int  g2048_save(const G2048 *g, char *buf, int cap);/* metin biçimi, "v1" başlıklı */
int  g2048_load(G2048 *g, const char *buf);         /* 0=ok, -1 bozuk → g dokunulmaz */
```
Yeni taş: %90 → 2, %10 → 4; RNG: xorshift32, `rng` alanında (testlerde deterministik).

- [ ] **Step 1: Testler:** `test_merge_once_per_move` (satır [2,2,2,2] sola → [4,4,0,0], skor +8);
  `test_merge_order_left` ([2,2,4,0] sola → [4,4,0,0]); `test_no_move_returns_0_and_no_spawn`;
  `test_move_right_up_down` (her yön için bir tablo); `test_undo_restores_board_and_score`;
  `test_undo_only_once`; `test_game_over_detection` (dolu, komşu eşit yok → `can_move`=0);
  `test_best_score_updates`; `test_save_load_roundtrip`; `test_2048_load_corrupt` (`"v1\n2 2 x"`, boş string,
  `"v9..."` → -1, durum değişmez); `test_spawn_distribution` (seed sabit, 10 000 yeni oyun → 4 oranı %8–%12).
- [ ] **Step 2:** FAIL. **Step 3:** uygula. **Step 4:** OK.
- [ ] **Step 5: Commit** — `feat(2048): game logic`

---

### Task 8: 2048 ekranı ve uygulama

**Files:**
- Create: `apps/game2048/main.c`; Delete: `apps/kuidemo/`
- Create: `core/kui/widgets.c` (+ `kui.h`): üst çubuk ve düğmeler, diyalog

**Interfaces (Produces, Plan 2+ de kullanır):**
```c
#define KUI_TOPBAR_H 120
typedef struct { KuiRect r; const char *label; int id; } KuiButton;
void kui_topbar(const char *title, const char *right_label); /* sol üstte ✕ (id KUI_BTN_EXIT=-1), sağda isteğe bağlı düğme (id KUI_BTN_MENU=-2) */
int  kui_topbar_hit(int x, int y);                            /* -1 çıkış, -2 menü, 0 hiçbiri */
void kui_button(const KuiButton *b, int pressed);
int  kui_buttons_hit(const KuiButton *bs, int n, int x, int y); /* id veya 0 */
int  kui_dialog(const char *title, const char *msg, const char **choices, int n); /* modal; seçilen indeks; ✕ → -1 */
```

Ekran: üst çubuk "2048" + sağda "Yeni"; altında Skor / En iyi; ortada 4×4 tahta
(`kui_grid_cell`, alan genişliği `kui_width()-80`), taş: değerine göre gri ton (2=beyaz çerçeveli,
≥128 siyah zemin/beyaz yazı), en altta ◀ ▲ ▼ ▶ ve "Geri al" düğmeleri. Kaydırma ve düğme ikisi de çalışır.
Bir hamlede **yalnız değişen hücreler + skor kutusu** `kui_mark_dirty` edilir. Oyun bitince
`kui_dialog("Oyun bitti", "Skor: N", {"Yeni oyun","Kapat"}, 2)`. 2048'e ulaşınca bir kez "Tebrikler! Devam?" diyalogu.
Her hamlede durum `data/game2048/save.txt`'ye atomik yazılır; açılışta yüklenir (bozuksa yeni oyun).
`KUI_EV_WAKE` → tüm ekran yeniden çiz + `kui_flush(1)`.

- [ ] **Step 1:** `widgets.c` + `main.c` yaz; `make build` derlenir.
- [ ] **Step 2: Cihazda doğrula:** aç → oyna (kaydırma + düğme) → hamlelerde flaş yok → ✕ ile çık → tekrar aç,
  oyun kaldığı yerden → güç tuşu uyut/uyandır → ekran düzgün → `save.txt`'yi bozup aç → yeni oyun.
  Ekran görüntüsü al.
- [ ] **Step 3: Commit** — `feat(2048): e-ink UI app`

---

### Task 8b: "Uygulamalar" menüsü (hub)

**Files:**
- Create: `core/hub/main.c`, `core/hub/appreg.c`, `core/hub/appreg.h`
- Test: `tests/host/test_appreg.c`

**Interfaces (Produces):**
```c
typedef struct { char id[32]; char name[48]; char exec[256]; int order; } HubApp;
/* $KUI_ROOT/apps/*/app.ini okur ("name=", "exec=" (app dizinine göre göreli), "order="); order, sonra ad sırası; en çok 32 */
int hub_scan_apps(const char *root, HubApp *out, int max);
```
`app.ini` örneği: `name=2048\nexec=game2048\norder=20`.
Menü: üst çubuk "Uygulamalar" (✕ → Kindle'a dön), uygulamalar 3 sütunlu ızgarada büyük etiketli
kutular (kui_grid_cell, alan genişliği `kui_width()-80`); 9'dan fazla ise alt kısımda ◀ ▶ sayfa.
Dokununca: `kui_quit()` → `fork`+`execl(exec)` (env `KUI_ROOT` korunur) → `waitpid` → `kui_init("hub")`
→ tam yeniden çiz + `kui_flush(1)`. Alt süreç ≠0 çıkarsa `kui_dialog("Hata", "<ad> beklenmedik şekilde kapandı", {"Tamam"},1)`.

- [ ] **Step 1: Testler:** `test_scan_sorts_by_order_then_name`, `test_scan_skips_dir_without_ini`,
  `test_scan_skips_ini_without_exec`, `test_scan_resolves_exec_relative_to_app_dir`, `test_scan_caps_at_max`. FAIL.
- [ ] **Step 2:** `appreg.c` uygula → OK. **Step 3:** `main.c` yaz, `make build`.
- [ ] **Step 4: Cihazda doğrula:** `kui-run.sh hub <hub>` → menüde "2048" → aç → ✕ → menüye döndü (Kindle ana ekranı
  arada görünmedi) → ✕ → Kindle ana ekranı geldi. 2048'i `kill -9` → menü geri geldi + hata diyaloğu.
- [ ] **Step 5: Commit** — `feat(hub): Uygulamalar launcher menu`

---

### Task 9: Paketleme, depo manifesti, yerel kurulum

**Files:**
- Create: `tools/pack.sh`, `tools/gen-manifest.sh`, `apps/game2048/pkg/{manifest.json,install.sh,uninstall.sh,launch.sh,app.ini}`, `core/pkg/scriptlets/uygulamalar.sh`
- Modify: `Makefile` (`dist`, `deploy`)

**Interfaces:**
- `.kpkg` = `tar czf` kökünde: `manifest.json` (v2: `manifest_version:2, id, name, author, description, version:[Y,M,P], dependencies, supported_platforms:["kindlehf"]`),
  `install.sh`, `uninstall.sh`, `launch.sh`, `scriptlets/`, ve kurulacak dosyalar. Biçim örneği:
  cihazdaki `/mnt/us/kmc/kpm/packages/koreader/` ve KindleTweaks `kuala_1.1.0_kindlehf.kpkg`.
- Paket kimlikleri: `kindle-apps-core`, `kindle-apps-2048` (2048 → core'a bağımlı). Sürüm `[2026,10,1]`.
- `gen-manifest.sh` → `dist/manifest.json` (`manifest_version:2, id:"kindle-apps", name:"Kindle Apps (TR)"`, her paket için
  `artifacts[{url:"packages/<id>/<id>_<ver>_kindlehf.kpkg", version, dependencies, supported_platforms}]`).
- Tek scriptlet (core paketinde) başlığı: `# Name: Uygulamalar`, `# Author: Kaan Dikeç`, `# DontUseFBInk`; gövde
  `/var/local/kmc/bin/kpm launch kindle-apps-core`. Core `launch.sh`: `exec /mnt/us/kindle-apps/bin/kui-run.sh hub /mnt/us/kindle-apps/bin/hub`.
  2048 paketi `/mnt/us/kindle-apps/apps/game2048/{game2048,app.ini}` kurar; `launch.sh`'ı yalnız hub'ı açar.

- [ ] **Step 1: Test:** `tests/pkg/test_pack.sh`: `make dist` sonrası her `.kpkg` için `tar tzf` → `manifest.json`,
  `install.sh`, `uninstall.sh`, `launch.sh` var; `find` ile symlink yok; `manifest.json` geçerli JSON (`python3 -m json.tool`).
  Çalıştır → FAIL.
- [ ] **Step 2:** Uygula; testi geçir.
- [ ] **Step 3: Cihazda yerel kurulum:** `make deploy` her `.kpkg` için kpm'in yaptığını taklit eder: paketi cihazda
  `/mnt/us/kindle-apps-staging/<id>/` içine açar ve o dizinde `sh install.sh` çalıştırır (önce core, sonra 2048),
  sonra staging'i siler. Kitaplıkta "Uygulamalar" görünür, menüde 2048 var, açılır.
  2048'i kaldır (`uninstall.sh`) → menüden kaybolur, uygulama dosyaları gider, `data/game2048/` kalır; tekrar kur → kayıt duruyor.
- [ ] **Step 4: Commit** — `feat: kpkg packaging, repo manifest, deploy`

---

### Task 10: GitHub deposu, CI ve gerçek kpm kurulumu

**Files:**
- Create: `.github/workflows/release.yml`, `README.md` (Türkçe), `tools/kindle-apps-depo.sh` (scriptlet)

**Interfaces:**
- Workflow: `v*` etiketinde ve `workflow_dispatch`'te: `tools/docker/Dockerfile` imajını kur → `make test` → `make build dist` →
  `dist/`'i GitHub Pages'e yayınla (`actions/upload-pages-artifact` + `actions/deploy-pages`) → `dist/*.kpkg` ve
  `tools/kindle-apps-depo.sh`'ı Release'e ekle.
- `kindle-apps-depo.sh` scriptlet'i (`# Name: Kindle Apps Deposu`): `kpm add-repo https://dikeckaan.github.io/kindle-apps/manifest.json`,
  `kpm update`, `kpm -y install kindle-apps-2048`; sonucu `fbink` ile gösterir.
- README: ne olduğu, ekran görüntüsü, kurulum (scriptlet yolu + SSH komut yolu), kaldırma, güvenlik notları, lisans.

- [ ] **Step 1:** Dosyaları yaz. `gh repo create dikeckaan/kindle-apps --public --source . --push`;
  Pages'i Actions kaynağıyla etkinleştir (`gh api -X POST repos/dikeckaan/kindle-apps/pages -f build_type=workflow`).
- [ ] **Step 2:** `git tag v2026.10.1 && git push --tags`; `gh run watch` → başarılı;
  `curl -sf https://dikeckaan.github.io/kindle-apps/manifest.json | python3 -m json.tool` → `kindle-apps-2048` içerir.
- [ ] **Step 3: Cihazda gerçek kurulum:** Task 9'daki yerel kurulumu kaldır; cihazda
  `kpm add-repo …; kpm update; kpm -y install kindle-apps-2048` → çıkış 0, kitaplıkta "Uygulamalar", menüde 2048, açılıyor, oynanıyor, çıkışta arayüz geri geliyor.
- [ ] **Step 4: Commit** — `ci: build and publish kpm repo to GitHub Pages`
