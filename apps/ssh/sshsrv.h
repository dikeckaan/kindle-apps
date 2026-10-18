/* Our SSH server process: paths, liveness check (root-owned pidfile + /proc/<pid>/exe) and port helpers. */
#ifndef SSHSRV_H
#define SSHSRV_H
#define SSH_DIR "/var/local/kindle-apps/ssh" /* root-only (0700): never on world-writable /mnt/us */
#define SSH_PASSWD SSH_DIR "/passwd"
#define SSH_HOSTKEY SSH_DIR "/dropbear_ed25519_host_key"
#define SSH_PIDFILE SSH_DIR "/dropbear.pid"
#define SSH_BIN "/mnt/us/kindle-apps/apps/ssh/dropbear"

/* pure: 1 if `port` is in LISTEN state in /proc/net/tcp-formatted text */
int tcp_port_listening(const char *proc_net_tcp, int port);
/* pid of our running dropbear, or -1 (pidfile must be a root-owned regular file in a root-only dir,
 * the process must be root's and its exe must be SSH_BIN) */
int ssh_server_pid(void);
/* pure: 1 if a /proc/<pid>/exe target is our dropbear (also after a package upgrade replaced the file: " (deleted)") */
int ssh_exe_matches(const char *exe);
/* SIGTERMs (then SIGKILLs after 2 s) every root process running our dropbear: the listener and all
 * logged-in session children. Removes the pidfile once none is left. Returns how many survive (0 = stopped). */
int ssh_stop_all(void);
/* port our running server listens on, or -1 */
int ssh_server_port(void);
#endif
