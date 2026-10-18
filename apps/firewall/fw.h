/* Firewall state from `iptables -S INPUT` output (pure). */
#ifndef FW_H
#define FW_H
/* 1 when nothing blocks incoming traffic (policy ACCEPT and no DROP/REJECT rule), else 0 */
int fw_is_open_policy(const char *iptables_S_output);
/* Port of a dropbear command line ("dropbear\0-R\0-p\02222\0…", NUL-separated as in /proc/<pid>/cmdline);
 * -1 if it is not dropbear or has no valid -p. */
int fw_dropbear_port(const char *cmdline, int len);
#endif
