/* Weather drawing shared by the weather and clock apps (device only). */
#ifndef WXDRAW_H
#define WXDRAW_H
#include "kui.h"
#include "wx.h"
void wx_draw_icon(KuiRect r, WxIcon ic); /* black-and-white glyph icon centred in r */
void wx_fmt_temp(double t, char *out, int cap); /* "13°", "-2°", "—" */
#endif
