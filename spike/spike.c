/* Throwaway device spike: FBInk on MTK, waveform modes, touch input. */
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "fbink.h"

#define BITS_LONG (sizeof(long) * 8)
#define TESTBIT(b, a) ((a[(b) / BITS_LONG] >> ((b) % BITS_LONG)) & 1)

static int fb;
static FBInkConfig cfg;

static void box(int x, int y, int w, int h, int gray, WFM_MODE_INDEX_T wfm, const char *name)
{
	FBInkRect r = { (unsigned short) x, (unsigned short) y, (unsigned short) w, (unsigned short) h };
	cfg.no_refresh = true;
	fbink_fill_rect_gray(fb, &cfg, &r, false, (uint8_t) gray);
	FBInkConfig rc = cfg;
	rc.wfm_mode = wfm;
	rc.is_flashing = false;
	int ret = fbink_refresh(fb, (uint32_t) y, (uint32_t) x, (uint32_t) w, (uint32_t) h, &rc);
	printf("box %-5s at %d,%d ret=%d\n", name, x, y, ret);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	const char *font = argc > 1 ? argv[1] : "DejaVuSans.ttf";
	fb = fbink_open();
	if (fb < 0 || fbink_init(fb, &cfg) < 0) { puts("fbink init failed"); return 1; }
	FBInkState st;
	fbink_get_state(&cfg, &st);
	printf("device=%s platform=%s screen=%ux%u rota=%u mtk=%d swap=%d mirx=%d miry=%d\n", st.device_name,
	       st.device_platform, st.screen_width, st.screen_height, st.current_rota, st.is_mtk, st.touch_swap_axes,
	       st.touch_mirror_x, st.touch_mirror_y);

	/* white + full flash */
	FBInkConfig fc = cfg;
	fc.is_flashing = true;
	fc.wfm_mode = WFM_GC16;
	fbink_cls(fb, &fc, NULL, false);

	printf("add font: %d\n", fbink_add_ot_font(font, FNT_REGULAR));
	FBInkOTConfig ot = { 0 };
	ot.size_px = 48;
	ot.margins.top = 150;
	ot.margins.left = 40;
	FBInkConfig tc = cfg;
	tc.wfm_mode = WFM_GL16;
	printf("print_ot: %d\n", fbink_print_ot(fb, "Merhaba İıĞğŞşÇçÖöÜü ♔♕♖♗♘♙", &ot, &tc, NULL));
	ot.margins.top = 260;
	fbink_print_ot(fb, "1:DU 2:A2 3:GL16 4:AUTO 5:REAGL", &ot, &tc, NULL);
	sleep(2);

	const struct { WFM_MODE_INDEX_T m; const char *n; } modes[] = {
		{ WFM_DU, "DU" }, { WFM_A2, "A2" }, { WFM_GL16, "GL16" }, { WFM_AUTO, "AUTO" }, { WFM_REAGL, "REAGL" },
	};
	for (int i = 0; i < 5; i++) {
		box(60 + i * 200, 400, 160, 160, 0, modes[i].m, modes[i].n);
		sleep(2);
	}
	/* gray boxes with GL16 */
	box(60, 650, 160, 160, 0x88, WFM_GL16, "g-GL16");
	sleep(2);
	box(260, 650, 160, 160, 0x88, WFM_DU, "g-DU");
	sleep(2);

	/* input devices */
	int touch = -1;
	for (int i = 0; i < 16; i++) {
		char p[32], name[128] = "";
		snprintf(p, sizeof p, "/dev/input/event%d", i);
		int fd = open(p, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		ioctl(fd, EVIOCGNAME(sizeof name), name);
		unsigned long absb[(ABS_MAX + BITS_LONG) / BITS_LONG] = { 0 };
		unsigned long keyb[(KEY_MAX + BITS_LONG) / BITS_LONG] = { 0 };
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absb), absb);
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keyb), keyb);
		int mt = TESTBIT(ABS_MT_POSITION_X, absb);
		printf("%s: '%s' mt=%d power=%d\n", p, name, mt, (int) TESTBIT(KEY_POWER, keyb));
		if (mt && touch < 0) {
			struct input_absinfo ax, ay;
			ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax);
			ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
			printf("  X %d..%d  Y %d..%d\n", ax.minimum, ax.maximum, ay.minimum, ay.maximum);
			printf("  GRAB=%d\n", ioctl(fd, EVIOCGRAB, 1));
			touch = fd;
		} else close(fd);
	}
	ot.margins.top = 900;
	fbink_print_ot(fb, "Ekranın 4 köşesine sırayla dokun (15 sn)", &ot, &tc, NULL);
	if (touch >= 0) {
		time_t end = time(NULL) + 15;
		int x = -1, y = -1;
		while (time(NULL) < end) {
			struct pollfd pf = { touch, POLLIN, 0 };
			if (poll(&pf, 1, 500) <= 0) continue;
			struct input_event ev;
			while (read(touch, &ev, sizeof ev) == sizeof ev) {
				if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X) x = ev.value;
				if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y) y = ev.value;
				if (ev.type == EV_KEY && ev.code == BTN_TOUCH) printf("BTN_TOUCH %d at raw %d,%d\n", ev.value, x, y);
				if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) printf("TRACK %d at raw %d,%d\n", ev.value, x, y);
			}
			fflush(stdout);
		}
		ioctl(touch, EVIOCGRAB, 0);
	}
	puts("done");
	fbink_close(fb);
	return 0;
}
