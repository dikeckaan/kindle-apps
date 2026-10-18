#include <stdlib.h>
#include "kui.h"
#include "t.h"

static void hex(const unsigned char d[32], char out[65])
{
	for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", d[i]);
}

static void test_sha256_vectors(void)
{
	unsigned char d[32];
	char h[65];
	kui_sha256("", 0, d);
	hex(d, h);
	T_EQ_STR(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	kui_sha256("abc", 3, d);
	hex(d, h);
	T_EQ_STR(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	kui_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d);
	hex(d, h);
	T_EQ_STR(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	char *m = malloc(1000000);
	memset(m, 'a', 1000000);
	kui_sha256(m, 1000000, d);
	free(m);
	hex(d, h);
	T_EQ_STR(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

int main(void)
{
	T_RUN(test_sha256_vectors);
	return T_DONE();
}
