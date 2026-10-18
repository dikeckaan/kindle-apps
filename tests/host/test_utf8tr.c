#include "kui.h"
#include "t.h"

static void test_tr_upper(void)
{
	T_EQ_INT(kui_tr_upper('i'), 0x130);
	T_EQ_INT(kui_tr_upper(0x131), 'I');
	T_EQ_INT(kui_tr_upper('a'), 'A');
	T_EQ_INT(kui_tr_upper(0xE7), 0xC7);   /* ç → Ç */
	T_EQ_INT(kui_tr_upper(0x11F), 0x11E); /* ğ → Ğ */
	T_EQ_INT(kui_tr_upper(0x15F), 0x15E); /* ş → Ş */
	T_EQ_INT(kui_tr_upper(0xF6), 0xD6);   /* ö → Ö */
	T_EQ_INT(kui_tr_upper(0xFC), 0xDC);   /* ü → Ü */
	T_EQ_INT(kui_tr_upper('5'), '5');
}

static void test_tr_lower(void)
{
	T_EQ_INT(kui_tr_lower('I'), 0x131);
	T_EQ_INT(kui_tr_lower(0x130), 'i');
	T_EQ_INT(kui_tr_lower(0xC7), 0xE7);
	T_EQ_INT(kui_tr_lower('Z'), 'z');
}

static void test_tr_upper_str(void)
{
	char out[64];
	kui_tr_upper_str("çiğdem ışık", out, sizeof out);
	T_EQ_STR(out, "ÇİĞDEM IŞIK");
	kui_tr_upper_str("çiğdem", out, 5); /* truncates on a code-point boundary */
	T_EQ_STR(out, "Çİ");
}

static void test_fold_circumflex(void)
{
	T_EQ_INT(kui_tr_fold_circumflex(0xE2), 'a');
	T_EQ_INT(kui_tr_fold_circumflex(0xEE), 'i');
	T_EQ_INT(kui_tr_fold_circumflex(0xFB), 'u');
	T_EQ_INT(kui_tr_fold_circumflex(0xC2), 'A');
	T_EQ_INT(kui_tr_fold_circumflex(0xCE), 0x130);
	T_EQ_INT(kui_tr_fold_circumflex(0xDB), 'U');
	T_EQ_INT(kui_tr_fold_circumflex('x'), 'x');
}

static void test_utf8_roundtrip(void)
{
	int cp;
	char buf[4];
	T_EQ_INT(kui_utf8_decode("\xC4\xB0", &cp), 2);
	T_EQ_INT(cp, 0x130);
	T_EQ_INT(kui_utf8_decode("\xE2\x99\x94", &cp), 3);
	T_EQ_INT(cp, 0x2654);
	T_EQ_INT(kui_utf8_encode(0x2654, buf), 3);
	T_ASSERT(memcmp(buf, "\xE2\x99\x94", 3) == 0);
	T_EQ_INT(kui_utf8_len("ağaç"), 4);
}

static void test_utf8_invalid(void)
{
	int cp = 0;
	T_EQ_INT(kui_utf8_decode("\xFF", &cp), 1);
	T_EQ_INT(cp, 0xFFFD);
	T_EQ_INT(kui_utf8_decode("\xC4", &cp), 1); /* truncated sequence */
	T_EQ_INT(cp, 0xFFFD);
}

static void test_utf8_rejects_overlong_surrogate_and_out_of_range(void)
{
	int cp = 0;
	T_EQ_INT(kui_utf8_decode("\xC0\xAF", &cp), 1);         /* overlong '/' */
	T_EQ_INT(cp, 0xFFFD);
	T_EQ_INT(kui_utf8_decode("\xE0\x80\xAF", &cp), 1);     /* overlong 3-byte */
	T_EQ_INT(cp, 0xFFFD);
	T_EQ_INT(kui_utf8_decode("\xED\xA0\x80", &cp), 1);     /* UTF-16 surrogate */
	T_EQ_INT(cp, 0xFFFD);
	T_EQ_INT(kui_utf8_decode("\xF4\x90\x80\x80", &cp), 1); /* > 0x10FFFF */
	T_EQ_INT(cp, 0xFFFD);
	T_EQ_INT(kui_utf8_decode("\xF0\x9F\x98\x80", &cp), 4); /* valid 4-byte */
	T_EQ_INT(cp, 0x1F600);
}

int main(void)
{
	T_RUN(test_tr_upper);
	T_RUN(test_tr_lower);
	T_RUN(test_tr_upper_str);
	T_RUN(test_fold_circumflex);
	T_RUN(test_utf8_roundtrip);
	T_RUN(test_utf8_invalid);
	T_RUN(test_utf8_rejects_overlong_surrogate_and_out_of_range);
	return T_DONE();
}
