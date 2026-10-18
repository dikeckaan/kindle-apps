/* Firewall state from `iptables -S INPUT` output (pure). */
#ifndef FW_H
#define FW_H
/* 1 when nothing blocks incoming traffic (policy ACCEPT and no DROP/REJECT rule), else 0 */
int fw_is_open_policy(const char *iptables_S_output);
#endif
