#include <math.h>
#include "sysinfo/si.h"
#include "t.h"

static void test_uptime(void)
{
	T_EQ_INT(si_parse_uptime("356789.12 1234.56\n"), 356789);
	T_EQ_INT(si_parse_uptime(""), -1);
	T_EQ_INT(si_parse_uptime("abc"), -1);
}

static void test_meminfo(void)
{
	long total, avail;
	const char *m = "MemTotal:         510432 kB\nMemFree:           12000 kB\nMemAvailable:     200000 kB\nBuffers: 1 kB\n";
	T_EQ_INT(si_parse_meminfo(m, &total, &avail), 0);
	T_EQ_INT(total, 510432L * 1024);
	T_EQ_INT(avail, 200000L * 1024);
	/* old kernels have no MemAvailable: free + buffers + cached */
	T_EQ_INT(si_parse_meminfo("MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 50 kB\nCached: 250 kB\n", &total, &avail), 0);
	T_EQ_INT(avail, 400L * 1024);
	T_EQ_INT(si_parse_meminfo("garbage", &total, &avail), -1);
}

static void test_fmt_bytes(void)
{
	char s[32];
	si_fmt_bytes(1288490188LL, s, sizeof s);
	T_EQ_STR(s, "1,2 GB");
	si_fmt_bytes(5242880, s, sizeof s);
	T_EQ_STR(s, "5,0 MB");
	si_fmt_bytes(900, s, sizeof s);
	T_EQ_STR(s, "900 B");
	si_fmt_bytes(-1, s, sizeof s);
	T_EQ_STR(s, "—");
}

static void test_fmt_duration(void)
{
	char s[32];
	si_fmt_duration(3 * 86400 + 4 * 3600 + 59, s, sizeof s);
	T_EQ_STR(s, "3 gün 4 sa");
	si_fmt_duration(2 * 3600 + 5 * 60, s, sizeof s);
	T_EQ_STR(s, "2 sa 5 dk");
	si_fmt_duration(42, s, sizeof s);
	T_EQ_STR(s, "0 dk");
	si_fmt_duration(-1, s, sizeof s);
	T_EQ_STR(s, "—");
}

static void test_temp(void)
{
	T_EQ_INT(si_batt_temp_c(77), 25); /* powerd reports Fahrenheit */
	T_EQ_INT(si_batt_temp_c(32), 0);
	T_EQ_INT(si_batt_temp_c(-1000), -1000); /* unknown stays unknown */
}

static void test_firmware(void)
{
	char s[64];
	si_parse_firmware("Kindle 5.19.2 (4183670082)\n", s, sizeof s);
	T_EQ_STR(s, "5.19.2");
	si_parse_firmware("", s, sizeof s);
	T_EQ_STR(s, "—");
}

int main(void)
{
	T_RUN(test_uptime);
	T_RUN(test_meminfo);
	T_RUN(test_fmt_bytes);
	T_RUN(test_fmt_duration);
	T_RUN(test_temp);
	T_RUN(test_firmware);
	return T_DONE();
}
