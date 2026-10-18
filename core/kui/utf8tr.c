#include "kui.h"

int kui_utf8_decode(const char *s, int *cp)
{
	const unsigned char *u = (const unsigned char *) s;
	int n, c;
	if (u[0] < 0x80) { *cp = u[0]; return 1; }
	if ((u[0] & 0xE0) == 0xC0) { n = 2; c = u[0] & 0x1F; }
	else if ((u[0] & 0xF0) == 0xE0) { n = 3; c = u[0] & 0x0F; }
	else if ((u[0] & 0xF8) == 0xF0) { n = 4; c = u[0] & 0x07; }
	else { *cp = 0xFFFD; return 1; }
	for (int i = 1; i < n; i++) {
		if ((u[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
		c = (c << 6) | (u[i] & 0x3F);
	}
	*cp = c;
	return n;
}

int kui_utf8_encode(int cp, char out[4])
{
	if (cp < 0x80) { out[0] = (char) cp; return 1; }
	if (cp < 0x800) {
		out[0] = (char) (0xC0 | (cp >> 6));
		out[1] = (char) (0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char) (0xE0 | (cp >> 12));
		out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char) (0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char) (0xF0 | (cp >> 18));
	out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char) (0x80 | (cp & 0x3F));
	return 4;
}

int kui_utf8_len(const char *s)
{
	int n = 0, cp;
	while (*s) { s += kui_utf8_decode(s, &cp); n++; }
	return n;
}

/* Turkish letters outside ASCII: lower, upper */
static const int tr_pairs[][2] = {
	{ 0xE7, 0xC7 }, { 0x11F, 0x11E }, { 0x131, 'I' }, { 'i', 0x130 }, { 0xF6, 0xD6 }, { 0x15F, 0x15E }, { 0xFC, 0xDC },
	{ 0xE2, 0xC2 }, { 0xEE, 0xCE }, { 0xFB, 0xDB },
};
#define N_PAIRS ((int) (sizeof tr_pairs / sizeof tr_pairs[0]))

int kui_tr_upper(int cp)
{
	for (int i = 0; i < N_PAIRS; i++)
		if (tr_pairs[i][0] == cp) return tr_pairs[i][1];
	if (cp >= 'a' && cp <= 'z') return cp - 32;
	return cp;
}

int kui_tr_lower(int cp)
{
	for (int i = 0; i < N_PAIRS; i++)
		if (tr_pairs[i][1] == cp) return tr_pairs[i][0];
	if (cp >= 'A' && cp <= 'Z') return cp + 32;
	return cp;
}

int kui_tr_fold_circumflex(int cp)
{
	switch (cp) {
	case 0xE2: return 'a';
	case 0xEE: return 'i';
	case 0xFB: return 'u';
	case 0xC2: return 'A';
	case 0xCE: return 0x130;
	case 0xDB: return 'U';
	default: return cp;
	}
}

void kui_tr_upper_str(const char *in, char *out, int outsz)
{
	int o = 0, cp;
	char enc[4];
	while (*in) {
		in += kui_utf8_decode(in, &cp);
		int n = kui_utf8_encode(kui_tr_upper(cp), enc);
		if (o + n >= outsz) break;
		for (int i = 0; i < n; i++) out[o++] = enc[i];
	}
	if (outsz > 0) out[o] = '\0';
}
