#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ajanda/agenda.h"
#include "t.h"

static void test_calendar(void)
{
	T_EQ_INT(agenda_jdn(2000, 1, 1), 2451545);
	T_EQ_INT(agenda_jdn(2026, 10, 19), 2461333);
	int y, m, d;
	agenda_from_jdn(2461333, &y, &m, &d);
	T_EQ_INT(y, 2026);
	T_EQ_INT(m, 10);
	T_EQ_INT(d, 19);
	T_EQ_INT(agenda_days_in_month(2024, 2), 29);
	T_EQ_INT(agenda_days_in_month(2026, 2), 28);
	T_EQ_INT(agenda_days_in_month(1900, 2), 28);
	T_EQ_INT(agenda_days_in_month(2000, 2), 29);
	T_EQ_INT(agenda_days_in_month(2026, 4), 30);
	T_EQ_INT(agenda_days_in_month(2026, 12), 31);
	T_EQ_INT(agenda_weekday(2026, 10, 19), 1); /* Monday */
	T_EQ_INT(agenda_weekday(2026, 10, 18), 0);
	T_EQ_INT(agenda_weekday(1923, 10, 29), 1);
	y = 2026, m = 12, d = 31;
	agenda_add_days(&y, &m, &d, 1);
	T_EQ_INT(y * 10000 + m * 100 + d, 20270101);
	agenda_add_days(&y, &m, &d, -60);
	T_EQ_INT(y * 10000 + m * 100 + d, 20261102);
	y = 2026, m = 11;
	agenda_add_months(&y, &m, 3);
	T_EQ_INT(y * 100 + m, 202702);
	agenda_add_months(&y, &m, -14);
	T_EQ_INT(y * 100 + m, 202512);
	agenda_add_months(&y, &m, -12 * 3 - 11);
	T_EQ_INT(y * 100 + m, 202201);
}

static void test_iso_week(void)
{
	T_EQ_INT(agenda_iso_week(2026, 10, 19), 43);
	T_EQ_INT(agenda_iso_week(2026, 1, 1), 1);   /* Thursday */
	T_EQ_INT(agenda_iso_week(2027, 1, 1), 53);  /* Friday: last week of 2026 */
	T_EQ_INT(agenda_iso_week(2026, 12, 31), 53);
	T_EQ_INT(agenda_iso_week(2027, 1, 4), 1);
	T_EQ_INT(agenda_iso_week(2024, 12, 30), 1); /* Monday of week 1 of 2025 */
	T_EQ_INT(agenda_iso_week(2021, 1, 3), 53);
	T_EQ_INT(agenda_iso_week(2022, 1, 2), 52);
	T_EQ_INT(agenda_iso_week(2008, 12, 29), 1);
}

static int has(int y, int m, int d, const char *name)
{
	AgendaItem it[8];
	int n = agenda_holidays(y, m, d, it, 8);
	for (int i = 0; i < n; i++)
		if (!strcmp(it[i].text, name)) return 1;
	return 0;
}

/* Diyanet İşleri Başkanlığı, "Dini Günler" 2023–2035 (vakithesaplama.diyanet.gov.tr), the dates exactly as listed;
 * kandils are the evening (Gregorian day) Diyanet gives. R / K: the first day of Ramazan / Kurban Bayramı. */
static const struct { int y, m, d; const char *what; } diyanet[] = {
	{ 2023, 1, 26, "Regaib Kandili" }, { 2023, 2, 17, "Miraç Kandili" }, { 2023, 3, 6, "Berat Kandili" }, { 2023, 3, 23, "Ramazan başlangıcı" },
	{ 2023, 4, 17, "Kadir Gecesi" }, { 2023, 4, 21, "R" }, { 2023, 6, 28, "K" }, { 2023, 7, 19, "Hicri yılbaşı" }, { 2023, 9, 26, "Mevlid Kandili" },
	{ 2024, 1, 11, "Regaib Kandili" }, { 2024, 2, 6, "Miraç Kandili" }, { 2024, 2, 24, "Berat Kandili" }, { 2024, 3, 11, "Ramazan başlangıcı" },
	{ 2024, 4, 5, "Kadir Gecesi" }, { 2024, 4, 10, "R" }, { 2024, 6, 16, "K" }, { 2024, 7, 7, "Hicri yılbaşı" }, { 2024, 9, 14, "Mevlid Kandili" },
	{ 2025, 1, 2, "Regaib Kandili" }, { 2025, 1, 26, "Miraç Kandili" }, { 2025, 2, 13, "Berat Kandili" }, { 2025, 3, 1, "Ramazan başlangıcı" },
	{ 2025, 3, 26, "Kadir Gecesi" }, { 2025, 3, 30, "R" }, { 2025, 6, 6, "K" }, { 2025, 6, 26, "Hicri yılbaşı" }, { 2025, 9, 3, "Mevlid Kandili" },
	{ 2025, 12, 25, "Regaib Kandili" },
	{ 2026, 1, 15, "Miraç Kandili" }, { 2026, 2, 2, "Berat Kandili" }, { 2026, 2, 19, "Ramazan başlangıcı" }, { 2026, 3, 16, "Kadir Gecesi" },
	{ 2026, 3, 20, "R" }, { 2026, 5, 27, "K" }, { 2026, 6, 16, "Hicri yılbaşı" }, { 2026, 8, 24, "Mevlid Kandili" }, { 2026, 12, 10, "Regaib Kandili" },
	{ 2027, 1, 4, "Miraç Kandili" }, { 2027, 1, 22, "Berat Kandili" }, { 2027, 2, 8, "Ramazan başlangıcı" }, { 2027, 3, 5, "Kadir Gecesi" },
	{ 2027, 3, 9, "R" }, { 2027, 5, 16, "K" }, { 2027, 6, 6, "Hicri yılbaşı" }, { 2027, 12, 2, "Regaib Kandili" }, { 2027, 12, 24, "Miraç Kandili" },
	{ 2028, 1, 28, "Ramazan başlangıcı" }, { 2028, 2, 22, "Kadir Gecesi" }, { 2028, 2, 26, "R" }, { 2028, 5, 5, "K" }, { 2028, 5, 25, "Hicri yılbaşı" },
	{ 2028, 8, 2, "Mevlid Kandili" }, { 2028, 11, 23, "Regaib Kandili" }, { 2028, 12, 13, "Miraç Kandili" }, { 2028, 12, 30, "Berat Kandili" },
	{ 2029, 1, 16, "Ramazan başlangıcı" }, { 2029, 2, 10, "Kadir Gecesi" }, { 2029, 2, 14, "R" }, { 2029, 4, 24, "K" }, { 2029, 5, 14, "Hicri yılbaşı" },
	{ 2029, 7, 23, "Mevlid Kandili" }, { 2029, 11, 8, "Regaib Kandili" }, { 2029, 12, 2, "Miraç Kandili" }, { 2029, 12, 20, "Berat Kandili" },
	{ 2030, 1, 5, "Ramazan başlangıcı" }, { 2030, 1, 30, "Kadir Gecesi" }, { 2030, 2, 4, "R" }, { 2030, 4, 13, "K" }, { 2030, 5, 3, "Hicri yılbaşı" },
	{ 2030, 7, 12, "Mevlid Kandili" }, { 2030, 10, 31, "Regaib Kandili" }, { 2030, 11, 22, "Miraç Kandili" }, { 2030, 12, 9, "Berat Kandili" },
	{ 2030, 12, 26, "Ramazan başlangıcı" },
	{ 2031, 1, 24, "R" }, { 2031, 4, 2, "K" }, { 2031, 4, 23, "Hicri yılbaşı" }, { 2031, 7, 1, "Mevlid Kandili" }, { 2031, 10, 16, "Regaib Kandili" },
	{ 2031, 11, 11, "Miraç Kandili" }, { 2031, 11, 29, "Berat Kandili" }, { 2031, 12, 15, "Ramazan başlangıcı" },
	{ 2032, 1, 14, "R" }, { 2032, 3, 22, "K" }, { 2032, 4, 11, "Hicri yılbaşı" }, { 2032, 6, 19, "Mevlid Kandili" }, { 2032, 10, 7, "Regaib Kandili" },
	{ 2032, 10, 31, "Miraç Kandili" }, { 2032, 11, 17, "Berat Kandili" }, { 2032, 12, 4, "Ramazan başlangıcı" }, { 2032, 12, 29, "Kadir Gecesi" },
	{ 2033, 1, 2, "R" }, { 2033, 3, 11, "K" }, { 2033, 4, 1, "Hicri yılbaşı" }, { 2033, 6, 9, "Mevlid Kandili" }, { 2033, 9, 29, "Regaib Kandili" },
	{ 2033, 10, 20, "Miraç Kandili" }, { 2033, 11, 6, "Berat Kandili" }, { 2033, 11, 23, "Ramazan başlangıcı" }, { 2033, 12, 18, "Kadir Gecesi" },
	{ 2033, 12, 23, "R" },
	{ 2034, 3, 1, "K" }, { 2034, 3, 21, "Hicri yılbaşı" }, { 2034, 5, 29, "Mevlid Kandili" }, { 2034, 9, 14, "Regaib Kandili" },
	{ 2034, 10, 9, "Miraç Kandili" }, { 2034, 10, 26, "Berat Kandili" }, { 2034, 11, 12, "Ramazan başlangıcı" }, { 2034, 12, 7, "Kadir Gecesi" },
	{ 2034, 12, 12, "R" },
	{ 2035, 2, 18, "K" }, { 2035, 3, 11, "Hicri yılbaşı" }, { 2035, 5, 19, "Mevlid Kandili" }, { 2035, 9, 6, "Regaib Kandili" },
	{ 2035, 9, 28, "Miraç Kandili" }, { 2035, 10, 16, "Berat Kandili" }, { 2035, 11, 1, "Ramazan başlangıcı" }, { 2035, 11, 26, "Kadir Gecesi" },
	{ 2035, 12, 1, "R" },
};
#define N_DIYANET ((int) (sizeof diyanet / sizeof diyanet[0]))

static int check_bayram(int y, int m, int d, const char *eve, const char *const *days, int n)
{
	int ok = 1, yy = y, mm = m, dd = d;
	agenda_add_days(&yy, &mm, &dd, -1);
	if (!has(yy, mm, dd, eve)) {
		fprintf(stderr, "  %04d-%02d-%02d: no %s\n", yy, mm, dd, eve);
		ok = 0;
	}
	for (int i = 0; i < n; i++) {
		yy = y, mm = m, dd = d;
		agenda_add_days(&yy, &mm, &dd, i);
		if (!has(yy, mm, dd, days[i])) {
			fprintf(stderr, "  %04d-%02d-%02d: no %s\n", yy, mm, dd, days[i]);
			ok = 0;
		}
	}
	return ok;
}

static void test_diyanet_dates(void)
{
	static const char *const rb[] = { "Ramazan Bayramı 1. gün", "Ramazan Bayramı 2. gün", "Ramazan Bayramı 3. gün" };
	static const char *const kb[] = { "Kurban Bayramı 1. gün", "Kurban Bayramı 2. gün", "Kurban Bayramı 3. gün", "Kurban Bayramı 4. gün" };
	for (int i = 0; i < N_DIYANET; i++) {
		int y = diyanet[i].y, m = diyanet[i].m, d = diyanet[i].d, ok;
		if (!strcmp(diyanet[i].what, "R")) ok = check_bayram(y, m, d, "Ramazan Bayramı arifesi", rb, 3);
		else if (!strcmp(diyanet[i].what, "K")) ok = check_bayram(y, m, d, "Kurban Bayramı arifesi", kb, 4);
		else ok = has(y, m, d, diyanet[i].what);
		if (!ok) T_FAIL_("Diyanet %04d-%02d-%02d %s", y, m, d, diyanet[i].what);
	}
}

/* every religious day of 2024–2035 is in Diyanet's list (nothing extra, nothing a day off) */
static void test_no_extra_religious_days(void)
{
	static const char *const once[] = { "Regaib Kandili", "Miraç Kandili", "Berat Kandili", "Ramazan başlangıcı", "Kadir Gecesi", "Hicri yılbaşı",
					    "Mevlid Kandili", "Ramazan Bayramı 1. gün", "Kurban Bayramı 1. gün" };
	for (int y = 2024, m = 1, d = 1; y < 2036; agenda_add_days(&y, &m, &d, 1)) {
		for (size_t k = 0; k < sizeof once / sizeof once[0]; k++) {
			if (!has(y, m, d, once[k])) continue;
			int found = 0;
			for (int i = 0; i < N_DIYANET; i++) {
				const char *w = diyanet[i].what;
				const char *want = !strcmp(w, "R") ? "Ramazan Bayramı 1. gün" : !strcmp(w, "K") ? "Kurban Bayramı 1. gün" : w;
				if (diyanet[i].y == y && diyanet[i].m == m && diyanet[i].d == d && !strcmp(want, once[k])) found = 1;
			}
			/* Diyanet's yearly lists skip a few dates that fall in the neighbouring year's list */
			int unlisted = (y == 2027 && m == 8 && d == 13) || (y == 2028 && m == 1 && d == 11) || (y == 2031 && m == 1 && d == 20) ||
				       (y == 2032 && m == 1 && d == 9);
			if (!found && !unlisted) T_FAIL_("extra %s on %04d-%02d-%02d", once[k], y, m, d);
		}
	}
	T_ASSERT(has(2027, 8, 13, "Mevlid Kandili")); /* 11 Rebiülevvel 1449 (R.evvel 1449 begins 3 Aug 2027) */
	T_ASSERT(has(2028, 1, 11, "Berat Kandili"));  /* 14 Şaban 1449 (Şaban begins 29 Dec 2027) */
	T_ASSERT(has(2031, 1, 20, "Kadir Gecesi"));   /* 26 Ramazan 1452 (Ramazan begins 26 Dec 2030) */
	T_ASSERT(has(2032, 1, 9, "Kadir Gecesi"));    /* 26 Ramazan 1453 (Ramazan begins 15 Dec 2031) */
}

static void test_hijri(void)
{
	int hy, hm, hd;
	T_EQ_INT(agenda_hijri(2026, 10, 19, &hy, &hm, &hd), 1);
	T_EQ_INT(hy, 1448);
	T_EQ_INT(hm, 5);
	T_EQ_INT(hd, 8); /* C.evvel 1448 began 12 Oct 2026 */
	agenda_hijri(2024, 4, 10, &hy, &hm, &hd);
	T_EQ_INT(hy * 10000 + hm * 100 + hd, 14451001);
	agenda_hijri(2024, 4, 9, &hy, &hm, &hd);
	T_EQ_INT(hy * 10000 + hm * 100 + hd, 14450930); /* a 30-day Ramazan */
	agenda_hijri(2035, 12, 31, &hy, &hm, &hd);
	T_EQ_INT(hy * 10000 + hm * 100 + hd, 14571101);
	T_EQ_INT(agenda_hijri_month_start(1447, 9), agenda_jdn(2026, 2, 19));
	T_EQ_STR(agenda_hijri_month_name(9), "Ramazan");
	T_EQ_STR(agenda_hijri_month_name(5), "Cemaziyelevvel");
	/* computed outside the table */
	T_EQ_INT(agenda_hijri(2010, 1, 1, &hy, &hm, &hd), 0);
	T_EQ_INT(hy * 100 + hm, 143101);
	T_EQ_INT(agenda_hijri(2040, 6, 1, &hy, &hm, &hd), 0);
	T_ASSERT(hy == 1461 || hy == 1462);
	/* month lengths stay 29 or 30 days across the edges of the table */
	for (int y = 1440, m = 1; y < 1462;) {
		int ny = m == 12 ? y + 1 : y, nm = m == 12 ? 1 : m + 1;
		long len = agenda_hijri_month_start(ny, nm) - agenda_hijri_month_start(y, m);
		if (len != 29 && len != 30) T_FAIL_("%d/%d has %ld days", y, m, len);
		y = ny, m = nm;
	}
	/* every day maps back onto itself */
	for (long j = agenda_jdn(2019, 1, 1); j < agenda_jdn(2041, 1, 1); j++) {
		int y, m, d;
		agenda_from_jdn(j, &y, &m, &d);
		agenda_hijri(y, m, d, &hy, &hm, &hd);
		if (agenda_hijri_month_start(hy, hm) + hd - 1 != j) {
			T_FAIL_("round trip %04d-%02d-%02d", y, m, d);
			break;
		}
	}
}

/* the computed rule against Diyanet's table: nearly always the same day, never more than a day off */
static void test_astro_rule(void)
{
	int same = 0, total = 0, worst = 0;
	for (int y = 1444, m = 7; !(y == 1457 && m == 12);) {
		long d = agenda_hijri_month_start_astro(y, m) - agenda_hijri_month_start(y, m);
		same += d == 0;
		total++;
		if (labs(d) > worst) worst = (int) labs(d);
		if (++m > 12) m = 1, y++;
	}
	T_EQ_INT(total, 161);
	T_ASSERT(same >= 150);
	T_EQ_INT(worst, 1);
	/* before the table (Diyanet's dates of 2020–2022) */
	T_ASSERT(has(2020, 5, 24, "Ramazan Bayramı 1. gün"));
	T_ASSERT(has(2020, 7, 31, "Kurban Bayramı 1. gün"));
	T_ASSERT(has(2021, 5, 13, "Ramazan Bayramı 1. gün"));
	T_ASSERT(has(2021, 7, 20, "Kurban Bayramı 1. gün"));
	T_ASSERT(has(2022, 5, 2, "Ramazan Bayramı 1. gün"));
	T_ASSERT(has(2022, 7, 9, "Kurban Bayramı 1. gün"));
}

static void test_official(void)
{
	AgendaItem it[8];
	T_ASSERT(has(2026, 1, 1, "Yılbaşı"));
	T_ASSERT(has(2026, 4, 23, "Ulusal Egemenlik ve Çocuk Bayramı"));
	T_ASSERT(has(2026, 5, 1, "Emek ve Dayanışma Günü"));
	T_ASSERT(has(2026, 5, 19, "Atatürk'ü Anma, Gençlik ve Spor Bayramı"));
	T_ASSERT(has(2026, 7, 15, "Demokrasi ve Millî Birlik Günü"));
	T_ASSERT(!has(2015, 7, 15, "Demokrasi ve Millî Birlik Günü")); /* since 2017 */
	T_ASSERT(has(2026, 8, 30, "Zafer Bayramı"));
	T_ASSERT(has(2026, 10, 29, "Cumhuriyet Bayramı"));
	T_EQ_INT(agenda_holidays(2026, 10, 28, it, 8), 1);
	T_EQ_STR(it[0].text, "Cumhuriyet Bayramı arifesi");
	T_EQ_INT(it[0].kind, AGENDA_HALF);
	T_EQ_INT(agenda_holidays(2026, 10, 29, it, 8), 1);
	T_EQ_INT(it[0].kind, AGENDA_HOLIDAY);
	T_EQ_INT(it[0].note, -1);
	T_EQ_INT(agenda_holidays(2026, 10, 19, it, 8), 0);
	agenda_holidays(2026, 3, 19, it, 8);
	T_EQ_INT(it[0].kind, AGENDA_HALF);
	agenda_holidays(2026, 3, 21, it, 8);
	T_EQ_INT(it[0].kind, AGENDA_HOLIDAY);
	agenda_holidays(2026, 8, 24, it, 8);
	T_EQ_INT(it[0].kind, AGENDA_KANDIL);
	/* 2033: Ramazan Bayramı falls on New Year's Day's neighbours; both are listed, holidays first */
	int n = agenda_holidays(2033, 1, 1, it, 8);
	T_EQ_INT(n, 2);
	T_EQ_STR(it[0].text, "Yılbaşı");
	T_EQ_STR(it[1].text, "Ramazan Bayramı arifesi");
	T_EQ_INT(agenda_holidays(2033, 1, 1, it, 1), 1); /* max respected */
}

static void test_notable(void)
{
	AgendaItem it[8];
	T_ASSERT(has(2026, 5, 10, "Anneler Günü"));   /* 2nd Sunday of May */
	T_ASSERT(!has(2026, 5, 3, "Anneler Günü"));
	T_ASSERT(has(2027, 5, 9, "Anneler Günü"));
	T_ASSERT(has(2026, 6, 21, "Babalar Günü"));   /* 3rd Sunday of June */
	T_ASSERT(has(2025, 6, 15, "Babalar Günü"));
	T_ASSERT(has(2026, 11, 24, "Öğretmenler Günü"));
	T_ASSERT(has(2026, 11, 10, "Atatürk'ü Anma Günü"));
	agenda_holidays(2026, 11, 10, it, 8);
	T_EQ_INT(it[0].kind, AGENDA_NOTABLE);
}

static void test_notes_file(void)
{
	static AgendaNote n[16];
	const char *file = "2026-10-19\tDiş hekimi 14:30\n"
			   "1990-05-12*\tAnnemin doğum günü\n"
			   "garbage line\n"
			   "2026-13-01\tbad month\n"
			   "2026-02-30\tbad day\n"
			   "\n"
			   "2026-10-20\tsatır\\nikinci \\\\ ters\\tsekme\r\n"
			   "2026-10-21\t\n";
	int c = agenda_notes_parse(file, n, 16);
	T_EQ_INT(c, 3);
	T_EQ_INT(n[0].y * 10000 + n[0].m * 100 + n[0].d, 20261019);
	T_EQ_STR(n[0].text, "Diş hekimi 14:30");
	T_EQ_INT(n[0].yearly, 0);
	T_EQ_INT(n[1].yearly, 1);
	T_EQ_INT(n[1].y, 1990);
	T_EQ_STR(n[2].text, "satır\nikinci \\ ters\tsekme");
	char buf[512];
	int len = agenda_notes_format(n, c, buf, sizeof buf);
	T_EQ_STR(buf, "2026-10-19\tDiş hekimi 14:30\n1990-05-12*\tAnnemin doğum günü\n2026-10-20\tsatır\\nikinci \\\\ ters\\tsekme\n");
	T_EQ_INT(len, (int) strlen(buf));
	T_EQ_INT(agenda_notes_format(n, c, buf, 20), -1);
	/* round trip through a file */
	char path[] = "/tmp/test_agenda_XXXXXX";
	int fd = mkstemp(path);
	T_ASSERT(fd >= 0);
	close(fd);
	T_EQ_INT(agenda_notes_save(path, n, c), 0);
	static AgendaNote back[16];
	T_EQ_INT(agenda_notes_load(path, back, 16), 3);
	T_EQ_STR(back[2].text, n[2].text);
	unlink(path);
	T_EQ_INT(agenda_notes_load("/nonexistent/notes.txt", back, 16), 0);
	T_EQ_INT(agenda_notes_parse(file, n, 2), 2); /* max respected */
}

static void test_notes_ops(void)
{
	static AgendaNote n[4];
	int c = 0;
	T_EQ_INT(agenda_note_add(n, &c, 4, 2026, 10, 19, "bir", 0), 0);
	T_EQ_INT(agenda_note_add(n, &c, 4, 2024, 2, 29, "artık gün", 1), 1);
	T_EQ_INT(agenda_note_add(n, &c, 4, 2026, 10, 19, "iki\tsekme", 0), 2);
	T_EQ_INT(agenda_note_add(n, &c, 4, 2026, 10, 19, "", 0), -1); /* empty text */
	T_EQ_INT(agenda_note_add(n, &c, 4, 2026, 10, 18, "dört", 0), 3);
	T_EQ_INT(agenda_note_add(n, &c, 4, 2026, 10, 18, "beş", 0), -1); /* full */
	T_EQ_INT(c, 4);
	T_ASSERT(agenda_note_on(&n[0], 2026, 10, 19));
	T_ASSERT(!agenda_note_on(&n[0], 2027, 10, 19));
	T_ASSERT(agenda_note_on(&n[1], 2028, 2, 29));
	T_ASSERT(agenda_note_on(&n[1], 2027, 2, 28)); /* not a leap year */
	T_ASSERT(!agenda_note_on(&n[1], 2028, 2, 28));
	T_ASSERT(!agenda_note_on(&n[1], 2023, 2, 28)); /* before the note's year */
	agenda_note_delete(n, &c, 0);
	T_EQ_INT(c, 3);
	T_EQ_STR(n[0].text, "artık gün");
	T_EQ_STR(n[1].text, "iki\tsekme");
	agenda_note_delete(n, &c, 7); /* out of range: nothing */
	T_EQ_INT(c, 3);
	/* long text is cut at a code point boundary */
	char lng[400];
	memset(lng, 0, sizeof lng);
	for (int i = 0; i < 150; i++) strcat(lng, "ş");
	c = 0;
	agenda_note_add(n, &c, 4, 2026, 1, 1, lng, 0);
	T_ASSERT(strlen(n[0].text) < AGENDA_TEXT_MAX);
	T_EQ_INT(strlen(n[0].text) % 2, 0);
}

static void test_items_and_upcoming(void)
{
	static AgendaNote n[8];
	int c = 0;
	agenda_note_add(n, &c, 8, 2026, 10, 29, "Fener alayı", 0);
	agenda_note_add(n, &c, 8, 1990, 10, 21, "Ayşe'nin doğum günü", 1);
	agenda_note_add(n, &c, 8, 2026, 10, 30, "Toplantı", 0);
	agenda_note_add(n, &c, 8, 2026, 10, 19, "Bugün", 0);
	AgendaItem it[16];
	int k = agenda_day_items(n, c, 2026, 10, 29, it, 16);
	T_EQ_INT(k, 2);
	T_EQ_STR(it[0].text, "Cumhuriyet Bayramı");
	T_EQ_INT(it[1].kind, AGENDA_NOTE);
	T_EQ_INT(it[1].note, 0);
	T_EQ_STR(it[1].text, "Fener alayı");
	k = agenda_upcoming_notes(n, c, 2026, 10, 19, 8, it, 16); /* 19 … 26 Oct */
	T_EQ_INT(k, 2);
	T_EQ_INT(it[0].d, 19);
	T_EQ_INT(it[1].d, 21);
	T_EQ_INT(it[1].yearly, 1);
	k = agenda_upcoming_notes(n, c, 2026, 10, 25, 7, it, 16); /* 25 … 31 Oct */
	T_EQ_INT(k, 4);
	T_EQ_INT(it[0].d, 28);
	T_EQ_INT(it[0].kind, AGENDA_HALF);
	T_EQ_INT(it[1].d, 29);
	T_EQ_INT(it[2].d, 29);
	T_EQ_INT(it[3].d, 30);
	T_EQ_INT(agenda_upcoming_notes(n, c, 2026, 10, 25, 7, it, 2), 2);
	/* across the year end */
	k = agenda_upcoming_notes(NULL, 0, 2026, 12, 30, 3, it, 16);
	T_EQ_INT(k, 1);
	T_EQ_INT(it[0].y * 10000 + it[0].m * 100 + it[0].d, 20270101);
	/* from the data dir */
	char dir[] = "/tmp/test_agenda_dXXXXXX";
	T_ASSERT(mkdtemp(dir) != NULL);
	char path[300];
	snprintf(path, sizeof path, "%s/notes.txt", dir);
	T_EQ_INT(agenda_notes_save(path, n, c), 0);
	k = agenda_upcoming(dir, 2026, 10, 29, 1, it, 16);
	T_EQ_INT(k, 2);
	T_EQ_STR(it[1].text, "Fener alayı");
	unlink(path);
	rmdir(dir);
	T_EQ_INT(agenda_upcoming(dir, 2026, 10, 29, 1, it, 16), 1); /* no notes file: holidays only */
}

int main(void)
{
	T_RUN(test_calendar);
	T_RUN(test_iso_week);
	T_RUN(test_hijri);
	T_RUN(test_diyanet_dates);
	T_RUN(test_no_extra_religious_days);
	T_RUN(test_astro_rule);
	T_RUN(test_official);
	T_RUN(test_notable);
	T_RUN(test_notes_file);
	T_RUN(test_notes_ops);
	T_RUN(test_items_and_upcoming);
	return T_DONE();
}
