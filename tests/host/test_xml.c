#include <stdlib.h>
#include "kui.h"
#include "net/net.h"
#include "t.h"

static void test_find_elements_and_attrs(void)
{
	const char *x = "<a><Currency Kod=\"USD\" X='1'><Unit>1</Unit><ForexBuying>49.3573</ForexBuying><CrossRateUSD/></Currency>"
			"<Currency Kod=\"EUR\"><Unit>1</Unit></Currency></a>";
	const char *end = x + strlen(x);
	XmlEl el, in;
	char buf[64];
	const char *p = xml_next(x, end, "Currency", &el);
	T_ASSERT(p != NULL);
	T_EQ_INT(xml_attr(&el, "Kod", buf, sizeof buf), 1);
	T_EQ_STR(buf, "USD");
	T_EQ_INT(xml_attr(&el, "X", buf, sizeof buf), 1);
	T_EQ_STR(buf, "1");
	T_EQ_INT(xml_attr(&el, "Missing", buf, sizeof buf), 0);
	T_ASSERT(xml_next(el.inner, el.inner_end, "ForexBuying", &in) != NULL);
	xml_text(in.inner, in.inner_end, buf, sizeof buf);
	T_EQ_STR(buf, "49.3573");
	/* self-closing element: empty inner */
	T_ASSERT(xml_next(el.inner, el.inner_end, "CrossRateUSD", &in) != NULL);
	T_ASSERT(in.inner == in.inner_end);
	/* <Unit> must not match <UnitX>, and the second Currency follows */
	p = xml_next(p, end, "Currency", &el);
	T_ASSERT(p != NULL);
	xml_attr(&el, "Kod", buf, sizeof buf);
	T_EQ_STR(buf, "EUR");
	T_ASSERT(xml_next(p, end, "Currency", &el) == NULL);
}

static void test_prefix_names_do_not_match(void)
{
	const char *x = "<items><item>a</item></items>";
	XmlEl el;
	char buf[16];
	T_ASSERT(xml_next(x, x + strlen(x), "item", &el) != NULL);
	xml_text(el.inner, el.inner_end, buf, sizeof buf);
	T_EQ_STR(buf, "a");
}

static void test_truncated_document(void)
{
	const char *x = "<item><title>ok</title></item><item><title>cut";
	const char *end = x + strlen(x);
	XmlEl el;
	const char *p = xml_next(x, end, "item", &el);
	T_ASSERT(p != NULL);
	T_ASSERT(xml_next(p, end, "item", &el) == NULL);
	T_ASSERT(xml_next(x, x + 3, "item", &el) == NULL);
}

static void test_text_cdata_and_entities(void)
{
	char buf[128];
	const char *a = "  <![CDATA[A & <b>B</b>]]> ";
	xml_text(a, a + strlen(a), buf, sizeof buf);
	T_EQ_STR(buf, "A & <b>B</b>");
	const char *b = "T&#252;rk &amp; &lt;x&gt; &quot;q&quot; &apos;&#x130;&nbsp;z &bogus;";
	xml_text(b, b + strlen(b), buf, sizeof buf);
	T_EQ_STR(buf, "Türk & <x> \"q\" 'İ\xC2\xA0z &bogus;");
	xml_text(b, b + strlen(b), buf, 6); /* bounded, never splits UTF-8 */
	T_EQ_STR(buf, "Türk");
}

static void test_html_to_text(void)
{
	char buf[256];
	const char *h = "<article><header><img src=\"x.jpg\"/><h2>Başlık &amp; özet</h2></header>"
			"<p>Bir   iki\n üç.</p><p>Dört<br/>beş</p><script>var x=1;</script>"
			"<figure>foto</figure><footer><small>© TRT HABER</small></footer></article>";
	html_to_text(h, h + strlen(h), buf, sizeof buf);
	T_EQ_STR(buf, "Başlık & özet\nBir iki üç.\nDört\nbeş\n© TRT HABER");
	const char *plain = "Sadece metin";
	html_to_text(plain, plain + strlen(plain), buf, sizeof buf);
	T_EQ_STR(buf, "Sadece metin");
}

int main(void)
{
	T_RUN(test_find_elements_and_attrs);
	T_RUN(test_prefix_names_do_not_match);
	T_RUN(test_truncated_document);
	T_RUN(test_text_cdata_and_entities);
	T_RUN(test_html_to_text);
	return T_DONE();
}
