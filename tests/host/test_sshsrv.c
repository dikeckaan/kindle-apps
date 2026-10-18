#include "ssh/sshsrv.h"
#include "t.h"

static const char *tcp =
	"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
	"   0: 00000000:08AE 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 4207 1 cd6c\n"
	"   1: 5A02A8C0:08AE 5502A8C0:F00D 01 00000000:00000000 02:000A68B4 00000000     0        0 4310 4 cd6c\n"
	"   2: 0100007F:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 4111 1 cd6c\n"
	"   3: 5A02A8C0:0016 5502A8C0:C350 01 00000000:00000000 02:000A68B4 00000000     0        0 4310 4 cd6c\n";

static void test_listening_port_found(void)
{
	T_EQ_INT(tcp_port_listening(tcp, 2222), 1); /* 0x08AE LISTEN (0A) */
	T_EQ_INT(tcp_port_listening(tcp, 8080), 1); /* 0x1F90 on 127.0.0.1 */
}

static void test_established_only_is_not_listening(void)
{
	T_EQ_INT(tcp_port_listening(tcp, 22), 0); /* 0x0016 only as ESTABLISHED (01) */
	T_EQ_INT(tcp_port_listening(tcp, 23), 0);
	T_EQ_INT(tcp_port_listening("", 22), 0);
}

static void test_exe_match_accepts_replaced_binary(void)
{
	T_EQ_INT(ssh_exe_matches(SSH_BIN), 1);
	T_EQ_INT(ssh_exe_matches(SSH_BIN " (deleted)"), 1); /* binary replaced by a package upgrade while running */
	T_EQ_INT(ssh_exe_matches(SSH_BIN "x"), 0);
	T_EQ_INT(ssh_exe_matches("/usr/sbin/dropbear"), 0);
	T_EQ_INT(ssh_exe_matches(SSH_BIN " (deleted)x"), 0);
	T_EQ_INT(ssh_exe_matches(""), 0);
}

int main(void)
{
	T_RUN(test_exe_match_accepts_replaced_binary);
	T_RUN(test_listening_port_found);
	T_RUN(test_established_only_is_not_listening);
	return T_DONE();
}
