#include <string.h>
#include "kui.h"
#include "t.h"

/* a monospace stand-in for the font: every character is half the size wide */
static int fake_width(const char *s, KuiFont f, int px, void *ctx)
{
	(void) ctx;
	int w = (int) strlen(s) * px / 2;
	return f == KUI_FONT_BOLD || f == KUI_FONT_SERIF_BOLD ? w * 11 / 10 : w; /* bold is 10 % wider */
}

static void test_off_changes_nothing(void)
{
	KuiFont f = KUI_FONT_REG;
	int px = 26;
	unsigned char fg = 90;
	kui_a11y_adjust(0, "abc", 1000, 100, &f, &px, &fg, 0, fake_width, NULL);
	T_EQ_INT(f, KUI_FONT_REG);
	T_EQ_INT(px, 26);
	T_EQ_INT(fg, 90);
}

static void test_bold_black_bigger(void)
{
	KuiFont f = KUI_FONT_REG;
	int px = 26;
	unsigned char fg = 90;
	kui_a11y_adjust(1, "abc", 1000, 200, &f, &px, &fg, 0, fake_width, NULL);
	T_EQ_INT(f, KUI_FONT_BOLD);
	T_EQ_INT(fg, 0);            /* gray text becomes black */
	T_EQ_INT(px, 34);           /* 26 × 1.3 */
	f = KUI_FONT_SERIF;
	px = 20;
	kui_a11y_adjust(1, "abc", 1000, 200, &f, &px, &fg, 0, fake_width, NULL);
	T_EQ_INT(f, KUI_FONT_SERIF_BOLD);
	T_EQ_INT(px, 30);           /* at least 30 px */
}

static void test_light_text_keeps_its_colour(void)
{
	KuiFont f = KUI_FONT_BOLD;
	int px = 40;
	unsigned char fg = 255;
	kui_a11y_adjust(1, "abc", 1000, 200, &f, &px, &fg, 1, fake_width, NULL);
	T_EQ_INT(fg, 255);
}

static void test_one_line_text_still_fits(void)
{
	KuiFont f = KUI_FONT_REG;
	int px = 40;
	unsigned char fg = 0;
	/* 10 chars × 40/2 = 200 px fits a 220 px box; bold ×1.3 would be 286: the largest bold size that fits is 40 */
	kui_a11y_adjust(1, "0123456789", 220, 200, &f, &px, &fg, 0, fake_width, NULL);
	T_EQ_INT(px, 40);
	/* a one-line label that fit in regular type but not in bold at the same size: a little smaller, still one line */
	f = KUI_FONT_REG;
	px = 40;
	kui_a11y_adjust(1, "0123456789", 205, 200, &f, &px, &fg, 0, fake_width, NULL);
	T_EQ_INT(px, 37);
	/* text that already wraps (a paragraph) simply grows and wraps onto more lines */
	px = 40;
	f = KUI_FONT_REG;
	kui_a11y_adjust(1, "0123456789", 150, 200, &f, &px, &fg, 0, fake_width, NULL);
	T_EQ_INT(px, 52);
}

static void test_box_height_caps_the_size(void)
{
	KuiFont f = KUI_FONT_REG;
	int px = 40;
	unsigned char fg = 0;
	kui_a11y_adjust(1, "ab", 1000, 50, &f, &px, &fg, 0, fake_width, NULL); /* 52 would overflow a 50 px box */
	T_EQ_INT(px, 45);
}

int main(void)
{
	T_RUN(test_off_changes_nothing);
	T_RUN(test_bold_black_bigger);
	T_RUN(test_light_text_keeps_its_colour);
	T_RUN(test_one_line_text_still_fits);
	T_RUN(test_box_height_caps_the_size);
	return T_DONE();
}
