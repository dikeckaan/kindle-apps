/* the hub's "Güncellemeler" screen */
#ifndef UPD_UI_H
#define UPD_UI_H
/* returns 1 when the core package (the hub itself) was replaced: the caller restarts */
int hub_updates(const char *root);
#endif
