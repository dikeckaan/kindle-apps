#include <stdlib.h>
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

int main(void)
{
	T_RUN(test_get_returns_body);
	T_RUN(test_get_curl_failure);
	T_RUN(test_get_hung_curl_is_killed);
	T_RUN(test_get_too_big);
	T_RUN(test_get_missing_curl);
	T_RUN(test_urlencode);
	return T_DONE();
}
