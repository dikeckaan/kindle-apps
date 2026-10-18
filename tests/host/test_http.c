#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "net/net.h"
#include "t.h"

#define ARGS "build/host/fake_curl_args"

static void use_fake(const char *mode)
{
	setenv("NET_CURL", "tests/host/fake_curl.sh", 1);
	setenv("FAKE_CURL_ARGS", ARGS, 1);
	setenv("FAKE_CURL_MODE", mode, 1);
}

static void test_get_returns_body(void)
{
	use_fake("ok");
	char *body = NULL;
	int len = 0;
	T_EQ_INT(net_get("https://example.com/a b?x=1;rm -rf /", &body, &len, 20), 0);
	T_ASSERT(body != NULL);
	T_EQ_INT(len, 10);
	T_EQ_STR(body, "hello body");
	free(body);
	/* the URL is passed as one argument (no shell), with curl's own time limit */
	char args[2048] = "";
	FILE *f = fopen(ARGS, "r");
	T_ASSERT(f != NULL);
	size_t n = f ? fread(args, 1, sizeof args - 1, f) : 0;
	if (f) fclose(f);
	args[n] = '\0';
	T_ASSERT(strstr(args, "\nhttps://example.com/a b?x=1;rm -rf /\n") != NULL);
	T_ASSERT(strstr(args, "--max-time\n20\n") != NULL);
	T_ASSERT(strstr(args, "-f") != NULL);
}

static void test_get_curl_failure(void)
{
	use_fake("fail");
	char *body = (char *) 1;
	int len = 7;
	T_EQ_INT(net_get("https://example.com/missing", &body, &len, 20), -1);
	T_ASSERT(body == NULL);
}

static void test_get_hung_curl_is_killed(void)
{
	use_fake("hang");
	char *body = NULL;
	int len = 0;
	time_t t0 = time(NULL);
	T_EQ_INT(net_get("https://example.com/slow", &body, &len, 1), -1);
	T_ASSERT(time(NULL) - t0 <= 4);
	T_ASSERT(body == NULL);
}

static void test_get_too_big(void)
{
	use_fake("big");
	char *body = NULL;
	int len = 0;
	T_EQ_INT(net_get("https://example.com/big", &body, &len, 20), -1);
	T_ASSERT(body == NULL);
}

static void test_get_missing_curl(void)
{
	setenv("NET_CURL", "/nonexistent/curl", 1);
	char *body = NULL;
	int len = 0;
	T_EQ_INT(net_get("https://example.com/", &body, &len, 5), -1);
}

static void test_urlencode(void)
{
	char out[128];
	net_urlencode("Maltepe İstanbul", out, sizeof out);
	T_EQ_STR(out, "Maltepe%20%C4%B0stanbul");
	net_urlencode("a-b_c.d~e&f=g", out, sizeof out);
	T_EQ_STR(out, "a-b_c.d~e%26f%3Dg");
	net_urlencode("şşşşşşşş", out, 10); /* never splits an escape, always terminated */
	T_EQ_STR(out, "%C5%9F");
}

#define DL "build/host/dl_book.azw3"
#define DL_PART "build/host/dl_book.azw3.part"

static int exists(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0;
}

static void read_args(char *args, int cap)
{
	FILE *f = fopen(ARGS, "r");
	size_t n = f ? fread(args, 1, (size_t) cap - 1, f) : 0;
	if (f) fclose(f);
	args[n] = '\0';
}

static void test_download_renames_on_success(void)
{
	use_fake("ok");
	unlink(DL);
	unlink(DL_PART);
	T_EQ_INT(net_download("https://www.gutenberg.org/ebooks/46.kf8.images", DL, 1000, 30), 0);
	T_ASSERT(!exists(DL_PART));
	char buf[64] = "";
	FILE *f = fopen(DL, "r");
	size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = '\0';
	T_EQ_STR(buf, "hello body");
	char args[2048];
	read_args(args, sizeof args);
	T_ASSERT(strstr(args, "\n-o\n" DL_PART "\n") != NULL);
	T_ASSERT(strstr(args, "--max-filesize\n1000\n") != NULL);
	T_ASSERT(strstr(args, "--max-time\n30\n") != NULL);
	T_ASSERT(strstr(args, "\nhttps://www.gutenberg.org/ebooks/46.kf8.images\n") != NULL);
	unlink(DL);
}

static void test_download_failure_leaves_nothing(void)
{
	use_fake("fail");
	unlink(DL);
	T_EQ_INT(net_download("https://example.com/missing", DL, 1000, 30), -1);
	T_ASSERT(!exists(DL_PART));
	T_ASSERT(!exists(DL));
	use_fake("empty"); /* an empty body is not a book */
	T_EQ_INT(net_download("https://example.com/empty", DL, 1000, 30), -1);
	T_ASSERT(!exists(DL_PART));
	T_ASSERT(!exists(DL));
	use_fake("ok"); /* larger than allowed even if curl let it through */
	T_EQ_INT(net_download("https://example.com/a", DL, 5, 30), -1);
	T_ASSERT(!exists(DL_PART));
	T_ASSERT(!exists(DL));
	setenv("NET_CURL", "/nonexistent/curl", 1);
	T_EQ_INT(net_download("https://example.com/a", DL, 1000, 30), -1);
	T_ASSERT(!exists(DL));
}

static void test_download_timeout(void)
{
	use_fake("hang");
	unlink(DL);
	time_t t0 = time(NULL);
	T_EQ_INT(net_download("https://example.com/slow", DL, 1000, 1), -1);
	T_ASSERT(time(NULL) - t0 <= 4);
	T_ASSERT(!exists(DL_PART));
	T_ASSERT(!exists(DL));
}

int main(void)
{
	T_RUN(test_get_returns_body);
	T_RUN(test_get_curl_failure);
	T_RUN(test_get_hung_curl_is_killed);
	T_RUN(test_get_too_big);
	T_RUN(test_get_missing_curl);
	T_RUN(test_urlencode);
	T_RUN(test_download_renames_on_success);
	T_RUN(test_download_failure_leaves_nothing);
	T_RUN(test_download_timeout);
	return T_DONE();
}
