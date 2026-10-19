/* Not Defteri: handwritten notes and drawings with the finger, plus typed notes (lines entered with the on-screen
 * keyboard). Drawings have pages, three pen widths, an eraser, undo (replaying the remaining strokes), a plain /
 * grid / ruled background, and export a page as PNG to documents/Notlar. Ink is drawn into the framebuffer as the
 * finger moves and refreshed in small boxes with the fast black/white waveform (kui_flush_fast) every BATCH_MS. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "notlar/notlar.h"
#include "kui.h"

#define APP "notlar"
#define TOOL_H 136        /* the bottom toolbar of the drawing screen */
#define EDGE 6            /* touches starting this close to a bar do not draw (palm / edge rejection) */
#define BATCH_MS 35       /* ink refresh period while the finger moves */
#define SAVE_IDLE_MS 1500 /* save after this long without a stroke */
#define LINE_CAP 72       /* bytes per typed line (what the keyboard's entry field shows) */
#define TW 120            /* list thumbnails */
#define ROW_H 170
#define GRAY_TXT 0x55

static int W, H, CY, CH; /* screen; canvas origin (y) and height */
static NtMask mask;
static int quit;

/* ---- helpers ---- */

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* local time; a Kindle without a time zone runs on UTC: use Türkiye's (UTC+3, no DST) */
static void local_tm(time_t t, struct tm *tm)
{
	localtime_r(&t, tm);
	if (tm->tm_gmtoff == 0 && !getenv("TZ")) {
		t += 3 * 3600;
		gmtime_r(&t, tm);
	}
}

static void fmt_date(long t, char *out, int cap)
{
	struct tm tm;
	local_tm((time_t) t, &tm);
	char d[96];
	kui_tr_date(&tm, d, sizeof d);
	snprintf(out, (size_t) cap, "%s %02d:%02d", d, tm.tm_hour, tm.tm_min);
}

/* one line of text in box, cut with "…" when too wide */
static void text_cut(KuiRect box, const char *s, KuiFont f, int px, KuiAlign a, unsigned char fg)
{
	char buf[256];
	snprintf(buf, sizeof buf, "%s", s);
	int len = (int) strlen(buf);
	if (kui_text_width(buf, f, px) > box.w) {
		while (len > 0) {
			do len--;
			while (len > 0 && ((unsigned char) buf[len] & 0xC0) == 0x80);
			while (len > 0 && buf[len - 1] == ' ') len--;
			strcpy(buf + len, "…");
			if (kui_text_width(buf, f, px) <= box.w) break;
		}
	}
	kui_text(box, buf, f, px, a, fg);
}

static void note_path(const char *file, char *out, int cap) { snprintf(out, (size_t) cap, "%s/%s", kui_data_dir(APP), file); }

static int note_empty(const NtNote *n)
{
	const char *t = n->title;
	while (*t == ' ') t++;
	return !*t && nt_note_strokes(n) == 0 && nt_para_count(n->text) == 0;
}

/* saves the note; an empty one is removed instead. 0 = ok */
static int save_note(const NtNote *n, const char *file)
{
	char path[400];
	note_path(file, path, sizeof path);
	if (note_empty(n)) {
		unlink(path);
		return 0;
	}
	if (nt_note_save(path, n) == 0) return 0;
	const char *ok[] = { T("Tamam") };
	kui_dialog(T("Not Defteri"), T("Not kaydedilemedi."), ok, 1, NULL);
	return -1;
}

/* ---- drawing screen ---- */

enum { B_THIN, B_MED, B_THICK, B_ERASER, B_UNDO, B_PREV, B_PAGE, B_NEXT, NB };

static NtNote *note;
static const char *note_file;
static int page, tool = NT_PEN_MED;
static int stroking, si, unsaved;
static NtSmooth smooth;
static KuiRect pend; /* canvas box drawn into the framebuffer but not refreshed yet */
static long last_ink, last_stroke;

static NtPage *pg(void) { return &note->pages[page]; }

static KuiRect bar_rect(int i)
{
	int bw = W / NB;
	return (KuiRect){ i * bw, H - TOOL_H, i == NB - 1 ? W - i * bw : bw, TOOL_H };
}

static KuiRect to_screen(KuiRect r) { return (KuiRect){ r.x, r.y + CY, r.w, r.h }; }

/* the canvas box r (canvas coordinates) into the framebuffer: background (with_bg) and ink */
static void blit(KuiRect r, int with_bg)
{
	if (r.w <= 0 || r.h <= 0) return;
	if (with_bg) {
		KuiRect rs[96];
		kui_fill(to_screen(r), 255);
		int n = nt_bg_rects(note->bg, r, mask.w, mask.h, rs, 96);
		for (int i = 0; i < n; i++) kui_fill(to_screen(rs[i]), NT_BG_GRAY);
	}
	for (int y = r.y; y < r.y + r.h; y++) {
		const unsigned char *row = mask.px + (size_t) y * (size_t) mask.w;
		for (int x = r.x, end = r.x + r.w; x < end;) {
			if (!row[x]) {
				x++;
				continue;
			}
			int s = x;
			while (x < end && row[x]) x++;
			kui_fill((KuiRect){ s, CY + y, x - s, 1 }, 0);
		}
	}
}

static void draw_title(void)
{
	char t[NT_TITLE_MAX + 32];
	nt_note_label(note, t, sizeof t);
	kui_topbar(t[0] ? t : T("Adsız çizim"), T("Menü"));
}

static void draw_toolbar(void)
{
	static const char *names[4] = { N_("İnce"), N_("Orta"), N_("Kalın"), N_("Silgi") };
	KuiRect bar = { 0, H - TOOL_H, W, TOOL_H };
	kui_fill(bar, 255);
	kui_fill((KuiRect){ 0, bar.y, W, 4 }, 0);
	for (int i = 0; i < NB; i++) {
		KuiRect r = bar_rect(i), in = { r.x + 6, r.y + 12, r.w - 12, r.h - 18 };
		if (i > 0 && i != B_PAGE && i != B_NEXT) kui_fill((KuiRect){ r.x, r.y + 16, 2, r.h - 28 }, 0xAA);
		if (i <= B_ERASER) {
			int sel = i == tool;
			unsigned char fg = sel ? 255 : 0, bgc = sel ? 0 : 255;
			if (sel) kui_fill(in, 0);
			kui_text_on((KuiRect){ in.x, in.y + 6, in.w, 50 }, T(names[i]), KUI_FONT_BOLD, kui_fit_px(T(names[i]), KUI_FONT_BOLD, 32, in.w - 8),
				    KUI_ALIGN_CENTER, fg, bgc);
			if (i < B_ERASER) { /* a sample of the pen's width */
				int t = 2 * nt_tool_radius(i) + 1;
				kui_fill((KuiRect){ in.x + 20, in.y + 82 - t / 2, in.w - 40, t }, fg);
			} else {
				kui_frame((KuiRect){ in.x + in.w / 2 - 22, in.y + 66, 44, 34 }, 3, fg);
			}
		} else if (i == B_UNDO) {
			unsigned char fg = pg()->ns ? 0 : 0xAA;
			kui_text((KuiRect){ in.x, in.y + 6, in.w, 50 }, T("Geri al"), KUI_FONT_BOLD, kui_fit_px(T("Geri al"), KUI_FONT_BOLD, 32, in.w - 8), KUI_ALIGN_CENTER, fg);
			kui_text((KuiRect){ in.x, in.y + 56, in.w, 56 }, "↶", KUI_FONT_BOLD, 48, KUI_ALIGN_CENTER, fg);
		} else if (i == B_PREV) {
			kui_text(in, "‹", KUI_FONT_BOLD, 80, KUI_ALIGN_CENTER, page > 0 ? 0 : 0xAA);
		} else if (i == B_PAGE) {
			char s[32];
			snprintf(s, sizeof s, "%d/%d", page + 1, note->npages);
			kui_text((KuiRect){ in.x, in.y, in.w, 60 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 40, in.w), KUI_ALIGN_CENTER, 0);
			kui_text((KuiRect){ in.x, in.y + 60, in.w, 44 }, T("sayfa"), KUI_FONT_REG, kui_fit_px(T("sayfa"), KUI_FONT_REG, 28, in.w), KUI_ALIGN_CENTER, GRAY_TXT);
		} else {
			int last = page == note->npages - 1;
			kui_text(in, last ? "+" : "›", KUI_FONT_BOLD, 80, KUI_ALIGN_CENTER, 0);
		}
	}
	kui_mark_dirty(bar);
}

/* flush: 1 full refresh, 0 partial, -1 none (a dialog's background after a wake) */
static void draw_page(int flush)
{
	kui_fill((KuiRect){ 0, 0, W, H }, 255);
	draw_title();
	nt_page_render(&mask, pg());
	blit((KuiRect){ 0, 0, mask.w, mask.h }, 1);
	draw_toolbar();
	if (flush < 0) return;
	if (flush) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, 0, W, H });
		kui_flush(0);
	}
}

static void redraw_page_bg(void) { draw_page(-1); }

/* the canvas again after the strokes changed (undo, clear): one partial refresh of the canvas */
static void redraw_canvas(void)
{
	nt_page_render(&mask, pg());
	KuiRect all = { 0, 0, mask.w, mask.h };
	blit(all, 1);
	kui_mark_dirty(to_screen(all));
	draw_toolbar();
	kui_flush(0);
}

/* new ink on the panel: pen strokes only add black, so the fast black/white update is exact; the eraser
 * brings back the gray background, which needs the normal (gray) partial refresh unless the page is plain */
static void ink_flush(void)
{
	last_ink = now_ms();
	if (!pend.w) return;
	KuiRect s = to_screen(pend);
	int eraser = stroking && tool == NT_ERASER;
	if (!stroking && pg()->ns) eraser = pg()->s[pg()->ns - 1].tool == NT_ERASER;
	blit(pend, eraser);
	if (eraser && note->bg != NT_BG_PLAIN) {
		kui_mark_dirty(s);
		kui_flush(0);
	} else {
		kui_flush_fast(s);
	}
	pend.w = 0;
}

static int in_canvas(int y) { return y >= CY + EDGE && y < H - TOOL_H - EDGE; }

static void clamp_canvas(int *x, int *y)
{
	*y -= CY;
	if (*x < 0) *x = 0;
	if (*x >= mask.w) *x = mask.w - 1;
	if (*y < 0) *y = 0;
	if (*y >= mask.h) *y = mask.h - 1;
}

static void stroke_begin(int x, int y)
{
	clamp_canvas(&x, &y);
	si = nt_page_begin(pg(), tool, x, y);
	if (si < 0) return; /* the page is full */
	stroking = 1;
	nt_smooth_start(&smooth, x, y);
	pend = kui_rect_union(pend, nt_stroke_draw(&mask, pg(), si, 0));
	if (now_ms() - last_ink >= BATCH_MS) ink_flush(); /* the first dot at once */
}

static void stroke_point(int x, int y)
{
	if (nt_page_add(pg(), x, y) < 0) return;
	pend = kui_rect_union(pend, nt_stroke_draw(&mask, pg(), si, pg()->s[si].n - 1));
}

static void stroke_move(int x, int y)
{
	int ox, oy;
	clamp_canvas(&x, &y);
	if (nt_smooth_feed(&smooth, x, y, &ox, &oy)) stroke_point(ox, oy);
	if (now_ms() - last_ink >= BATCH_MS) ink_flush();
}

static void stroke_end(void)
{
	int ox, oy;
	if (!stroking) return;
	if (nt_smooth_end(&smooth, &ox, &oy)) stroke_point(ox, oy);
	ink_flush();
	stroking = 0;
	nt_page_end(pg(), 0.6); /* the stored stroke differs from the drawn one by < 1 px */
	unsaved = 1;
	note->modified = (long) time(NULL);
	last_stroke = now_ms();
	if (pg()->ns == 1) { /* "Geri al" turns active */
		draw_toolbar();
		kui_flush(0);
	}
}

static void flush_save(void)
{
	if (!unsaved) return;
	save_note(note, note_file);
	unsaved = 0;
}

static void export_page(void)
{
	const char *root = getenv("KUI_ROOT");
	char dir[300], path[400], shown[200], msg[300];
	snprintf(dir, sizeof dir, "%s", root && *root ? root : "/mnt/us/kindle-apps");
	char *slash = strrchr(dir, '/');
	if (slash && slash != dir) *slash = '\0'; /* next to kindle-apps: /mnt/us */
	strncat(dir, "/documents", sizeof dir - strlen(dir) - 1);
	mkdir(dir, 0755);
	strncat(dir, "/Notlar", sizeof dir - strlen(dir) - 1);
	mkdir(dir, 0755);
	struct tm tm;
	local_tm(time(NULL), &tm);
	snprintf(shown, sizeof shown, "not-%04d%02d%02d-%02d%02d-s%d.png", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, page + 1);
	snprintf(path, sizeof path, "%s/%s", dir, shown);
	unsigned char *png;
	int len, rc = -1;
	nt_page_render(&mask, pg());
	if (nt_png(&mask, note->bg, &png, &len) == 0) {
		rc = kui_write_atomic(path, png, len);
		free(png);
	}
	const char *ok[] = { T("Tamam") };
	if (rc == 0) snprintf(msg, sizeof msg, T("Sayfa resim olarak kaydedildi:\ndocuments/Notlar\n%s"), shown);
	else snprintf(msg, sizeof msg, "%s", T("Resim kaydedilemedi."));
	kui_dialog(T("Dışa aktar"), msg, ok, 1, redraw_page_bg);
}

static void page_menu(void)
{
	char title[64];
	snprintf(title, sizeof title, T("Sayfa %d"), page + 1);
	const char *ch[] = { T("Temizle"), T("Sil"), T("Vazgeç") };
	int c = kui_dialog(title, T("Temizle: sayfadaki çizimler silinir.\nSil: sayfa nottan çıkarılır."), ch, 3, redraw_page_bg);
	if (c == 0) {
		nt_page_clear(pg());
		unsaved = 1;
	} else if (c == 1) {
		nt_note_del_page(note, page);
		if (page >= note->npages) page = note->npages - 1;
		unsaved = 1;
	}
}

static void menu(void)
{
	const char *ch[] = { T("Arka plan"), T("Başlık"), T("Sayfa"), T("Dışa aktar") };
	char msg[128];
	snprintf(msg, sizeof msg, T("Sayfa %d / %d · %d çizgi"), page + 1, note->npages, pg()->ns);
	int c = kui_dialog(T("Menü"), msg, ch, 4, redraw_page_bg);
	int full = 0;
	if (c == 0) {
		const char *bgs[] = { T("Düz"), T("Kareli"), T("Çizgili") };
		int b = kui_dialog(T("Arka plan"), T("Bütün sayfalar için"), bgs, 3, redraw_page_bg);
		if (b >= 0 && b != note->bg) {
			note->bg = b;
			unsaved = 1;
		}
	} else if (c == 1) {
		char buf[NT_TITLE_MAX];
		snprintf(buf, sizeof buf, "%s", note->title);
		if (kui_input_text(T("Başlık"), buf, sizeof buf, 0, 0, NULL)) {
			snprintf(note->title, sizeof note->title, "%s", buf);
			unsaved = 1;
		}
		full = 1; /* back from the full-screen keyboard */
	} else if (c == 2) {
		page_menu();
	} else if (c == 3) {
		export_page();
	}
	if (kui_quit_pending()) quit = 1;
	flush_save();
	if (!quit) draw_page(full);
}

static void go_page(int p)
{
	if (p < 0 || p >= note->npages) return;
	page = p;
	draw_page(1); /* another page: a full refresh clears the ghosts */
}

static void on_toolbar(int x, int y)
{
	for (int i = 0; i < NB; i++) {
		if (!kui_rect_contains(bar_rect(i), x, y)) continue;
		if (i <= B_ERASER) {
			tool = i;
			draw_toolbar();
			kui_flush(0);
		} else if (i == B_UNDO) {
			if (nt_page_undo(pg())) {
				unsaved = 1;
				note->modified = (long) time(NULL);
				redraw_canvas();
			}
		} else if (i == B_PREV) {
			flush_save();
			go_page(page - 1);
		} else if (i == B_NEXT) {
			flush_save();
			if (page == note->npages - 1) {
				if (nt_note_add_page(note, note->npages) < 0) return;
				unsaved = 1;
			}
			go_page(page + 1);
		}
		return;
	}
}

static void draw_screen(NtNote *n, const char *file)
{
	note = n;
	note_file = file;
	page = 0;
	stroking = unsaved = 0;
	pend.w = 0;
	last_ink = 0;
	draw_page(1);
	kui_set_drag_events(1);
	while (!quit) {
		int timeout = -1;
		long now = now_ms();
		if (pend.w) timeout = (int) (BATCH_MS - (now - last_ink) > 0 ? BATCH_MS - (now - last_ink) : 0);
		else if (unsaved && !stroking) timeout = (int) (SAVE_IDLE_MS - (now - last_stroke) > 0 ? SAVE_IDLE_MS - (now - last_stroke) : 0);
		KuiEvent e = kui_next_event(timeout);
		switch (e.type) {
		case KUI_EV_QUIT: quit = 1; break;
		case KUI_EV_NONE:
			if (pend.w) ink_flush();
			else if (!stroking && now_ms() - last_stroke >= SAVE_IDLE_MS) flush_save();
			break;
		case KUI_EV_WAKE: /* the screensaver took the touch: end the stroke where it was */
			stroke_end();
			draw_page(1);
			break;
		case KUI_EV_DOWN:
			if (in_canvas(e.y)) stroke_begin(e.x, e.y);
			break;
		case KUI_EV_MOVE:
			if (stroking) stroke_move(e.x, e.y);
			break;
		case KUI_EV_UP: stroke_end(); break;
		case KUI_EV_TAP: {
			if (e.y >= H - TOOL_H) {
				on_toolbar(e.x, e.y);
				break;
			}
			int hit = e.y < CY ? kui_topbar_hit(W, 1, e.x, e.y) : 0;
			if (hit == KUI_BTN_EXIT) {
				kui_set_drag_events(0);
				flush_save();
				return;
			}
			if (hit == KUI_BTN_MENU) menu();
			break; /* a tap on the canvas was a dot */
		}
		default: break;
		}
	}
	kui_set_drag_events(0);
	stroke_end();
	flush_save();
}

/* ---- typed notes: a list of lines ---- */

#define MAX_BLOCKS 64
static KuiRect r_block[MAX_BLOCKS];
static int block_para[MAX_BLOCKS], n_blocks, t_first, t_more;

static void draw_text_note(int full)
{
	kui_fill((KuiRect){ 0, 0, W, H }, 255);
	kui_topbar(T("Yazı notu"), T("Ekle")); /* the first line is the note's name in the list */
	int np = nt_para_count(note->text), y = CY + 24, px = 42, lh = kui_line_height(KUI_FONT_SERIF, px);
	n_blocks = 0;
	t_more = 0;
	if (np == 0) {
		kui_text((KuiRect){ 60, H / 2 - 150, W - 120, 300 }, T("Boş not.\n“Ekle” ile bir satır yazın."), KUI_FONT_REG, 40, KUI_ALIGN_CENTER, GRAY_TXT);
	}
	for (int i = t_first; i < np && n_blocks < MAX_BLOCKS; i++) {
		char buf[LINE_CAP * 2 + 8];
		KuiLine lines[16];
		nt_para_get(note->text, i, buf, sizeof buf);
		int nl = kui_wrap_text(buf, W - 140, KUI_FONT_SERIF, px, lines, 16);
		if (nl < 1) nl = 1;
		int bh = nl * lh + 30;
		if (y + bh > H - 70) {
			t_more = 1;
			break;
		}
		kui_text((KuiRect){ 30, y + 15, 50, lh }, "•", KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 0);
		for (int l = 0; l < nl; l++) {
			char part[LINE_CAP * 2 + 8];
			snprintf(part, sizeof part, "%.*s", lines[l].len, buf + lines[l].off);
			kui_text((KuiRect){ 90, y + 15 + l * lh, W - 140, lh }, part, KUI_FONT_SERIF, px, KUI_ALIGN_LEFT, 0);
		}
		r_block[n_blocks] = (KuiRect){ 0, y, W, bh };
		block_para[n_blocks++] = i;
		y += bh;
		kui_fill((KuiRect){ 30, y - 2, W - 60, 2 }, 0xCC);
	}
	if (t_more || t_first > 0) {
		char s[96];
		snprintf(s, sizeof s, T("%d–%d / %d · kaydırın"), t_first + 1, t_first + n_blocks, np);
		kui_text((KuiRect){ 20, H - 60, W - 40, 50 }, s, KUI_FONT_REG, 28, KUI_ALIGN_CENTER, GRAY_TXT);
	} else if (np > 0) {
		kui_text((KuiRect){ 20, H - 60, W - 40, 50 }, T("Dokunun: düzenle · basılı tutun: sil"), KUI_FONT_REG, 28, KUI_ALIGN_CENTER, GRAY_TXT);
	}
	if (full < 0) return; /* -1: no refresh (a dialog's background) */
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, 0, W, H });
		kui_flush(0);
	}
}

static void redraw_text_bg(void) { draw_text_note(-1); }

static void add_line(void)
{
	char buf[LINE_CAP] = "";
	if (kui_input_text(T("Yeni satır"), buf, sizeof buf, 0, 1, NULL)) {
		if (nt_para_set(&note->text, nt_para_count(note->text), buf) == 0) {
			note->modified = (long) time(NULL);
			save_note(note, note_file);
		}
		int np = nt_para_count(note->text); /* keep the new line on screen */
		if (np - t_first > 6) t_first = np - 6;
	}
	if (kui_quit_pending()) quit = 1;
}

static void text_screen(NtNote *n, const char *file, int start_typing)
{
	note = n;
	note_file = file;
	t_first = 0;
	if (start_typing) add_line();
	if (quit) return;
	draw_text_note(1);
	while (!quit) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) {
			quit = 1;
			break;
		}
		if (e.type == KUI_EV_WAKE) {
			draw_text_note(1);
			continue;
		}
		if (e.type == KUI_EV_SWIPE) {
			int np = nt_para_count(note->text);
			if ((e.dir == KUI_DIR_UP || e.dir == KUI_DIR_LEFT) && t_more) t_first += n_blocks > 1 ? n_blocks - 1 : 1;
			else if ((e.dir == KUI_DIR_DOWN || e.dir == KUI_DIR_RIGHT) && t_first > 0) t_first = t_first > 5 ? t_first - 5 : 0;
			else continue;
			if (t_first >= np) t_first = np > 0 ? np - 1 : 0;
			draw_text_note(0);
			continue;
		}
		if (e.type != KUI_EV_TAP && e.type != KUI_EV_LONGPRESS) continue;
		int hit = kui_topbar_hit(W, 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) {
			add_line();
			if (!quit) draw_text_note(1);
			continue;
		}
		for (int b = 0; b < n_blocks; b++) {
			if (!kui_rect_contains(r_block[b], e.x, e.y)) continue;
			int pi = block_para[b];
			char buf[LINE_CAP];
			nt_para_get(note->text, pi, buf, sizeof buf);
			int changed = 0;
			if (e.type == KUI_EV_LONGPRESS) {
				const char *ch[] = { T("Sil"), T("Vazgeç") };
				if (kui_dialog(T("Satır silinsin mi?"), buf, ch, 2, redraw_text_bg) == 0) changed = nt_para_set(&note->text, pi, "") == 0;
				if (kui_quit_pending()) quit = 1;
				if (changed) {
					note->modified = (long) time(NULL);
					save_note(note, note_file);
				}
				if (!quit) draw_text_note(0);
			} else {
				kui_frame((KuiRect){ r_block[b].x + 10, r_block[b].y + 4, r_block[b].w - 20, r_block[b].h - 8 }, 4, 0); /* pressed */
				kui_mark_dirty(r_block[b]);
				kui_flush(0);
				if (kui_input_text(T("Satırı düzenle"), buf, sizeof buf, 0, 0, NULL)) changed = nt_para_set(&note->text, pi, buf) == 0; /* empty: deleted */
				if (kui_quit_pending()) quit = 1;
				if (changed) {
					note->modified = (long) time(NULL);
					save_note(note, note_file);
				}
				if (!quit) draw_text_note(1);
			}
			break;
		}
	}
	save_note(note, note_file);
}

/* ---- note list ---- */

typedef struct {
	char file[64];
	int kind, npages, nparas;
	long modified;
	char label[160];
	unsigned char *thumb; /* TW × th grays, drawings only */
} Entry;

static Entry *ents;
static int n_ents, l_first, th_h;
static KuiRect r_row[16];
static int n_rows;

static void free_list(void)
{
	for (int i = 0; i < n_ents; i++) free(ents[i].thumb);
	free(ents);
	ents = NULL;
	n_ents = 0;
}

static int by_modified(const void *a, const void *b)
{
	const Entry *x = a, *y = b;
	return x->modified < y->modified ? 1 : x->modified > y->modified ? -1 : strcmp(y->file, x->file);
}

static void load_list(void)
{
	free_list();
	DIR *d = opendir(kui_data_dir(APP));
	if (!d) return;
	struct dirent *de;
	int cap = 0;
	while ((de = readdir(d))) {
		int len = (int) strlen(de->d_name);
		if (len < 5 || len >= 64 || strcmp(de->d_name + len - 4, ".ntl") != 0) continue;
		char path[400];
		NtNote n;
		note_path(de->d_name, path, sizeof path);
		if (nt_note_load(path, &n) != 0) continue;
		if (n_ents == cap) {
			cap = cap ? cap * 2 : 32;
			Entry *e = realloc(ents, sizeof *e * (size_t) cap);
			if (!e) {
				nt_note_free(&n);
				break;
			}
			ents = e;
		}
		Entry *e = &ents[n_ents++];
		memset(e, 0, sizeof *e);
		snprintf(e->file, sizeof e->file, "%s", de->d_name);
		e->kind = n.kind;
		e->npages = n.npages;
		e->nparas = nt_para_count(n.text);
		e->modified = n.modified;
		nt_note_label(&n, e->label, sizeof e->label);
		if (n.kind == NT_KIND_DRAW && n.npages > 0 && (e->thumb = malloc((size_t) (TW * th_h)))) {
			nt_page_render(&mask, &n.pages[0]);
			nt_thumb(&mask, e->thumb, TW, th_h);
		}
		nt_note_free(&n);
	}
	closedir(d);
	qsort(ents, (size_t) n_ents, sizeof *ents, by_modified);
}

static void draw_thumb(const Entry *e, int x, int y)
{
	KuiRect fr = { x - 3, y - 3, TW + 6, th_h + 6 };
	kui_fill(fr, 255);
	kui_frame(fr, 3, 0);
	if (!e->thumb) { /* a typed note: a few lines of "text" */
		for (int i = 0; i < 5; i++) kui_fill((KuiRect){ x + 16, y + 24 + i * 22, i == 4 ? TW / 2 : TW - 32, 8 }, 0x99);
		return;
	}
	for (int ty = 0; ty < th_h; ty++) { /* 4 grays, as runs */
		const unsigned char *row = e->thumb + ty * TW;
		for (int tx = 0; tx < TW;) {
			int q = row[tx] / 64, s = tx;
			while (tx < TW && row[tx] / 64 == q) tx++;
			if (q < 3) kui_fill((KuiRect){ x + s, y + ty, tx - s, 1 }, (unsigned char) (q * 0x55));
		}
	}
}

static int rows_per_page(void) { return (H - CY - 100) / ROW_H; }

static void draw_list(int full)
{
	kui_fill((KuiRect){ 0, 0, W, H }, 255);
	kui_topbar(T("Not Defteri"), T("Yeni"));
	int per = rows_per_page();
	n_rows = 0;
	if (n_ents == 0) {
		kui_text((KuiRect){ 60, H / 2 - 200, W - 120, 400 }, T("Henüz not yok.\n“Yeni” ile bir çizim ya da yazı notu başlatın."), KUI_FONT_REG, 42,
			 KUI_ALIGN_CENTER, GRAY_TXT);
	}
	for (int i = l_first; i < n_ents && n_rows < per && n_rows < 16; i++) {
		const Entry *e = &ents[i];
		int y = CY + 10 + n_rows * ROW_H;
		r_row[n_rows++] = (KuiRect){ 0, y, W, ROW_H };
		draw_thumb(e, 30, y + (ROW_H - th_h) / 2);
		const char *label = e->label[0] ? e->label : e->kind == NT_KIND_DRAW ? T("Adsız çizim") : T("Boş yazı notu");
		text_cut((KuiRect){ 30 + TW + 30, y + 22, W - TW - 90, 60 }, label, KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
		char date[128], info[200];
		fmt_date(e->modified, date, sizeof date);
		if (e->kind == NT_KIND_DRAW) snprintf(info, sizeof info, T("Çizim · %d sayfa"), e->npages);
		else snprintf(info, sizeof info, T("Yazı · %d satır"), e->nparas);
		text_cut((KuiRect){ 30 + TW + 30, y + 84, W - TW - 90, 40 }, info, KUI_FONT_REG, 30, KUI_ALIGN_LEFT, GRAY_TXT);
		text_cut((KuiRect){ 30 + TW + 30, y + 122, W - TW - 90, 36 }, date, KUI_FONT_REG, 26, KUI_ALIGN_LEFT, GRAY_TXT);
		kui_fill((KuiRect){ 20, y + ROW_H - 2, W - 40, 2 }, 0xCC);
	}
	if (n_ents > per) {
		char s[96];
		snprintf(s, sizeof s, T("%d–%d / %d · kaydırın"), l_first + 1, l_first + n_rows, n_ents);
		kui_text((KuiRect){ 20, H - 70, W - 40, 56 }, s, KUI_FONT_REG, 30, KUI_ALIGN_CENTER, GRAY_TXT);
	}
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, 0, W, H });
		kui_flush(0);
	}
}

static void redraw_list_bg(void)
{
	kui_fill((KuiRect){ 0, 0, W, H }, 255);
	kui_topbar(T("Not Defteri"), T("Yeni"));
}

static void open_note(const char *file, NtNote *n, int is_new)
{
	if (n->kind == NT_KIND_DRAW) draw_screen(n, file);
	else text_screen(n, file, is_new);
}

static void new_note(void)
{
	const char *ch[] = { T("Çizim"), T("Yazı"), T("Vazgeç") };
	int c = kui_dialog(T("Yeni not"), T("Parmakla çizim mi, klavyeyle yazı mı?"), ch, 3, redraw_list_bg);
	if (c != 0 && c != 1) return;
	NtNote n;
	long now = (long) time(NULL);
	if (nt_note_init(&n, c == 0 ? NT_KIND_DRAW : NT_KIND_TEXT, now) != 0) return;
	char file[64], path[400];
	for (long k = now;; k++) { /* a name not used yet */
		snprintf(file, sizeof file, "n%ld.ntl", k);
		note_path(file, path, sizeof path);
		if (access(path, F_OK) != 0) break;
	}
	open_note(file, &n, 1);
	nt_note_free(&n);
}

int main(void)
{
	if (kui_init(APP) != 0) return 1;
	kui_set_refresh_every(0); /* full refreshes only on a new screen */
	W = kui_width();
	H = kui_height();
	CY = KUI_TOPBAR_H;
	CH = H - CY - TOOL_H;
	mask = (NtMask){ calloc((size_t) W * (size_t) CH, 1), W, CH };
	th_h = TW * CH / W;
	if (!mask.px) return 1;
	load_list();
	draw_list(1);
	while (!quit) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) {
			draw_list(1);
			continue;
		}
		if (e.type == KUI_EV_SWIPE) {
			int per = rows_per_page();
			if ((e.dir == KUI_DIR_UP || e.dir == KUI_DIR_LEFT) && l_first + per < n_ents) l_first += per;
			else if ((e.dir == KUI_DIR_DOWN || e.dir == KUI_DIR_RIGHT) && l_first > 0) l_first = l_first > per ? l_first - per : 0;
			else continue;
			draw_list(0);
			continue;
		}
		if (e.type != KUI_EV_TAP && e.type != KUI_EV_LONGPRESS) continue;
		int hit = kui_topbar_hit(W, 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) {
			new_note();
			if (quit || kui_quit_pending()) break;
			load_list();
			l_first = 0;
			draw_list(1);
			continue;
		}
		for (int r = 0; r < n_rows; r++) {
			if (!kui_rect_contains(r_row[r], e.x, e.y)) continue;
			Entry *en = &ents[l_first + r];
			char path[400];
			note_path(en->file, path, sizeof path);
			if (e.type == KUI_EV_LONGPRESS) {
				const char *ch[] = { T("Sil"), T("Vazgeç") };
				const char *label = en->label[0] ? en->label : en->kind == NT_KIND_DRAW ? T("Adsız çizim") : T("Boş yazı notu");
				if (kui_dialog(T("Not silinsin mi?"), label, ch, 2, redraw_list_bg) == 0) unlink(path);
				if (kui_quit_pending()) quit = 1;
				load_list();
				if (l_first >= n_ents) l_first = l_first >= rows_per_page() ? l_first - rows_per_page() : 0;
				if (!quit) draw_list(0);
				break;
			}
			kui_frame((KuiRect){ r_row[r].x + 8, r_row[r].y + 2, r_row[r].w - 16, r_row[r].h - 6 }, 5, 0); /* pressed */
			kui_mark_dirty(r_row[r]);
			kui_flush(0);
			NtNote n;
			char file[64];
			snprintf(file, sizeof file, "%s", en->file);
			if (nt_note_load(path, &n) == 0) {
				open_note(file, &n, 0);
				nt_note_free(&n);
			}
			if (quit) break;
			load_list();
			draw_list(1);
			break;
		}
	}
	free_list();
	free(mask.px);
	kui_quit();
	return 0;
}
