#include "kui.h"
#include "t.h"

static void test_policy_partial_until_threshold(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 25, 8);
	for (int i = 1; i <= 24; i++) T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_PARTIAL);
	T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_FULL_FLASH);
	T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_PARTIAL);
}

static void test_policy_first_draw_is_quiet(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 25, KUI_FLASH_EVERY_DEFAULT);
	T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_QUIET); /* an app's first screen over the hub: no flash */
}

static void test_policy_screen_change_is_quiet_and_keeps_partial_count(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 25, 8);
	for (int i = 0; i < 10; i++) kui_policy_next(&p, 0);
	T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_QUIET);
	T_EQ_INT(p.count, 10);
	for (int i = 11; i <= 24; i++) T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_PARTIAL);
	T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_FULL_FLASH);
	T_EQ_INT(p.changes, 0);
}

static void test_policy_flash_every_n_screen_changes(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 25, 8);
	for (int round = 0; round < 3; round++) {
		for (int i = 1; i <= 7; i++) T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_QUIET);
		T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_FLASH);
	}
}

static void test_policy_wake_flashes_once(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 25, 8);
	kui_policy_next(&p, 1);
	kui_policy_next(&p, 0);
	kui_policy_request_flash(&p);
	T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_FLASH);
	T_EQ_INT(p.count, 0);
	T_EQ_INT(p.changes, 0);
	T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_QUIET);
	/* a pending flash also upgrades a partial update (the screensaver covered the whole screen) */
	kui_policy_request_flash(&p);
	T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_FULL_FLASH);
	T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_PARTIAL);
}

static void test_policy_explicit_flash(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 25, 8);
	for (int i = 0; i < 5; i++) kui_policy_next(&p, 1);
	T_EQ_INT(kui_policy_next(&p, KUI_FLUSH_FLASH), KUI_UPD_FULL_FLASH);
	for (int i = 1; i <= 7; i++) T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_QUIET); /* the counter restarted */
	T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_FLASH);
}

static void test_policy_never(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 0, 0);
	int flashes = 0;
	for (int i = 0; i < 1000; i++) flashes += kui_policy_next(&p, i % 7 == 0) == KUI_UPD_FULL_FLASH;
	T_EQ_INT(flashes, 0);
	kui_policy_request_flash(&p); /* a wake still flashes */
	T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_FLASH);
}

static void test_policy_set_every_keeps_screen_counter(void)
{
	KuiRefreshPolicy p;
	kui_policy_init(&p, 25, 8);
	for (int i = 0; i < 7; i++) kui_policy_next(&p, 1);
	kui_policy_set_every(&p, 0); /* kui_set_refresh_every(0) right after kui_init */
	T_EQ_INT(p.changes, 7);
	for (int i = 0; i < 100; i++) T_EQ_INT(kui_policy_next(&p, 0), KUI_UPD_PARTIAL);
	T_EQ_INT(kui_policy_next(&p, 1), KUI_UPD_FULL_FLASH);
	kui_policy_init(&p, -3, -1); /* negative settings mean never */
	T_EQ_INT(p.every, 0);
	T_EQ_INT(p.flash_every, 0);
}

int main(void)
{
	T_RUN(test_policy_partial_until_threshold);
	T_RUN(test_policy_first_draw_is_quiet);
	T_RUN(test_policy_screen_change_is_quiet_and_keeps_partial_count);
	T_RUN(test_policy_flash_every_n_screen_changes);
	T_RUN(test_policy_wake_flashes_once);
	T_RUN(test_policy_explicit_flash);
	T_RUN(test_policy_never);
	T_RUN(test_policy_set_every_keeps_screen_counter);
	return T_DONE();
}
