# Plan 2 — Satranç ve Uyku Ayarı Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** "Uygulamalar" menüsüne tam kurallı, tek oyunculu (Stockfish, 6 seviye) ve iki oyunculu
(çevrimdışı) bir Satranç ile Kindle uyku süresini ayarlayan Uyku Ayarı uygulamasını eklemek ve
kpm deposunda yayınlamak.

**Architecture:** Plan 1 çatısı (`kui`, hub, `kui-run.sh`, kpkg) aynen kullanılır. Satranç üç katman:
`chess_rules.c` (saf kurallar, perft testli), `chess_game.c` (oyun durumu, bitiş, geri al, kayıt —
saf), `uci.c` (Stockfish ile boru üzerinden UCI — host'ta sahte motorla testli) ve `main.c` (ekran).
Stockfish 11 (klasik değerlendirme, NNUE'siz) kaynaktan kindlehf için statik derlenir ve satranç
paketine konur. Uyku Ayarı küçük bir kui uygulamasıdır (powerd lipc).

**Tech Stack:** C11 + kui (Plan 1), Stockfish 11 (C++, GPLv3), koxtoolchain kindlehf (Docker).

**Spec:** `docs/superpowers/specs/2026-10-18-kindle-apps-design.md` (§2.1 Satranç, §2.10 Uyku Ayarı, §6.3)

## Global Constraints

- Plan 1'in Global Constraints'i aynen geçerli (kindlehf, yalnız `/mnt/us`, symlink yok, Türkçe arayüz,
  sol üstte ✕, bölgesel GL16 güncelleme, 25'te bir tam yenileme, GPLv3, commit trailer'ları).
- Paket kimlikleri: `kindle-apps-chess`, `kindle-apps-sleep`; kpm bağımlılığı **tanımlanmaz**
  (kpm 0.2.2 hatası) — install.sh çekirdek yoksa uyarır; depo scriptlet'i ve README her paketi ayrı kurar.
- kpkg girdileri `./` öneksiz (tools/pack.sh), sürüm `[2026,10,5]`.
- Menü sırası (`app.ini order=`): Satranç 10, 2048 20, Uyku Ayarı 90.
- `kui_dialog(title, msg, choices, n, redraw_bg)` — uyanınca arka planı yeniden çizen geri çağırma zorunlu.
- Motor hamlesi ≤ ~5 sn (spec §6.3).
- Satranç verisi: `data/chess/save.txt`; Uyku Ayarı verisi yok (değer powerd'de).

## Review Focus

1. **Motor çökmesi/yanıt vermemesi** — Stockfish ölürse veya 10 sn'de `bestmove` gelmezse uygulama donmamalı:
   "Motor yanıt vermedi" diyaloğu, oyun iki oyunculu moda geçebilir ya da yeniden başlatılır (Task 4 testi
   `test_uci_engine_dies_returns_error`, `test_uci_timeout`).
2. **Motor düşünürken ✕ veya güç tuşu** — uygulama hemen çıkabilmeli / uyanınca tahtayı çizmeli; motor süreci
   arkada kalmamalı (Task 5 cihaz doğrulaması + `test_uci_close_kills_engine`).
3. **Bozuk/eski kayıt** (geçersiz FEN, yasadışı hamle listesi) çökme değil yeni oyun (Task 3 `test_game_load_corrupt`).
4. **Terfi, rok ve geçerken alma dokunmatikle** — terfide seçim diyaloğu; rok şah iki kare gidince yapılır
   (Task 5 cihaz doğrulaması, Task 2 perft).
5. **Uyku süresini çok uzun seçip uygulamayı kapatmak** — değer powerd'de kalır, uygulama tekrar açılınca doğru
   gösterilir; "Hiç uyuma" açıkken başka bir süre seçilince `preventScreenSaver` kapatılır (Task 6 testi).

---

## Dosya Yapısı

```
tools/build-stockfish.sh          # SF 11'i indir (sabit sürüm), kindlehf için statik derle → build/kindlehf/stockfish
apps/chess/chess_rules.c/.h       # tahta, FEN, yasal hamle üretimi, hamle yap/geri al, şah kontrolü (saf)
apps/chess/chess_game.c/.h        # oyun: geçmiş, bitiş durumları, UCI hamle metni, geri al, kayıt (saf)
apps/chess/uci.c/.h               # motor süreci, UCI konuşması, zaman aşımı (POSIX)
apps/chess/levels.c/.h            # 6 seviye → UCI ayarları (saf)
apps/chess/main.c                 # ekranlar: mod/seviye/renk seçimi, tahta, diyaloglar
apps/chess/pkg/                   # manifest, install/uninstall/launch, app.ini (stockfish + COPYING dahil)
apps/sleep/sleep_opts.c/.h        # seçenekler ve mevcut durum → etiket (saf)
apps/sleep/main.c                 # Uyku Ayarı ekranı (lipc get/set)
apps/sleep/pkg/
tests/host/test_chess_rules.c, test_chess_game.c, test_uci.c, test_levels.c, test_sleep_opts.c
tests/host/fake_engine.sh         # UCI'yi taklit eden sahte motor (testler için)
```

---

### Task 1: Spike — Stockfish derleme ve powerd uyku özellikleri

**Files:** Create `tools/build-stockfish.sh`; Modify spec §8 ("Spike sonuçları 2").

- [ ] **Step 1:** `tools/build-stockfish.sh`: `https://github.com/official-stockfish/Stockfish/archive/refs/tags/sf_11.tar.gz`
  → `third_party/Stockfish-sf_11`; Docker'da `make -C src build ARCH=<SF11'in desteklediği armv7 hedefi> COMP=gcc
  CXX=arm-kindlehf-linux-gnueabihf-g++ EXTRALDFLAGS=-static` (hedef adı SF11 Makefile'ından okunur) →
  `build/kindlehf/stockfish` (strip). `third_party/Stockfish-sf_11/Copying.txt` → lisans.
- [ ] **Step 2: Cihazda:** `stockfish bench 16 1 8` süresini ve `echo "uci" | stockfish` çıktısında
  `UCI_Elo` (min/max) ve `Skill Level` seçeneklerini kaydet; başlangıç konumunda `go movetime 2000` derinliğini ölç.
- [ ] **Step 3: Cihazda powerd:** `lipc-get-prop com.lab126.powerd screenSaverTimeout` (varsayılan), `lipc-set-prop … 1800`
  → geri oku; 2 dk dokunmadan bekle değil — `status` çıktısındaki "Remaining time" ile doğrula; `preventScreenSaver 1/0`.
  Kalıcılığı kullanıcıdan bir yeniden başlatma isteyerek ölç (yeniden başlatma kullanıcı onayıyla).
  Sonra değeri eski hâline getir.
- [ ] **Step 4:** Sonuçları spec §8'e yaz (SF hedef adı, ikili boyutu, bench süresi, Elo aralığı, powerd kalıcılığı).
- [ ] **Step 5: Commit** — `spike: stockfish on kindlehf, powerd sleep properties`

---

### Task 2: Satranç kuralları (saf) — perft ile

**Files:** Create `apps/chess/chess_rules.c/.h`; Test `tests/host/test_chess_rules.c`

**Interfaces (Produces):**
```c
typedef enum { EMPTY = 0, WP, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK } Piece;
typedef struct { unsigned char from, to, promo; unsigned char flags; } Move;  /* kare 0=a1 … 63=h8; promo: WQ/WR/WB/WN (renkten bağımsız tür) */
#define MV_CAPTURE 1
#define MV_EP 2
#define MV_CASTLE 4
#define MV_DOUBLE 8
typedef struct {
	unsigned char sq[64];
	int side;            /* 0 beyaz, 1 siyah */
	int castling;        /* bit0 K, bit1 Q, bit2 k, bit3 q */
	int ep;              /* geçerken alma hedef karesi veya -1 */
	int halfmove, fullmove;
} Board;
int  chess_from_fen(Board *b, const char *fen);       /* 0 ok, -1 geçersiz */
void chess_to_fen(const Board *b, char *out, int cap);
int  chess_legal_moves(const Board *b, Move *out);     /* yasal hamle sayısı (≤ 256) */
void chess_make(Board *b, Move m);                     /* kopya-yap yaklaşımı: çağıran önce kopyalar */
int  chess_in_check(const Board *b, int side);
int  chess_piece_color(int p);                         /* 0/1, boşsa -1 */
long chess_perft(const Board *b, int depth);
#define CHESS_START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
```

- [ ] **Step 1: Testler:** `test_fen_roundtrip_start` ve Kiwipete FEN; `test_fen_rejects_garbage` (`"xyz"`, 7 satır, iki beyaz şah → -1);
  `test_perft_start` (d1=20, d2=400, d3=8902, d4=197281); `test_perft_kiwipete`
  (`r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1`: d1=48, d2=2039, d3=97862);
  `test_perft_position3` (`8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1`: d1=14, d2=191, d3=2812, d4=43238);
  `test_perft_position4` (`r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1`: d1=6, d2=264, d3=9467);
  `test_ep_capture_removes_pawn`; `test_castling_moves_rook`; `test_promotion_four_choices`.
- [ ] **Step 2:** FAIL. **Step 3:** Uygula (0x88 veya 64-kare dizisi + yön tabloları; yasallık: hamleyi yap, kendi şahın saldırı altında mı).
- [ ] **Step 4:** `make test` OK (perft d4 host'ta < 2 sn olmalı). **Step 5: Commit** — `feat(chess): rules with perft`

---

### Task 3: Oyun durumu (saf) — bitiş, UCI metni, geri al, kayıt

**Files:** Create `apps/chess/chess_game.c/.h`; Test `tests/host/test_chess_game.c`

**Interfaces (Produces):**
```c
typedef enum { RES_ONGOING, RES_MATE, RES_STALEMATE, RES_FIFTY, RES_REPETITION, RES_MATERIAL } Result;
#define GAME_MAX_PLIES 600
typedef struct {
	char start_fen[100];
	Board pos;                       /* güncel konum */
	Move hist[GAME_MAX_PLIES]; int n; /* oynanan yarım hamleler */
	int mode;                        /* 1 tek oyunculu, 2 iki oyunculu */
	int level;                       /* 1..6 (tek oyunculu) */
	int human_side;                  /* 0 beyaz, 1 siyah (tek oyunculu) */
	int flip;                        /* iki oyunculuda her hamlede tahtayı çevir */
} Game;
void   game_new(Game *g, int mode, int level, int human_side);
int    game_play(Game *g, Move m);                    /* yasal değilse -1 */
int    game_play_uci(Game *g, const char *uci);       /* "e2e4", "e7e8q"; yasal değilse -1 */
void   move_to_uci(Move m, char out[6]);
int    game_undo(Game *g, int plies);                  /* geri alınan sayı */
Result game_result(const Game *g);                    /* tekrar: aynı konum (FEN ilk 4 alan) 3 kez */
void   game_uci_position(const Game *g, char *out, int cap); /* "position fen <start> moves e2e4 …" */
int    game_save(const Game *g, char *buf, int cap);   /* "v1" başlıklı metin */
int    game_load(Game *g, const char *buf);            /* 0 ok, -1 bozuk → g dokunulmaz */
const char *result_text(Result r, int loser_side);     /* Türkçe: "Şah mat — Beyaz kazandı", "Pat — berabere", … */
```

- [ ] **Step 1: Testler:** `test_fools_mate_is_mate` (f3 e5 g4 Qh4 → RES_MATE, kaybeden beyaz → "Şah mat — Siyah kazandı");
  `test_stalemate` (`7k/5Q2/6K1/8/8/8/8/8 b - - 0 1` → RES_STALEMATE); `test_fifty_move` (halfmove 100 → RES_FIFTY);
  `test_threefold` (Nf3 Nf6 Ng1 Ng8 ×2 → RES_REPETITION); `test_insufficient_material` (K v K, K+B v K, K+N v K → RES_MATERIAL;
  K+R v K → ONGOING); `test_play_uci_rejects_illegal`; `test_uci_promotion_roundtrip` ("a7a8n");
  `test_undo_two_plies`; `test_uci_position_string`; `test_save_load_roundtrip`;
  `test_game_load_corrupt` (bozuk FEN; yasadışı hamle içeren liste; "v9"; boş → -1, g değişmez).
- [ ] **Step 2:** FAIL. **Step 3:** Uygula. **Step 4:** OK. **Step 5: Commit** — `feat(chess): game state, results, save`

---

### Task 4: Seviyeler ve UCI motor istemcisi

**Files:** Create `apps/chess/levels.c/.h`, `apps/chess/uci.c/.h`, `tests/host/fake_engine.sh`; Test `tests/host/test_levels.c`, `tests/host/test_uci.c`

**Interfaces (Produces):**
```c
typedef struct { const char *name; int limit_strength; int elo; int skill; int depth; int movetime_ms; } Level;
const Level *level_get(int level);   /* 1..6; aralık dışı → 3 */
/* 1 Çok Kolay: skill 0, depth 1, 100 ms · 2 Kolay: Elo 1350, 500 ms · 3 Orta: Elo 1600, 1000 ms
 * 4 İyi: Elo 1900, 1500 ms · 5 Zor: Elo 2200, 2500 ms · 6 Usta: tam güç, 4000 ms
 * (Elo alt sınırı Task 1'de ölçülen UCI_Elo min'ine göre kırpılır) */

typedef struct { pid_t pid; int to_fd, from_fd; char buf[4096]; int len; } Uci;
int  uci_start(Uci *u, const char *path);          /* "uci" → "uciok" (5 sn); 0 ok, -1 hata */
int  uci_configure(Uci *u, const Level *lv);       /* setoption'lar + isready/readyok */
int  uci_go(Uci *u, const char *position_cmd, const Level *lv); /* position + go (movetime veya depth) gönderir */
int  uci_poll_bestmove(Uci *u, char move[6]);      /* engellemez: 1 hamle hazır, 0 henüz yok, -1 motor öldü */
int  uci_wait_bestmove(Uci *u, char move[6], int timeout_ms); /* 1, 0 zaman aşımı, -1 öldü */
void uci_stop(Uci *u);                             /* "stop" */
void uci_close(Uci *u);                            /* quit, 500 ms bekle, sonra SIGKILL + waitpid */
int  uci_read_fd(const Uci *u);                    /* poll için */
```
`fake_engine.sh`: `uci`→`id name Fake`/`uciok`, `isready`→`readyok`, `go …`→ `bestmove e2e4` (ortam değişkeni
`FAKE_MODE=die` ise `go`'da çıkar, `FAKE_MODE=hang` ise hiç yanıt vermez).

- [ ] **Step 1: Testler:** `test_levels_monotonic` (movetime artan; 6 tam güç); `test_level_out_of_range_is_3`;
  `test_uci_handshake_and_bestmove` (sahte motor → "e2e4"); `test_uci_engine_dies_returns_error` (`-1`);
  `test_uci_timeout` (hang → `uci_wait_bestmove(…, 300)` 0 döner); `test_uci_close_kills_engine`
  (close sonrası `kill(pid,0)` başarısız); `test_uci_missing_binary` (`uci_start` -1).
- [ ] **Step 2:** FAIL. **Step 3:** Uygula (fork/exec, iki boru, `O_NONBLOCK` okuma, satır tamponu).
- [ ] **Step 4:** OK. **Step 5: Commit** — `feat(chess): levels and UCI engine client`

---

### Task 5: Satranç ekranı

**Files:** Create `apps/chess/main.c`; Modify `Makefile` (`APPS += chess`, `stockfish` derleme hedefi)

Ekranlar (hepsi üst çubuklu, ✕ = bir önceki ekran / tahtada çıkış):
1. **Başlangıç:** kayıt varsa "Devam et", "Tek oyunculu", "İki oyunculu" büyük düğmeler.
2. **Seviye** (tek oyunculu): 6 düğme, Türkçe adlar (`level_get`).
3. **Renk** (tek oyunculu): "Beyaz", "Siyah", "Rastgele".
4. **Tahta:** 8×8, kare 128 px (1024 px), tahta oyuncunun rengi alta gelecek şekilde (iki oyunculuda `flip` ayarına
   göre her hamlede döner). Açık kare 255, koyu kare 0xBB; taşlar DejaVu glifleri (beyaz U+2654–2659 çerçeve, siyah
   U+265A–265F dolu), 100 px. Seçili kare kalın siyah çerçeve, yasal hedefler ortada küçük nokta, son hamlenin iki
   karesi ince çerçeve. Altta durum satırı ("Sıra: Beyaz", "Şah!", "Düşünüyor…") ve düğmeler: "Geri al", "Menü".
   Menü diyaloğu: "Yeni oyun", "Tahtayı çevir", "Kapat".
- Bir hamlede yalnız değişen kareler + durum satırı `kui_mark_dirty` (Plan 1 çoklu dikdörtgen).
- Dokunma: kendi taşına dokun → seç; yasal hedefe dokun → oyna; başka kendi taşına → seçimi değiştir; aynı kareye → bırak.
  Terfi → `kui_dialog("Terfi", "Taş seçin", {"Vezir","Kale","Fil","At"}, 4, redraw)`. Rok: şahı iki kare gitmek.
- Motor sırası: `uci_go` → döngü `kui_next_event(100)` + `uci_poll_bestmove`; ✕ → `uci_stop` + kaydet + çık;
  10 sn'de hamle yoksa veya motor ölürse `kui_dialog("Motor yanıt vermedi", …, {"Tekrar dene","İki oyunculu devam"})`.
- Bitiş: `game_result` ≠ ONGOING → `kui_dialog(result_text(...), …, {"Yeni oyun","Tahtaya bak"})`.
- Geri al: tek oyunculuda 2 yarım hamle (motor düşünüyorsa önce `uci_stop`), iki oyunculuda 1.
- Her hamlede `data/chess/save.txt` atomik; açılışta bozuksa başlangıç ekranı. `KUI_EV_WAKE` → tam çiz.
- Motor yolu: `$KUI_ROOT/apps/chess/stockfish`.

- [ ] **Step 1:** Yaz; `make build` (Makefile `stockfish` hedefi `tools/build-stockfish.sh` çağırır).
- [ ] **Step 2: Cihazda doğrula** (ekran görüntüleriyle): üç başlangıç ekranı; tek oyunculu Kolay'da birkaç hamle,
  motor ≤ 5 sn; Usta'da hamle ≤ 5 sn; rok, geçerken alma, terfi diyaloğu; Çoban matı ile mat diyaloğu;
  iki oyunculu + tahtayı çevir; motor düşünürken ✕ → hızla çıkar, `pidof stockfish` boş; güç tuşu ile uyut/uyandır;
  `kill -9 <stockfish>` → "Motor yanıt vermedi" diyaloğu.
- [ ] **Step 3: Commit** — `feat(chess): e-ink chess app with Stockfish`

---

### Task 6: Uyku Ayarı

**Files:** Create `apps/sleep/sleep_opts.c/.h`, `apps/sleep/main.c`; Test `tests/host/test_sleep_opts.c`

**Interfaces (Produces):**
```c
typedef struct { const char *label; int seconds; } SleepOpt;   /* seconds 0 = Hiç uyuma */
int  sleep_opts(const SleepOpt **out);                         /* 5 dk 300, 10 dk 600, 30 dk 1800, 1 saat 3600, 2 saat 7200, Hiç uyuma 0 */
int  sleep_current_index(int timeout_s, int prevent);          /* prevent=1 → "Hiç uyuma"; eşleşmeyen süre → -1 */
void sleep_describe(int timeout_s, int prevent, char *out, int cap); /* "Şu an: 10 dakika" / "Şu an: hiç uyumuyor" / "Şu an: 45 dakika" */
```
Ekran: üst çubuk "Uyku Ayarı", büyük "Şu an: …" satırı, 6 seçenek düğmesi (seçili olan siyah), altta not
("Hiç uyuma pili hızlı tüketir." + Task 1 sonucuna göre "Kindle yeniden başlayınca 10 dakikaya döner." gerekiyorsa).
Seçim: süre → `lipc-set-prop com.lab126.powerd preventScreenSaver 0` + `screenSaverTimeout <s>`; Hiç uyuma →
`preventScreenSaver 1`. Değerler her açılışta `lipc-get-prop` ile okunur (popen).

- [ ] **Step 1: Testler:** `test_opts_table`; `test_current_index_prevent_wins`; `test_current_index_unknown_is_minus1`;
  `test_describe_minutes_hours_never` ("10 dakika", "1 saat", "2 saat", "45 dakika", "hiç uyumuyor").
- [ ] **Step 2:** FAIL. **Step 3:** Uygula. **Step 4:** OK.
- [ ] **Step 5: Cihazda doğrula:** 30 dk seç → `lipc-get-prop … screenSaverTimeout` = 1800; Hiç uyuma → preventScreenSaver 1;
  10 dk seç → preventScreenSaver 0 ve 600; uygulamayı kapat-aç → doğru seçenek siyah. Ekran görüntüsü.
- [ ] **Step 6: Commit** — `feat(sleep): sleep timeout app`

---

### Task 7: Paketler, depo, yayın

**Files:** Create `apps/chess/pkg/*`, `apps/sleep/pkg/*`; Modify `Makefile` (`dist`, `deploy` paket listeleri), `tools/kindle-apps-depo.sh`,
`README.md`, `THIRD_PARTY.md` (Stockfish GPLv3), `core/pkg/manifest.json` ve `apps/game2048/pkg/manifest.json` sürümü `[2026,10,5]`.

- [ ] **Step 1:** `tests/pkg/test_pack.sh` artık 4 paketi bekler: `kindle-apps-{core,2048,chess,sleep}`; satranç paketi
  `files/stockfish` ve `files/COPYING-stockfish` içerir. Çalıştır → FAIL.
- [ ] **Step 2:** Paket şablonlarını yaz (Plan 1 2048 paketiyle aynı desen; chess install.sh stockfish'i de kopyalar ve `chmod +x`).
  `make build dist` → test OK. CI'da Stockfish derlemesi `make cross` içinde.
- [ ] **Step 3:** `make deploy` (4 paket) → menüde Satranç, 2048, Uyku Ayarı sırasıyla görünür (ekran görüntüsü).
- [ ] **Step 4:** Commit, `main`'e ff-merge, `v2026.10.5` etiketi, `gh workflow run release --ref v2026.10.5`, Pages manifestinde 4 paket;
  cihazda kpm ile `kindle-apps-chess` ve `kindle-apps-sleep` kurulumu çıkış 0.
- [ ] **Step 5: Commit** — `feat: package chess and sleep apps; release v2026.10.5`
