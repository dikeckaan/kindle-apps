#include <stdlib.h>
#include "t.h"
#include "kui.h"
#include "bulmaca/bulmaca.h"

/* 5×4, numbered 1 ÇAY/ÇIĞ, 2 YALA, 3 İIŞ, 4 AYI, 5 ĞÜL, 6 ÜZ, 7 ZAR */
static const char *GRID =
	"# a comment line\n"
	"P 3 5 4\n"
	"G ÇAY#İ\n"
	"G I#AYI\n"
	"G ĞÜL#Ş\n"
	"G #ZAR#\n"
	"A 0 0 ÇAY\tSıcak içecek\n"
	"D 0 0 ÇIĞ\tKar kütlesi düşmesi\n"
	"D 0 2 YALA\tdeneme\n"
	"D 0 4 İIŞ\tdeneme\n"
	"A 1 2 AYI\tİri bir hayvan\n"
	"A 2 0 ĞÜL\tdeneme\n"
	"D 2 1 ÜZ\tdeneme\n"
	"A 3 1 ZAR\tOyun küpü\n";

static void test_number(void)
{
	BmPuzzle p;
	T_EQ_INT(bm_parse(GRID + strlen("# a comment line\n"), &p), 0);
	T_EQ_INT(p.id, 3);
	T_EQ_INT(p.w, 5);
	T_EQ_INT(p.h, 4);
	/* numbers: (0,0)=1 (0,2)=2 (0,4)=3 (1,2)=4 (2,0)=5 (2,1)=6 (3,1)=7 */
	T_EQ_INT(p.num[0], 1);
	T_EQ_INT(p.num[2], 2);
	T_EQ_INT(p.num[4], 3);
	T_EQ_INT(p.num[1 * 5 + 2], 4);
	T_EQ_INT(p.num[2 * 5 + 0], 5);
	T_EQ_INT(p.num[2 * 5 + 1], 6);
	T_EQ_INT(p.num[3 * 5 + 1], 7);
	T_EQ_INT(p.num[1], 0);
	T_EQ_INT(p.n_across, 4);
	T_EQ_INT(p.n_words, 8);
	/* across first, in number order */
	T_EQ_INT(p.words[0].num, 1);
	T_EQ_INT(p.words[1].num, 4);
	T_EQ_INT(p.words[2].num, 5);
	T_EQ_INT(p.words[3].num, 7);
	T_EQ_INT(p.words[4].dir, BM_DOWN);
	T_EQ_INT(p.words[4].num, 1);
	T_EQ_INT(p.words[5].num, 2);
	T_EQ_INT(p.words[6].num, 3);
	T_EQ_INT(p.words[7].num, 6);
	T_EQ_STR(p.words[0].clue, "Sıcak içecek");
	T_EQ_STR(p.words[3].clue, "Oyun küpü");
	char a[64];
	bm_word_answer(&p, 5, a, sizeof a);
	T_EQ_STR(a, "YALA");
	bm_word_answer(&p, 6, a, sizeof a);
	T_EQ_STR(a, "İIŞ");
	T_EQ_INT(p.sol[4], 0x130); /* İ */
	T_EQ_INT(p.sol[9], 'I');
	T_EQ_INT(p.word_at[BM_ACROSS][1 * 5 + 3], 1);
	T_EQ_INT(p.word_at[BM_DOWN][1 * 5 + 3], -1);
	T_EQ_INT(p.word_at[BM_DOWN][3 * 5 + 2], 5);
	T_EQ_INT(bm_word_cell(&p, 5, 3), 3 * 5 + 2);
}

static void test_parse_bad(void)
{
	BmPuzzle p;
	char buf[1024];
	/* a clue missing */
	snprintf(buf, sizeof buf, "%s", GRID + strlen("# a comment line\n"));
	*strstr(buf, "A 3 1 ZAR") = '\0';
	T_EQ_INT(bm_parse(buf, &p), -1);
	/* an answer that does not match the grid */
	snprintf(buf, sizeof buf, "%s", GRID + strlen("# a comment line\n"));
	memcpy(strstr(buf, "ZAR\t"), "ZOR", 3);
	T_EQ_INT(bm_parse(buf, &p), -1);
	/* a clue at a cell where no word starts */
	T_EQ_INT(bm_parse("P 1 3 3\nG ABC\nG ###\nG ###\nA 0 1 BC\tx\n", &p), -1);
	/* not a Turkish letter */
	T_EQ_INT(bm_parse("P 1 3 3\nG AQC\nG ###\nG ###\nA 0 0 AQC\tx\n", &p), -1);
	/* short row, too few rows, huge size */
	T_EQ_INT(bm_parse("P 1 3 3\nG AB\nG ###\nG ###\n", &p), -1);
	T_EQ_INT(bm_parse("P 1 3 3\nG ABC\nG ###\nA 0 0 ABC\tx\n", &p), -1);
	T_EQ_INT(bm_parse("P 1 30 3\n", &p), -1);
	T_EQ_INT(bm_parse("garbage", &p), -1);
	/* lower-case answers fold like the grid: i → İ, ı → I */
	T_EQ_INT(bm_parse("P 1 3 3\nG İKI\nG ###\nG ###\nA 0 0 ikı\tsayı\n", &p), 0);
	T_EQ_INT(bm_parse("P 1 3 3\nG İKI\nG ###\nG ###\nA 0 0 IKI\tsayı\n", &p), -1);
}

static void test_fold(void)
{
	T_EQ_INT(bm_fold('i'), 0x130);
	T_EQ_INT(bm_fold(0x131), 'I');
	T_EQ_INT(bm_fold('I'), 'I');
	T_EQ_INT(bm_fold(0x130), 0x130);
	T_EQ_INT(bm_fold(0xE7), 0xC7);   /* ç */
	T_EQ_INT(bm_fold(0x11F), 0x11E); /* ğ */
	T_EQ_INT(bm_fold(0xF6), 0xD6);   /* ö */
	T_EQ_INT(bm_fold(0x15F), 0x15E); /* ş */
	T_EQ_INT(bm_fold(0xFC), 0xDC);   /* ü */
	T_EQ_INT(bm_fold(0xE2), 'A');    /* â */
	T_EQ_INT(bm_fold(0xEE), 0x130);  /* î */
	T_EQ_INT(bm_fold(0xDB), 'U');    /* Û */
	T_EQ_INT(bm_fold('z'), 'Z');
	T_ASSERT(bm_is_letter(0x11F));
	T_ASSERT(bm_is_letter('a'));
	T_ASSERT(!bm_is_letter('q'));
	T_ASSERT(!bm_is_letter('W'));
	T_ASSERT(!bm_is_letter('x'));
	T_ASSERT(!bm_is_letter('1'));
	T_ASSERT(!bm_is_letter(0xE4)); /* ä */
}

static void test_select_and_cursor(void)
{
	BmPuzzle p;
	BmState s;
	T_EQ_INT(bm_parse(GRID + strlen("# a comment line\n"), &p), 0);
	bm_state_init(&s, &p);
	int dir = BM_ACROSS;
	/* (0,0) has both: across first, a second tap switches to down and back */
	T_EQ_INT(bm_select(&p, 0, dir, 0, &dir), 0);
	T_EQ_INT(dir, BM_ACROSS);
	T_EQ_INT(bm_select(&p, 0, dir, 1, &dir), 4);
	T_EQ_INT(dir, BM_DOWN);
	T_EQ_INT(bm_select(&p, 0, dir, 1, &dir), 0);
	T_EQ_INT(dir, BM_ACROSS);
	/* (1,3) only across: down is asked for but across is given */
	dir = BM_DOWN;
	T_EQ_INT(bm_select(&p, 1 * 5 + 3, dir, 0, &dir), 1);
	T_EQ_INT(dir, BM_ACROSS);
	T_EQ_INT(bm_select(&p, 1 * 5 + 3, dir, 1, &dir), 1); /* nothing to switch to */
	T_EQ_INT(dir, BM_ACROSS);
	T_EQ_INT(bm_select(&p, 3, dir, 0, &dir), -1); /* black */
	T_EQ_INT(bm_select(&p, 99, dir, 0, &dir), -1);
	/* cursor: next empty cell of the word, skipping filled ones */
	T_EQ_INT(bm_next_cell(&p, &s, 5, 2), 7);
	T_ASSERT(bm_type(&p, &s, 12, 'l'));
	T_EQ_INT(s.fill[12], 'L');
	T_EQ_INT(bm_next_cell(&p, &s, 5, 7), 17);
	T_EQ_INT(bm_next_cell(&p, &s, 5, 17), 17); /* last cell stays */
	T_EQ_INT(bm_prev_cell(&p, 5, 7), 2);
	T_EQ_INT(bm_prev_cell(&p, 5, 2), 2);
	T_EQ_INT(bm_first_empty(&p, &s, 5), 2);
	T_EQ_INT(bm_word_pos(&p, 5, 17), 3);
	T_EQ_INT(bm_word_pos(&p, 5, 0), -1);
	T_EQ_INT(bm_step_word(&p, 0, -1), 7);
	T_EQ_INT(bm_step_word(&p, 7, 1), 0);
	T_EQ_INT(bm_step_word(&p, 3, 1), 4);
}

static void test_play(void)
{
	BmPuzzle p;
	BmState s;
	T_EQ_INT(bm_parse(GRID + strlen("# a comment line\n"), &p), 0);
	bm_state_init(&s, &p);
	T_ASSERT(!bm_type(&p, &s, 3, 'a'));   /* black */
	T_ASSERT(!bm_type(&p, &s, 0, 'q'));   /* not Turkish */
	T_ASSERT(bm_type(&p, &s, 0, 0xE7));   /* ç */
	T_ASSERT(!bm_type(&p, &s, 0, 0xC7));  /* the same letter again: no change */
	T_ASSERT(bm_type(&p, &s, 1, 'a'));
	T_ASSERT(bm_type(&p, &s, 2, 'i'));    /* wrong: Y */
	int empty = -1;
	T_EQ_INT(bm_check_word(&p, &s, 0, &empty), 1);
	T_EQ_INT(empty, 0);
	T_ASSERT(s.flags[2] & BM_WRONG);
	T_ASSERT(!(s.flags[1] & BM_WRONG));
	T_ASSERT(bm_type(&p, &s, 2, 'y'));    /* fixing it clears the mark */
	T_ASSERT(!(s.flags[2] & BM_WRONG));
	T_EQ_INT(bm_check_word(&p, &s, 0, &empty), 0);
	T_ASSERT(bm_word_full(&p, &s, 0));
	T_ASSERT(!bm_word_full(&p, &s, 4));
	/* reveal locks the cell and counts as help */
	T_ASSERT(!s.helped);
	T_ASSERT(!bm_reveal(&p, &s, 0)); /* already right */
	T_ASSERT(bm_reveal(&p, &s, 4));
	T_EQ_INT(s.fill[4], 0x130);
	T_ASSERT(s.helped);
	T_ASSERT(!bm_type(&p, &s, 4, 'a'));
	T_ASSERT(!bm_erase(&p, &s, 4));
	T_ASSERT(bm_erase(&p, &s, 2));
	T_ASSERT(!bm_erase(&p, &s, 2));
	T_ASSERT(!bm_solved(&p, &s));
	T_EQ_INT(bm_check_all(&p, &s, &empty), 0);
	T_EQ_INT(empty, 15 - 3);
	/* fill everything right */
	for (int c = 0; c < 20; c++)
		if (p.sol[c] && !(s.flags[c] & BM_REVEALED)) bm_type(&p, &s, c, p.sol[c]);
	T_ASSERT(bm_solved(&p, &s));
	T_EQ_INT(bm_filled_pct(&p, &s), 100);
	bm_type(&p, &s, 19 - 1, 'K'); /* (3,3) R → K */
	T_ASSERT(!bm_solved(&p, &s));
	T_EQ_INT(bm_check_all(&p, &s, &empty), 1);
	T_EQ_INT(empty, 0);
}

static void test_save(void)
{
	BmPuzzle p;
	BmState s, t;
	T_EQ_INT(bm_parse(GRID + strlen("# a comment line\n"), &p), 0);
	bm_state_init(&s, &p);
	bm_type(&p, &s, 0, 0xE7);
	bm_type(&p, &s, 9, 0x131);
	bm_reveal(&p, &s, 12);
	s.seconds = 125;
	char buf[BM_SAVE_CAP];
	int n = bm_save_format(&p, &s, buf, sizeof buf);
	T_ASSERT(n > 0);
	T_ASSERT(strstr(buf, "fill Ç--#--#--I--L#-#---#\n") != NULL);
	T_EQ_INT(bm_save_parse(buf, &p, &t), 0);
	T_EQ_INT(t.fill[0], 0xC7);
	T_EQ_INT(t.fill[9], 'I');
	T_EQ_INT(t.fill[12], 'L');
	T_ASSERT(t.flags[12] & BM_REVEALED);
	T_ASSERT(t.helped);
	T_EQ_INT(t.seconds, 125);
	T_EQ_INT(t.done, 0);
	T_ASSERT(!memcmp(t.fill, s.fill, sizeof s.fill));
	/* another puzzle's save, garbage, a letter on a black square */
	p.id = 4;
	T_EQ_INT(bm_save_parse(buf, &p, &t), -1);
	p.id = 3;
	T_EQ_INT(bm_save_parse("nonsense", &p, &t), -1);
	p.sol[18] = 'K'; /* the same id, another grid (a newer puzzle file) */
	T_EQ_INT(bm_save_parse(buf, &p, &t), -1);
	p.sol[18] = 'R';
	T_EQ_INT(bm_save_parse(buf, &p, &t), 0);
	char bad[BM_SAVE_CAP];
	snprintf(bad, sizeof bad, "%s", buf);
	char *f = strstr(bad, "fill ") + 5;
	f[4] = 'A'; /* cell 3 is black — after Ç (2 bytes) it is f[4] */
	T_EQ_INT(bm_save_parse(bad, &p, &t), -1);
	T_EQ_INT(bm_save_format(&p, &s, buf, 20), -1);
}

static void test_days(void)
{
	T_EQ_INT(bm_days(1970, 1, 1), 0);
	T_EQ_INT(bm_days(2026, 10, 19), 20745);
	int y, m, d;
	bm_civil(20745, &y, &m, &d);
	T_EQ_INT(y, 2026);
	T_EQ_INT(m, 10);
	T_EQ_INT(d, 19);
	bm_civil(bm_days(2028, 2, 29), &y, &m, &d);
	T_EQ_INT(m * 100 + d, 229);
	long jan1 = bm_days(2026, 1, 1);
	T_EQ_INT(bm_daily(jan1, 400), 0);
	T_EQ_INT(bm_daily(jan1 + 291, 400), 291);
	T_EQ_INT(bm_daily(jan1 + 400, 400), 0);
	T_EQ_INT(bm_daily(jan1 - 1, 400), 399);
	T_EQ_INT(bm_daily(jan1, 0), -1);
}

static void test_stats(void)
{
	BmStats st = { 0 }, u;
	long d = bm_days(2026, 10, 19);
	bm_stats_record(&st, d, 300, 1);
	T_EQ_INT(st.streak, 1);
	bm_stats_record(&st, d + 1, 200, 0);
	T_EQ_INT(st.streak, 2);
	bm_stats_record(&st, d + 1, 100, 1); /* the same day again: streak unchanged */
	T_EQ_INT(st.streak, 2);
	bm_stats_record(&st, -1, 50, 1);     /* archive */
	T_EQ_INT(st.streak, 2);
	bm_stats_record(&st, d + 5, 400, 1); /* missed days */
	T_EQ_INT(st.streak, 1);
	T_EQ_INT(st.best, 2);
	T_EQ_INT(st.solved, 5);
	T_EQ_INT(st.clean, 4);
	T_EQ_INT(st.best_sec, 50);
	T_EQ_INT(st.total_sec, 1050);
	char buf[256];
	T_ASSERT(bm_stats_format(&st, buf, sizeof buf) > 0);
	bm_stats_parse(buf, &u);
	T_ASSERT(!memcmp(&st, &u, sizeof st));
	bm_stats_parse("solved=-4\njunk\nbest=3", &u);
	T_EQ_INT(u.solved, 0);
	T_EQ_INT(u.best, 3);
	char t[16];
	bm_fmt_time(245, t, sizeof t);
	T_EQ_STR(t, "4:05");
	bm_fmt_time(3729, t, sizeof t);
	T_EQ_STR(t, "1:02:09");
}

/* every shipped puzzle parses, has clued words only, connected letters and short, answer-free clues */
static void test_shipped(void)
{
	BmDb db;
	int n = bm_db_load("apps/bulmaca/data/puzzles.txt", &db);
	T_ASSERT(n >= 300);
	static BmPuzzle p;
	for (int i = 0; i < n; i++) {
		if (bm_db_get(&db, i, &p) != 0) {
			T_FAIL_("puzzle %d does not parse", i + 1);
			continue;
		}
		T_EQ_INT(p.id, i + 1);
		T_ASSERT(p.w == 11 && p.h == 11);
		T_ASSERT(p.n_words >= 10);
		/* letters connected (flood fill from the first) */
		int seen[BM_CELLS] = { 0 }, stack[BM_CELLS], sp = 0, letters = 0, reached = 0;
		for (int c = 0; c < p.w * p.h; c++) letters += p.sol[c] != 0;
		for (int c = 0; c < p.w * p.h && !sp; c++)
			if (p.sol[c]) { stack[sp++] = c; seen[c] = 1; }
		while (sp) {
			int c = stack[--sp], r = c / p.w, k = c % p.w;
			reached++;
			int nb[4] = { r > 0 ? c - p.w : -1, r < p.h - 1 ? c + p.w : -1, k > 0 ? c - 1 : -1, k < p.w - 1 ? c + 1 : -1 };
			for (int j = 0; j < 4; j++)
				if (nb[j] >= 0 && p.sol[nb[j]] && !seen[nb[j]]) { seen[nb[j]] = 1; stack[sp++] = nb[j]; }
		}
		T_EQ_INT(reached, letters);
		for (int w = 0; w < p.n_words; w++) {
			T_ASSERT(p.words[w].len >= 3);
			T_ASSERT(kui_utf8_len(p.words[w].clue) <= 40);
			char a[64], up[BM_CLUE * 2];
			bm_word_answer(&p, w, a, sizeof a);
			kui_tr_upper_str(p.words[w].clue, up, sizeof up);
			for (char *tok = strtok(up, " ,.;:!?()'\"-"); tok; tok = strtok(NULL, " ,.;:!?()'\"-"))
				if (!strcmp(tok, a)) T_FAIL_("puzzle %d: clue \"%s\" contains %s", i + 1, p.words[w].clue, a);
		}
	}
	bm_db_free(&db);
	T_EQ_INT(bm_db_load("no/such/file", &db), -1);
}

int main(void)
{
	T_RUN(test_number);
	T_RUN(test_parse_bad);
	T_RUN(test_fold);
	T_RUN(test_select_and_cursor);
	T_RUN(test_play);
	T_RUN(test_save);
	T_RUN(test_days);
	T_RUN(test_stats);
	T_RUN(test_shipped);
	return T_DONE();
}
