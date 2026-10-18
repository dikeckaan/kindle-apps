#include "firewall/fw.h"
#include "t.h"

/* captured from the Kindle (FW 5.19.2) */
static const char *kindle_default =
	"-P INPUT DROP\n"
	"-A INPUT -i ppp0 -p tcp -m tcp --dport 40317 -j ACCEPT\n"
	"-A INPUT -i wlan0 -p tcp -m state --state RELATED,ESTABLISHED -j ACCEPT\n"
	"-A INPUT -s 127.0.0.1/32 -i lo -j ACCEPT\n"
	"-A INPUT -p tcp -m tcp --dport 22 -m conntrack --ctstate NEW,ESTABLISHED -j ACCEPT\n";

static void test_fw_drop_policy_is_closed_wall(void) { T_EQ_INT(fw_is_open_policy(kindle_default), 0); }

static void test_fw_accept_flushed_is_open_wall(void) { T_EQ_INT(fw_is_open_policy("-P INPUT ACCEPT\n"), 1); }

static void test_fw_accept_with_drop_rule_is_closed(void)
{
	T_EQ_INT(fw_is_open_policy("-P INPUT ACCEPT\n-A INPUT -p tcp --dport 80 -j DROP\n"), 0);
	T_EQ_INT(fw_is_open_policy("-P INPUT ACCEPT\n-A INPUT -j REJECT --reject-with icmp-port-unreachable\n"), 0);
}

static void test_fw_garbage_is_closed(void) { T_EQ_INT(fw_is_open_policy(""), 0); }

int main(void)
{
	T_RUN(test_fw_drop_policy_is_closed_wall);
	T_RUN(test_fw_accept_flushed_is_open_wall);
	T_RUN(test_fw_accept_with_drop_rule_is_closed);
	T_RUN(test_fw_garbage_is_closed);
	return T_DONE();
}
