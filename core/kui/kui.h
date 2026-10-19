/* kui — minimal e-ink UI toolkit for Kindle (FBInk + evdev). */
#ifndef KUI_H
#define KUI_H

/* ---- geometry ---- */
typedef struct { int x, y, w, h; } KuiRect;
int     kui_rect_contains(KuiRect r, int px, int py);
KuiRect kui_rect_union(KuiRect a, KuiRect b); /* an empty rect (w == 0) is ignored */
KuiRect kui_grid_cell(KuiRect area, int cols, int rows, int col, int row, int gap);

/* ---- widgets: geometry and hit-testing (pure); drawing lives in widgets.c ---- */
#define KUI_TOPBAR_H 120
#define KUI_BTN_EXIT (-1)
#define KUI_BTN_MENU (-2)
typedef struct { KuiRect r; const char *label; int id; } KuiButton;
KuiRect kui_topbar_exit_rect(void);            /* top-left ✕ */
KuiRect kui_topbar_menu_rect(int screen_w);    /* top-right button */
int kui_topbar_hit(int screen_w, int has_menu, int x, int y); /* KUI_BTN_EXIT, KUI_BTN_MENU or 0 */
int kui_buttons_hit(const KuiButton *bs, int n, int x, int y); /* id or 0 */

/* ---- refresh policy ---- */
typedef enum { KUI_UPD_PARTIAL, KUI_UPD_FULL } KuiUpdKind;
typedef struct { int every; int count; } KuiRefreshPolicy; /* every == 0: never auto full */
void       kui_policy_init(KuiRefreshPolicy *p, int every);
KuiUpdKind kui_policy_next(KuiRefreshPolicy *p, int screen_changed);

/* ---- dirty regions: separate small rects are refreshed separately (unchanged pixels in between are left alone);
 * when more than KUI_MAX_DIRTY would be needed, overlapping/nearest rects are merged ---- */
#define KUI_MAX_DIRTY 16
typedef struct { KuiRect r[KUI_MAX_DIRTY]; int n; } KuiDirty;
void kui_dirty_add(KuiDirty *d, KuiRect r);

/* ---- UTF-8 and Turkish case rules ---- */
int  kui_utf8_decode(const char *s, int *cp); /* bytes consumed; invalid → 1 and cp = 0xFFFD */
int  kui_utf8_encode(int cp, char out[4]);    /* bytes written */
int  kui_utf8_len(const char *s);             /* code points */
int  kui_tr_upper(int cp);
int  kui_tr_lower(int cp);
int  kui_tr_fold_circumflex(int cp);
void kui_tr_upper_str(const char *in, char *out, int outsz);

/* ---- text wrapping: lines are byte ranges of the source text ---- */
typedef struct { int off, len; } KuiLine;
/* Greedy wrap to `width` using measure(s, n) (pixel width of n bytes); '\n' forces a break, a blank paragraph
 * gives an empty line, a word wider than the line is split between code points. Returns the line count (<= max). */
int kui_wrap(const char *text, int width, int (*measure)(const char *s, int n, void *ctx), void *ctx, KuiLine *out, int max);

/* ---- translations (Turkish is the source language) ---- */
const char *kui_t(const char *src);   /* translated text, or src */
#define T(s) kui_t(s)                 /* user-visible text */
#define N_(s) (s)                     /* marks text in static tables; translate where it is shown with T() */
typedef struct { const char *code, *name; } KuiLang;
const KuiLang *kui_languages(int *n); /* the 20 supported languages, Turkish first */
const char *kui_lang(void);           /* current code, e.g. "tr" */
void kui_i18n_set(const char *code);  /* unknown code → "tr" */
void kui_i18n_load_saved(void);       /* $KUI_ROOT/data/lang (kui_init calls it) */
int  kui_i18n_save(const char *code); /* 0 = ok */

/* ---- dates and numbers (names and separators follow the language) ---- */
void kui_set_num_format(const char *decimal_mark, const char *thousands_sep);
void kui_set_text_hook(const char *(*translate)(const char *)); /* used for day/month names; NULL = Turkish */
struct tm;
const char *kui_tr_weekday(int wday);       /* 0 = "Pazar"; "" when out of range */
const char *kui_tr_weekday_short(int wday); /* "Paz", "Pzt", … */
const char *kui_tr_month(int mon0);         /* 0 = "Ocak" */
void kui_tr_date(const struct tm *tm, char *out, int cap); /* "18 Ekim 2026, Pazar" */
void kui_fmt_num(double v, int decimals, char *out, int cap); /* 6632.39 → "6.632,39" */

/* ---- events and gestures ---- */
typedef enum { KUI_EV_NONE, KUI_EV_TAP, KUI_EV_LONGPRESS, KUI_EV_SWIPE, KUI_EV_WAKE, KUI_EV_QUIT, KUI_EV_POWER,
	       KUI_EV_DOWN, KUI_EV_MOVE, KUI_EV_UP } KuiEvType;
/* KUI_EV_DOWN / MOVE / UP: raw finger positions, only while kui_set_drag_events(1) is on (see below) */
/* KUI_EV_POWER: the power button was pressed (only while kui_grab_power_key(1) is on) */
typedef enum { KUI_DIR_NONE, KUI_DIR_LEFT, KUI_DIR_RIGHT, KUI_DIR_UP, KUI_DIR_DOWN } KuiDir;
typedef struct { KuiEvType type; int x, y; KuiDir dir; } KuiEvent;
typedef struct { int down, x0, y0, x, y; long t0_ms; } KuiGesture;
#define KUI_SWIPE_MIN_PX   80
#define KUI_LONGPRESS_MS   600
#define KUI_LONGPRESS_SLOP 40
/* Feed screen-space samples (down = 1 press/drag, 0 release); events are emitted on release only. */
KuiEvent kui_gesture_feed(KuiGesture *g, int down, int x, int y, long t_ms);
/* Drag stream (drag.c, pure): one contact's samples → KUI_EV_DOWN at the first sample, KUI_EV_MOVE for every
 * later sample at a new position, KUI_EV_UP (last position) on release, followed by the gesture event
 * (tap / long press / swipe) of the whole touch, so taps still work while drawing. contact: the touch's tracking
 * id (>= 0) while down, -1 when released; a new id without a release in between ends the old touch first.
 * Writes at most KUI_DRAG_MAX_OUT events to out; returns their number. */
#define KUI_DRAG_MAX_OUT 4
typedef struct { KuiGesture g; int down, id, x, y; } KuiDrag;
int kui_drag_feed(KuiDrag *d, int contact, int x, int y, long t_ms, KuiEvent *out);
/* Opt-in (default off, so other apps see only taps and swipes): kui_next_event also returns KUI_EV_DOWN/MOVE/UP
 * with screen coordinates. Slot 0 (the first finger) only; other fingers and palms are ignored. */
void kui_set_drag_events(int on);
/* Fast ink (fastink.c): refreshes r at once with the direct black/white waveform (DU, ~4× faster than GL16; no
 * flash). For black strokes on white only: grays in r are left as they are on the panel. Not part of kui_flush's
 * dirty list; skipped while the screensaver is up. */
void kui_flush_fast(KuiRect r);

/* ---- on-screen keyboard (pure layout + hit-test) ---- */
typedef enum { KUI_KB_TR_LETTERS, KUI_KB_SYMBOLS } KuiKbPage;
#define KUI_KEY_BACK  (-10)
#define KUI_KEY_ENTER (-11)
#define KUI_KEY_SHIFT (-12)
#define KUI_KEY_PAGE  (-13)
typedef struct { KuiRect r; int cp; char label[8]; } KuiKey; /* cp: code point, or a KUI_KEY_* special */
int kui_kb_layout(KuiRect area, KuiKbPage page, int shift, KuiKey *out, int max);
int kui_kb_hit(const KuiKey *keys, int n, int x, int y); /* key index or -1 */

/* ---- hashing ---- */
void kui_sha256(const void *data, int len, unsigned char out[32]);

/* ---- storage ---- */
const char *kui_data_dir(const char *app); /* $KUI_ROOT/data/<app>, created on demand */
int kui_write_atomic(const char *path, const void *buf, int len); /* 0 = ok */
int kui_read_file(const char *path, char *buf, int cap);          /* bytes read (NUL-terminated), -1 if missing */
int kui_cfg_get_int(const char *path, const char *key, int def);
int kui_cfg_set_int(const char *path, const char *key, int val);  /* 0 = ok */

/* ---- lipc (Kindle system properties) ---- */
int  kui_lipc_get_int(const char *pub, const char *prop, int def);
void kui_lipc_set_int(const char *pub, const char *prop, int v);
int  kui_lipc_get_str(const char *pub, const char *prop, char *out, int cap); /* 0 = ok */
/* Keep the Kindle awake while on: the previous preventScreenSaver value is saved to
 * /var/local/kindle-apps/prevent_ss.restore and put back by kui_prevent_sleep(0) — or by kui-run.sh if the app crashes. */
void kui_prevent_sleep(int on);

/* ---- device: screen (FBInk) and input (evdev + lipc) ---- */
typedef enum { KUI_FONT_REG, KUI_FONT_BOLD, KUI_FONT_SERIF, KUI_FONT_SERIF_BOLD } KuiFont; /* serif falls back to sans if missing */
typedef enum { KUI_ALIGN_LEFT, KUI_ALIGN_CENTER, KUI_ALIGN_RIGHT } KuiAlign;
#define KUI_ERR_TOUCH_BUSY (-2) /* touchscreen grabbed by another app (KOReader) */
int  kui_init(const char *app); /* 0 = ok */
void kui_quit(void);
int  kui_width(void);
int  kui_height(void);
void kui_fill(KuiRect r, unsigned char gray); /* 0 = black, 255 = white; draws into the buffer only */
void kui_frame(KuiRect r, int thickness, unsigned char gray);
void kui_text(KuiRect box, const char *utf8, KuiFont f, int px, KuiAlign a, unsigned char fg); /* vertically centred; blends onto
                                       * whatever is below — reliable for dark text only (FBInk Y8 bgless path) */
/* Text on a known solid background (use for light/white text): the glyphs' line box is painted with bg. */
void kui_text_on(KuiRect box, const char *utf8, KuiFont f, int px, KuiAlign a, unsigned char fg, unsigned char bg);
int  kui_text_width(const char *utf8, KuiFont f, int px);
int  kui_wrap_text(const char *text, int width, KuiFont f, int px, KuiLine *out, int max); /* kui_wrap with FBInk metrics */
int  kui_line_height(KuiFont f, int px); /* pixel advance between wrapped lines */
void kui_mark_dirty(KuiRect r);
void kui_flush(int screen_changed); /* the only place that refreshes the panel */
void kui_set_refresh_every(int n);
KuiEvent kui_next_event(int timeout_ms); /* KUI_EV_NONE on timeout */
int  kui_input_asleep(void); /* 1 while the screensaver is up: do not draw (KUI_EV_WAKE follows) */
int  kui_quit_pending(void); /* 1 once SIGTERM/SIGINT arrived (kui_next_event would return KUI_EV_QUIT) */
/* Takes the power button from powerd while on (it then arrives as KUI_EV_POWER instead of starting the screensaver);
 * released by kui_quit or when the process ends. 0 = ok, -1 if there is no power key device. */
int  kui_grab_power_key(int on);
void kui_drain_power_key(void); /* drops queued power-button events (e.g. the press that woke the device) */
void kui_wait_refresh(void);    /* waits until the e-ink panel finished the last refresh (before suspending) */

/* ---- power: Wi-Fi and timed suspend ---- */
int  kui_wifi_enabled(void);           /* 1 on, 0 off, -1 unknown */
void kui_wifi_enable(int on);
int  kui_wifi_wait(int timeout_s);     /* 1 once the Wi-Fi reports CONNECTED */
/* Suspends to RAM for about `seconds` (RTC alarm wakes it; the power button or a touch may wake it sooner).
 * Wi-Fi must be off (its wakeup source blocks suspend). Returns the seconds actually asleep, -1 if suspend failed. */
int  kui_suspend(int seconds);
int  kui_woke_early(int requested, int slept); /* pure: 1 when something other than the alarm woke the device */
/* battery saver: suspend (or, failing that, wait awake) until just after the next minute starts */
enum { KUI_SLEEP_MINUTE, KUI_SLEEP_WOKEN, KUI_SLEEP_QUIT };
int  kui_sleep_to_next_minute(void); /* KUI_SLEEP_WOKEN: a touch or the power button; QUIT: SIGTERM */
int  kui_ms_to_next_minute(void);    /* until 200 ms after the next minute boundary */
int  kui_sleep_until_minute(long target_epoch_min); /* same, until that minute (time()/60) begins */
int  kui_sleep_until_every(int every_min);           /* until the next multiple of every_min minutes */

/* "Pil ve ekran" (powerui.c): the battery saver's settings live in the app's settings file */
typedef enum { KUI_SAVER_IDLE_MIN, KUI_SAVER_CLOCK_MIN, KUI_SAVER_DATA_HOURS, KUI_SAVER_WX_HOURS, KUI_SAVER_LIGHT_OFF } KuiSaverKey;
int  kui_saver_get(const char *cfg, KuiSaverKey k); /* a valid choice, or the default */
enum { KUI_PS_SAVER = 1, KUI_PS_DATA = 2 };         /* show the saver rows / the news & currency row */
void kui_power_screen(const char *cfg, int flags);  /* cfg NULL: battery and front light only */
int  kui_light_get(void);                           /* front light 0..kui_light_max(), -1 unknown */
int  kui_light_max(void);
void kui_light_set(int v);
const char *kui_batt_log_path(void);
/* battery history: one "time level charging" line per change (bounded); the remaining time from the drain since
 * the charger was last unplugged (at least 2 % and an hour of data), -1 when unknown or charging */
void kui_batt_log(const char *path, long now, int level, int charging);
/* kui_battery() reads the level at most once per max age (default 60 s) and otherwise redraws the last value:
 * desk apps set it to their data interval so the battery is read together with the data, not every minute */
void kui_battery_max_age(long seconds);
void kui_battery_refresh(void);  /* the next kui_battery()/kui_battery_charging() reads again */
int  kui_battery_charging(void); /* cached like the level */
int  kui_battery_level(void);    /* cached; -1 unknown */
long kui_batt_estimate_s(const char *path, long now, int level);
void kui_fmt_duration(long s, char *out, int cap); /* "≈ 3 gün 4 sa", "≈ 5 sa 20 dk", "≈ 42 dk" */
/* Kolay okuma (Uygulamalar → Ayarlar): bold, black and about 1.3× larger text that still fits its box; applied by
 * kui_text / kui_text_on to every string. light: the text is drawn on a dark background (its colour is kept). */
void kui_a11y_adjust(int easy, const char *s, int box_w, int box_h, KuiFont *f, int *px, unsigned char *fg, int light,
		     int (*width)(const char *s, KuiFont f, int px, void *ctx), void *ctx);
int  kui_easy(void);   /* Kolay okuma on */
int  kui_simple(void); /* Basit mod on */
void kui_ui_set(const char *key, int v); /* "easy" / "simple", saved in data/ui; this process sees it at once */
/* crash report (crash.c): signal, address and the last touches go to the app's log */
void kui_crash_handler_install(const char *app);
void kui_crash_note_event(int type, int x, int y);
/* drops touches for ms (the tap that woke the device, or the rest of a tap that closed another app) */
void kui_drain_input(int ms);
void kui_topbar(const char *title, const char *right_label); /* right_label may be NULL */
int  kui_fit_px(const char *text, KuiFont f, int px, int max_w); /* largest size <= px whose single line fits max_w */
void kui_button(const KuiButton *b, int pressed);
/* battery gauge with percentage and ⚡ while charging (powerd); returns the level, -1 if unknown (nothing drawn) */
int  kui_battery(KuiRect r);
/* Modal dialog drawn over the current screen; returns the chosen index, -1 on ✕/quit.
 * The caller redraws its screen afterwards. */
int  kui_dialog(const char *title, const char *msg, const char **choices, int n, void (*redraw_bg)(void));
/* Modal text entry with the Turkish keyboard; secret shows ●. Returns 1 on Tamam (len >= min_len), 0 on ✕. */
int  kui_input_text(const char *title, char *buf, int cap, int secret, int min_len, void (*redraw_bg)(void));
/* redraw_bg (may be NULL) repaints the screen behind the dialog after a wake from the screensaver. */

#endif
