/* Uygulamalar: a third-party screen program left running in the background (gambatte-k2, KOReader, kterm …) redraws over
 * our screen and takes the touches: warn and offer to close it. */
#ifndef GUARD_H
#define GUARD_H
/* Scans once (cheap: /proc, plus environ/fd of the few FOREIGN processes). Asks "Kapat / Devam"; "Devam" is remembered
 * for these processes until the hub restarts. Returns 1 when a dialog was shown: the caller redraws its screen (full). */
int hub_guard(void (*redraw)(void));
#endif
