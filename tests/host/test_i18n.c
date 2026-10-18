#include <stdlib.h>
#include <sys/stat.h>
#include "kui.h"
#include "t.h"

#define ROOT "build/host/i18nroot"

static void write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	fputs(text, f);
	fclose(f);
}

static void setup(void)
{
	system("rm -rf " ROOT " && mkdir -p " ROOT "/lang");
	setenv("KUI_ROOT", ROOT, 1);
	write_file(ROOT "/lang/de.txt", "# German\nYeni oyun\tNeues Spiel\nÇıkış\tBeenden\nSatır\\nikinci\tZeile\\nzwei\n"
					"%d puan\t%d Punkte\nbozuk satır sekmesiz\n\tboş kaynak\nYalnız Almanca\tNur Deutsch\n");
	write_file(ROOT "/lang/en.txt", "Yeni oyun\tNew game\nYalnız İngilizce\tEnglish only\nÇıkış\tExit\n");
}

static void test_turkish_is_identity(void)
{
	setup();
	kui_i18n_set("tr");
	T_EQ_STR(kui_lang(), "tr");
	T_EQ_STR(T("Yeni oyun"), "Yeni oyun");
}

static void test_lookup_with_escapes_and_fallback(void)
{
	setup();
	kui_i18n_set("de");
	T_EQ_STR(kui_lang(), "de");
	T_EQ_STR(T("Yeni oyun"), "Neues Spiel");
	T_EQ_STR(T("Satır\nikinci"), "Zeile\nzwei");
	T_EQ_STR(T("%d puan"), "%d Punkte");
	T_EQ_STR(T("Yalnız İngilizce"), "English only"); /* missing in de: English */
	T_EQ_STR(T("Hiçbir yerde yok"), "Hiçbir yerde yok"); /* nowhere: the Turkish source */
	T_EQ_STR(T("bozuk satır sekmesiz"), "bozuk satır sekmesiz"); /* malformed lines are ignored */
}

static void test_missing_catalog_falls_back(void)
{
	setup();
	kui_i18n_set("fi"); /* no fi.txt: English, then Turkish */
	T_EQ_STR(T("Çıkış"), "Exit");
	T_EQ_STR(T("Yalnız Almanca"), "Yalnız Almanca");
	kui_i18n_set("../../etc/passwd"); /* not a language code */
	T_EQ_STR(kui_lang(), "tr");
	T_EQ_STR(T("Çıkış"), "Çıkış");
}

static void test_saved_language(void)
{
	setup();
	mkdir(ROOT "/data", 0755);
	write_file(ROOT "/data/lang", "de\n");
	kui_i18n_load_saved();
	T_EQ_STR(kui_lang(), "de");
	T_EQ_INT(kui_i18n_save("en"), 0);
	kui_i18n_load_saved();
	T_EQ_STR(T("Yeni oyun"), "New game");
}

static void test_numbers_follow_language(void)
{
	setup();
	char s[32];
	kui_i18n_set("en");
	kui_fmt_num(6632.39, 2, s, sizeof s);
	T_EQ_STR(s, "6,632.39");
	kui_i18n_set("de");
	kui_fmt_num(6632.39, 2, s, sizeof s);
	T_EQ_STR(s, "6.632,39");
	kui_i18n_set("fr");
	kui_fmt_num(6632.39, 2, s, sizeof s);
	T_EQ_STR(s, "6 632,39");
	kui_i18n_set("tr");
	kui_fmt_num(6632.39, 2, s, sizeof s);
	T_EQ_STR(s, "6.632,39");
}

static void test_format_mismatch_is_rejected(void)
{
	/* catalogs live on user-writable storage: a translation must never change the printf conversions */
	setup();
	write_file(ROOT "/lang/de.txt", "%d puan\t%s%s%n Punkte\n%s ve %d\t%d und %s\nYüzde %%%d\t%d %% Prozent\n%d oyun\t%d Spiele\n"
					"Ad: %s\tName: %.20s %ls\n");
	kui_i18n_set("de");
	T_EQ_STR(T("%d puan"), "%d puan");      /* extra conversions and %n: refused */
	T_EQ_STR(T("%s ve %d"), "%s ve %d");    /* reordered: refused */
	T_EQ_STR(T("Yüzde %%%d"), "%d %% Prozent"); /* %% may move */
	T_EQ_STR(T("%d oyun"), "%d Spiele");
	T_EQ_STR(T("Ad: %s"), "Ad: %s");        /* different conversion */
}

static void test_language_list(void)
{
	int n;
	const KuiLang *l = kui_languages(&n);
	T_EQ_INT(n, 20);
	T_EQ_STR(l[0].code, "tr");
	T_EQ_STR(l[0].name, "Türkçe");
	int found = 0;
	for (int i = 0; i < n; i++) found += !strcmp(l[i].code, "el") && !strcmp(l[i].name, "Ελληνικά");
	T_EQ_INT(found, 1);
}

int main(void)
{
	T_RUN(test_turkish_is_identity);
	T_RUN(test_lookup_with_escapes_and_fallback);
	T_RUN(test_missing_catalog_falls_back);
	T_RUN(test_saved_language);
	T_RUN(test_numbers_follow_language);
	T_RUN(test_format_mismatch_is_rejected);
	T_RUN(test_language_list);
	return T_DONE();
}
