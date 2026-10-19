#include "kui.h"
#include "t.h"

static void test_policy_partial_until_threshold(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 25);
	for (int i = 1; i <= 24; i++) T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_PARTIAL);
	T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_FULL);
	T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_PARTIAL);
}

static void test_policy_screen_change_forces_full(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 25);
	for (int i = 0; i < 10; i++) kui_policy_next(&p, 0);
	T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL);
	T_EQ_INT(p.count, 0);
	for (int i = 1; i <= 24; i++) T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_PARTIAL);
	T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_FULL);
}

static void test_policy_never(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 0);
	int full = 0;
	for (int i = 0; i < 1000; i++) full += kui_policy_next(&p, 0) == KUI_UPD_FULL;
	T_EQ_INT(full, 0);
}

int main(void)
{
	T_RUN(test_policy_partial_until_threshold);
	T_RUN(test_policy_screen_change_forces_full);
	T_RUN(test_policy_never);
	return T_DONE();
}
