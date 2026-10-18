/* Masa Panosu data: prayer times (Aladhan, Diyanet method), Wikipedia "on this day", moon phase, calendar facts. */
#ifndef PANO_H
#define PANO_H

enum { PANO_IMSAK, PANO_GUNES, PANO_OGLE, PANO_IKINDI, PANO_AKSAM, PANO_YATSI, PANO_N_PRAYER };
typedef struct { char t[PANO_N_PRAYER][6]; char date[11]; } PanoPrayer; /* "HH:MM", date "DD-MM-YYYY" */
typedef struct { int year; char text[400]; } PanoEvent;

void pano_prayer_url(double lat, double lon, int day, int month, int year, char *out, int cap);
int  pano_parse_prayer(const char *json, PanoPrayer *out); /* 0 ok */
const char *pano_prayer_name(int i);                      /* Turkish source text ("İmsak" …), show with T() */
/* the next prayer after minute-of-day now (0..1439) and the minutes until it; after yatsı, tomorrow's imsak */
int  pano_next_prayer(const PanoPrayer *p, int now_min, int *mins_left);

void pano_onthisday_url(const char *lang, int month, int day, char *out, int cap);
int  pano_parse_onthisday(const char *json, PanoEvent *out, int max);

double pano_moon_age(long unix_time);          /* days since the last new moon (0 … 29.53) */
const char *pano_moon_phase_name(double age);  /* Turkish source text */
int  pano_moon_lit_percent(double age);

int pano_iso_week(int year, int month, int day);
int pano_day_of_year(int year, int month, int day);
#endif
