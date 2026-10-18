/* kui — minimal e-ink UI toolkit for Kindle (FBInk + evdev). */
#ifndef KUI_H
#define KUI_H

/* ---- geometry ---- */
typedef struct { int x, y, w, h; } KuiRect;
int     kui_rect_contains(KuiRect r, int px, int py);
KuiRect kui_rect_union(KuiRect a, KuiRect b); /* an empty rect (w == 0) is ignored */
KuiRect kui_grid_cell(KuiRect area, int cols, int rows, int col, int row, int gap);

/* ---- refresh policy ---- */
typedef enum { KUI_UPD_PARTIAL, KUI_UPD_FULL } KuiUpdKind;
typedef struct { int every; int count; } KuiRefreshPolicy; /* every == 0: never auto full */
void       kui_policy_init(KuiRefreshPolicy *p, int every);
KuiUpdKind kui_policy_next(KuiRefreshPolicy *p, int screen_changed);

/* ---- UTF-8 and Turkish case rules ---- */
int  kui_utf8_decode(const char *s, int *cp); /* bytes consumed; invalid → 1 and cp = 0xFFFD */
int  kui_utf8_encode(int cp, char out[4]);    /* bytes written */
int  kui_utf8_len(const char *s);             /* code points */
int  kui_tr_upper(int cp);
int  kui_tr_lower(int cp);
int  kui_tr_fold_circumflex(int cp);
void kui_tr_upper_str(const char *in, char *out, int outsz);

/* ---- storage ---- */
const char *kui_data_dir(const char *app); /* $KUI_ROOT/data/<app>, created on demand */
int kui_write_atomic(const char *path, const void *buf, int len); /* 0 = ok */
int kui_read_file(const char *path, char *buf, int cap);          /* bytes read (NUL-terminated), -1 if missing */
int kui_cfg_get_int(const char *path, const char *key, int def);
int kui_cfg_set_int(const char *path, const char *key, int val);  /* 0 = ok */

#endif
