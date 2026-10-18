#include "kui.h"
#include "t.h"

static KuiKey keys[64];
static KuiRect area = { 0, 900, 1072, 548 };

static int find_cp(int n, int cp)
{
	for (int i = 0; i < n; i++)
		if (keys[i].cp == cp) return i;
	return -1;
}

static void test_kb_layout_has_turkish_letters(void)
{
	int n = kui_kb_layout(area, KUI_KB_TR_LETTERS, 1, keys, 64);
	const int need[] = { 0x11E, 0xDC, 0x15E, 0x130, 0xD6, 0xC7, 'I', 'Q', 'W', 'Z' };
	for (unsigned i = 0; i < sizeof need / sizeof need[0]; i++) T_ASSERT(find_cp(n, need[i]) >= 0);
	T_ASSERT(find_cp(n, KUI_KEY_BACK) >= 0);
	T_ASSERT(find_cp(n, KUI_KEY_ENTER) >= 0);
	T_ASSERT(find_cp(n, KUI_KEY_SHIFT) >= 0);
	T_ASSERT(find_cp(n, KUI_KEY_PAGE) >= 0);
	T_ASSERT(find_cp(n, ' ') >= 0);
}

static void test_kb_lowercase_dotless(void)
{
	int n = kui_kb_layout(area, KUI_KB_TR_LETTERS, 0, keys, 64);
	T_ASSERT(find_cp(n, 0x131) >= 0); /* ı */
	T_ASSERT(find_cp(n, 'i') >= 0);   /* from İ */
	T_ASSERT(find_cp(n, 'I') < 0);
	T_EQ_STR(keys[find_cp(n, 0x131)].label, "ı");
}

static void test_kb_symbols_page_has_digits(void)
{
	int n = kui_kb_layout(area, KUI_KB_SYMBOLS, 0, keys, 64);
	for (int d = '0'; d <= '9'; d++) T_ASSERT(find_cp(n, d) >= 0);
	T_ASSERT(find_cp(n, '@') >= 0);
}

static void test_kb_keys_inside_area_no_overlap(void)
{
	for (int page = 0; page < 2; page++) {
		int n = kui_kb_layout(area, (KuiKbPage) page, 1, keys, 64);
		for (int i = 0; i < n; i++) {
			KuiRect r = keys[i].r;
			T_ASSERT(r.w >= 60 && r.h >= 80);
			T_ASSERT(r.x >= area.x && r.y >= area.y && r.x + r.w <= area.x + area.w && r.y + r.h <= area.y + area.h);
			for (int j = i + 1; j < n; j++) {
				KuiRect s = keys[j].r;
				int overlap = r.x < s.x + s.w && s.x < r.x + r.w && r.y < s.y + s.h && s.y < r.y + r.h;
				T_ASSERT(!overlap);
			}
		}
	}
}

static void test_kb_hit(void)
{
	int n = kui_kb_layout(area, KUI_KB_TR_LETTERS, 1, keys, 64);
	int q = find_cp(n, 'Q');
	T_EQ_INT(kui_kb_hit(keys, n, keys[q].r.x + 5, keys[q].r.y + 5), q);
	T_EQ_INT(kui_kb_hit(keys, n, 5, 5), -1);
}

int main(void)
{
	T_RUN(test_kb_layout_has_turkish_letters);
	T_RUN(test_kb_lowercase_dotless);
	T_RUN(test_kb_symbols_page_has_digits);
	T_RUN(test_kb_keys_inside_area_no_overlap);
	T_RUN(test_kb_hit);
	return T_DONE();
}
