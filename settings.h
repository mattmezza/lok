#ifndef LOK_SETTINGS_H
#define LOK_SETTINGS_H

#include <X11/Xlib.h>

enum { INIT, INPUT, INPUT_ALT, FAILED, CAPS, NUMCOLS };

struct settings {
	double background[NUMCOLS][3], foreground[3], dim_foreground[3];
	char *emoji[NUMCOLS];
	char *title, *subtitle, *fingerprint_subtitle, *footer;
	char *caps_lock, *failed_attempt, *failed_attempts;
	char *emoji_font, *title_font, *subtitle_font, *footer_font;
	int show_emoji, typing_background_feedback, text_gap, bottom_margin;
	int title_datetime, subtitle_datetime, footer_datetime;
	int fingerprint_enabled, show_fingerprint_icon;
	int fail_on_clear, monitor_timeout;
};

int settings_open(const char *account_home);
int settings_load(struct settings *settings, int fd, Display *display);
char *settings_format_attempts(const char *format, int count);

#endif
