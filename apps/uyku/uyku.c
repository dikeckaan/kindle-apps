#include <string.h>
#include "uyku.h"

int uyku_interval(int m) { return m == 30 || m == 60 || m == 120 ? m : 60; }

static long floor_div(long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

long uyku_next_wake(long now, int local_off_s, int every_min)
{
	long local = now + local_off_s, step = (long) uyku_interval(every_min) * 60;
	long next = (floor_div(local, step) + 1) * step;
	if (next - local < 60) next += step; /* a wake right after this one would only redraw the same screen */
	long midnight = (floor_div(local, 86400) + 1) * 86400;
	if (next > midnight && midnight - local >= 60) next = midnight;
	return next + 30 - local_off_s; /* half a minute late: the RTC counts whole seconds and may fire a moment early */
}

unsigned long long uyku_hash(unsigned long long h, const char *s)
{
	if (!h) h = 1469598103934665603ULL;
	for (; s && *s; s++) {
		h ^= (unsigned char) *s;
		h *= 1099511628211ULL;
	}
	h ^= 0x1f; /* field separator: ("ab","c") and ("a","bc") differ */
	h *= 1099511628211ULL;
	return h;
}

UykuUpd uyku_update_kind(int first, unsigned long long prev_body, unsigned long long body, int new_day, int partials_since_full)
{
	if (first || new_day) return UYKU_UPD_FULL;
	if (body == prev_body) return UYKU_UPD_FOOTER;
	return partials_since_full + 1 >= UYKU_FULL_EVERY ? UYKU_UPD_FULL : UYKU_UPD_PARTIAL;
}

int uyku_weather_due(long now, long fetched) { return fetched <= 0 || now - fetched >= 3 * 3600L - 10 * 60; }

/* traditional Turkish proverbs (anonymous, public domain) */
static const char *const proverbs[] = {
	"Damlaya damlaya göl olur.",
	"Sakla samanı, gelir zamanı.",
	"Bugünün işini yarına bırakma.",
	"Ağaç yaşken eğilir.",
	"Bilmemek ayıp değil, öğrenmemek ayıp.",
	"Tatlı dil yılanı deliğinden çıkarır.",
	"Komşu komşunun külüne muhtaçtır.",
	"Gülü seven dikenine katlanır.",
	"Ak akçe kara gün içindir.",
	"Ayağını yorganına göre uzat.",
	"Bir elin nesi var, iki elin sesi var.",
	"Emek olmadan yemek olmaz.",
	"Güneş balçıkla sıvanmaz.",
	"İşleyen demir ışıldar.",
	"Az veren candan, çok veren maldan.",
	"Sabır acıdır, meyvesi tatlıdır.",
	"Yavaş yavaş giden yol alır.",
	"Kitap, insanın en iyi dostudur.",
	"Rüzgâr eken fırtına biçer.",
	"Bin bilsen de bir bilene danış.",
	"Dost kara günde belli olur.",
	"Her işte bir hayır vardır.",
	"İyilik eden iyilik bulur.",
	"Su testisi su yolunda kırılır.",
	"Ne ekersen onu biçersin.",
	"Çok yaşayan bilmez, çok gezen bilir.",
	"Söz gümüşse sükût altındır.",
	"Üzüm üzüme baka baka kararır.",
	"Gönül ne kahve ister ne kahvehane; gönül sohbet ister, kahve bahane.",
	"Akıl akıldan üstündür.",
	"Sağlık varlıktan iyidir.",
	"Bir lisan bir insan, iki lisan iki insan.",
	"Mum dibine ışık vermez.",
	"Erken kalkan yol alır.",
	"Yalnız taş duvar olmaz.",
	"Gönül gönülden su içer.",
	"Gün doğmadan neler doğar.",
	"Sabah ola, hayır ola.",
};

int uyku_proverb_count(void) { return (int) (sizeof proverbs / sizeof proverbs[0]); }

const char *uyku_proverb(long day)
{
	long n = uyku_proverb_count(), i = day % n;
	return proverbs[i < 0 ? i + n : i];
}

/* the line starting at p: its length up to '\n' or the end */
static int line_len(const char *p) { return (int) strcspn(p, "\n"); }

int uyku_poem_excerpt(const char *text, int max_lines, char *out, int cap, int *more)
{
	int taken = 0, o = 0;
	const char *p = text ? text : "";
	*more = 0;
	if (cap > 0) out[0] = '\0';
	while (*p && taken < max_lines) {
		int len = line_len(p);
		if (len == 0) { /* a stanza break: take it only when the next stanza fits, or too little is shown yet */
			const char *q = p + 1;
			int next = 0;
			while (*q && line_len(q) > 0) {
				next++;
				q += line_len(q);
				if (*q) q++;
			}
			if (!next) break; /* trailing blank lines */
			if (taken + 1 + next > max_lines && taken * 2 >= max_lines) {
				*more = 1;
				break;
			}
			if (taken + 2 > max_lines) { /* a break as the last line is useless */
				*more = 1;
				break;
			}
		}
		if (o + len + 2 > cap) {
			*more = 1;
			break;
		}
		if (taken) out[o++] = '\n';
		memcpy(out + o, p, (size_t) len);
		o += len;
		out[o] = '\0';
		taken++;
		p += len;
		if (*p) p++;
	}
	if (*p && !*more) { /* lines left (other than blank ones) */
		for (const char *q = p; *q; q++)
			if (*q != '\n') *more = 1;
	}
	return taken;
}
