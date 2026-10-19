/* RAM copies of binaries: an update overwrites files on /mnt/us (fuse), and a running program whose binary is replaced
 * there dies with SIGBUS. The hub runs apps from these copies, Satranç its engine. */
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kui.h"

#define RAM_DIR "/tmp/kui-bin"

/* RAM_DIR: ours alone (root-owned, 0700, not a link), so nobody else can swap a file in it between copy and exec */
static int ram_dir_ok(void)
{
	struct stat st;
	mkdir(RAM_DIR, 0700);
	return lstat(RAM_DIR, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && (st.st_mode & 077) == 0;
}

/* copies a binary to RAM_DIR/<name>: an update overwrites files on /mnt/us (fuse), and a running program whose binary
 * is replaced there dies with SIGBUS (seen: Satranç while the packages were reinstalled). NULL when it cannot. */
const char *kui_ram_copy(const char *exe, const char *name)
{
	static char dst[96];
	char tmp[104];
	if (!ram_dir_ok()) return NULL;
	snprintf(dst, sizeof dst, "%s/%s", RAM_DIR, name);
	snprintf(tmp, sizeof tmp, "%s.new", dst);
	unlink(tmp);
	int in = open(exe, O_RDONLY), out = in < 0 ? -1 : open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0700);
	char buf[65536];
	ssize_t n = 0;
	while (out >= 0 && (n = read(in, buf, sizeof buf)) > 0)
		if (write(out, buf, (size_t) n) != n) n = -1;
	if (in >= 0) close(in);
	if (out < 0) return NULL;
	if (close(out) != 0 || n < 0 || rename(tmp, dst) != 0) {
		unlink(tmp);
		return NULL;
	}
	return dst;
}
