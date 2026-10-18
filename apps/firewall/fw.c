#include <string.h>
#include "fw.h"

int fw_is_open_policy(const char *out)
{
	if (!strstr(out, "-P INPUT ACCEPT")) return 0;
	return !strstr(out, "-j DROP") && !strstr(out, "-j REJECT");
}

