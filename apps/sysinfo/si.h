/* System information: parsers and Turkish formatting for /proc, powerd and firmware data. */
#ifndef SI_H
#define SI_H
long si_parse_uptime(const char *proc_uptime);                       /* seconds, -1 if unparsable */
int  si_parse_meminfo(const char *meminfo, long *total, long *avail); /* bytes; 0 ok */
void si_fmt_bytes(long long b, char *out, int cap);                   /* "1,2 GB"; "—" when < 0 */
void si_fmt_duration(long s, char *out, int cap);                     /* "3 gün 4 sa", "2 sa 5 dk" */
int  si_batt_temp_c(int fahrenheit);                                  /* powerd battTemperature → °C */
void si_parse_firmware(const char *prettyversion, char *out, int cap); /* "5.19.2" */
#endif
