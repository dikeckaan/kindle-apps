/* SSH password records ("kav1$<salt hex>$<hash hex>"), shared by the app and the patched dropbear. */
#ifndef SSHCFG_H
#define SSHCFG_H
#define SSH_ITERATIONS 10000
/* h0 = sha256(salt || password), h(i+1) = sha256(h(i) || salt), SSH_ITERATIONS times */
int ssh_make_record(const char *password, const unsigned char salt[16], char *out, int cap); /* bytes written or -1 */
int ssh_check_record(const char *record, const char *password); /* 1 match, 0 mismatch, -1 malformed */
int ssh_check_file(const char *path, const char *password);     /* as above; -1 also when missing/symlink */
#endif
