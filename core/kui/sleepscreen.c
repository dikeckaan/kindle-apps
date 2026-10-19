/* Our own sleep screen. While a kui app runs, kui-run.sh pauses the Kindle UI (awesome, pillow), so the Kindle's
 * screensaver never gets drawn: after the power button the app's last screen stayed up as if frozen. Like KOReader,
 * we draw a sleep screen ourselves when powerd announces goingToScreenSaver; KUI_EV_WAKE then redraws the app. */
#include <stdio.h>
#include <time.h>
#include "kui.h"

void kui_sleep_screen(void)
{
	int w = kui_width(), h = kui_height();
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char date[96], batt[64];
	kui_tr_date(&tm, date, sizeof date);
	int lvl = kui_battery_level();
	batt[0] = '\0';
	if (lvl >= 0) snprintf(batt, sizeof batt, T("Pil %%%d"), lvl);

	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_text((KuiRect){ 0, h / 2 - 420, w, 260 }, "☾", KUI_FONT_REG, 220, KUI_ALIGN_CENTER, 0);
	kui_text((KuiRect){ 40, h / 2 - 130, w - 80, 110 }, T("Uyku modunda"), KUI_FONT_BOLD, 76, KUI_ALIGN_CENTER, 0);
	kui_fill((KuiRect){ w / 2 - 160, h / 2 + 4, 320, 4 }, 0);
	kui_text((KuiRect){ 40, h / 2 + 40, w - 80, 70 }, date, KUI_FONT_REG, 44, KUI_ALIGN_CENTER, 0);
	if (batt[0]) kui_text((KuiRect){ 40, h / 2 + 120, w - 80, 60 }, batt, KUI_FONT_REG, 36, KUI_ALIGN_CENTER, 90);
	kui_text((KuiRect){ 40, h - 220, w - 80, 70 }, T("Açmak için güç tuşuna basın"), KUI_FONT_BOLD, 42, KUI_ALIGN_CENTER, 0);
	kui_flush_sleep();
}
