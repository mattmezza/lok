#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>

#include "settings.h"
#include "settings-defaults.h"

struct key_files {
	GKeyFile *defaults;
	GKeyFile *user;
};

int
settings_open(const char *account_home)
{
	const char *base = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	int use_xdg = base && base[0] == '/';
	uid_t uid = getuid(), euid = geteuid();
	gid_t gid = getgid(), egid = getegid();
	char *path;
	int fd, error;

	if (!use_xdg) {
		if (!home || home[0] != '/')
			home = account_home;
		if (!home || home[0] != '/')
			return -1;
	}

	/* Resolve symlinks and check access as the invoking user. The UI reads
	 * the descriptor only after permanently dropping privileges. */
	if (setegid(gid) < 0 || seteuid(uid) < 0) {
		perror("lok: assume invoking identity for config.ini");
		exit(1);
	}
	path = use_xdg ? g_build_filename(base, "lok", "config.ini", NULL) :
	                 g_build_filename(home, ".config", "lok", "config.ini", NULL);
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
	error = errno;
	if (seteuid(euid) < 0 || setegid(egid) < 0) {
		perror("lok: restore startup identity");
		exit(1);
	}
	if (fd < 0 && error != ENOENT)
		fprintf(stderr, "lok: cannot open %s: %s\n", path, strerror(error));
	g_free(path);
	return fd;
}

static GKeyFile *
read_config(int fd)
{
	struct stat info;
	char data[64 * 1024 + 1];
	GKeyFile *keys = NULL;
	GError *error = NULL;
	size_t used = 0;
	ssize_t n;

	if (fd < 0)
		return NULL;
	if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode)) {
		while (used < sizeof(data)) {
			n = read(fd, data + used, sizeof(data) - used);
			if (n == 0)
				break;
			if (n < 0) {
				if (errno == EINTR)
					continue;
				fprintf(stderr, "lok: cannot read config.ini: %s\n", strerror(errno));
				goto done;
			}
			used += n;
		}
		if (used == sizeof(data))
			fprintf(stderr, "lok: config.ini exceeds 64 KiB; using defaults\n");
		else if (!g_utf8_validate(data, used, NULL))
			fprintf(stderr, "lok: config.ini is not UTF-8; using defaults\n");
		else {
			keys = g_key_file_new();
			if (!g_key_file_load_from_data(keys, data, used, G_KEY_FILE_NONE, &error)) {
				fprintf(stderr, "lok: cannot parse config.ini: %s; using defaults\n",
				        error->message);
				g_error_free(error);
				g_clear_pointer(&keys, g_key_file_unref);
			}
		}
	} else {
		fprintf(stderr, "lok: config.ini must be a regular file; using defaults\n");
	}

done:
	close(fd);
	return keys;
}

static void
invalid_value(const char *group, const char *name, GError *error)
{
	fprintf(stderr, "lok: invalid [%s] %s in config.ini; using default\n", group, name);
	g_clear_error(&error);
}

static char *
get_string(struct key_files files, const char *group, const char *name)
{
	GError *error = NULL;
	char *value;

	if (files.user && g_key_file_has_key(files.user, group, name, NULL)) {
		value = g_key_file_get_string(files.user, group, name, &error);
		if (!error)
			return value;
		g_free(value);
		invalid_value(group, name, error);
	}
	return g_key_file_get_string(files.defaults, group, name, NULL);
}

static int
get_bool(struct key_files files, const char *group, const char *name)
{
	GError *error = NULL;
	int value;

	if (files.user && g_key_file_has_key(files.user, group, name, NULL)) {
		value = g_key_file_get_boolean(files.user, group, name, &error);
		if (!error)
			return value;
		invalid_value(group, name, error);
	}
	return g_key_file_get_boolean(files.defaults, group, name, NULL);
}

static int
get_integer(struct key_files files, const char *group, const char *name)
{
	GError *error = NULL;
	int value;

	if (files.user && g_key_file_has_key(files.user, group, name, NULL)) {
		value = g_key_file_get_integer(files.user, group, name, &error);
		if (!error && value >= 0 && value <= UINT16_MAX)
			return value;
		invalid_value(group, name, error);
	}
	return g_key_file_get_integer(files.defaults, group, name, NULL);
}

static int
get_color(struct key_files files, Display *display, const char *name, double *rgb)
{
	XColor color;
	Colormap map = DefaultColormap(display, DefaultScreen(display));
	char *value = get_string(files, "colors", name);
	int valid = value && XParseColor(display, map, value, &color);

	g_free(value);
	if (!valid) {
		invalid_value("colors", name, NULL);
		value = g_key_file_get_string(files.defaults, "colors", name, NULL);
		valid = value && XParseColor(display, map, value, &color);
		g_free(value);
	}
	if (!valid)
		return 0;
	rgb[0] = color.red / 65535.0;
	rgb[1] = color.green / 65535.0;
	rgb[2] = color.blue / 65535.0;
	return 1;
}

int
settings_load(struct settings *settings, int fd, Display *display)
{
	static const char *state_names[NUMCOLS] = {
		[INIT] = "idle", [INPUT] = "typing", [INPUT_ALT] = "typing_alt",
		[FAILED] = "failed", [CAPS] = "caps_lock",
	};
	struct key_files files = { g_key_file_new(), read_config(fd) };
	int valid, i;

	valid = g_key_file_load_from_data(files.defaults, (const char *)default_settings,
	                                 sizeof(default_settings), G_KEY_FILE_NONE, NULL);
	if (!valid)
		goto done;
	for (i = 0; i < NUMCOLS; i++) {
		settings->emoji[i] = get_string(files, "emoji", state_names[i]);
		valid &= get_color(files, display, state_names[i], settings->background[i]);
	}
	valid &= get_color(files, display, "foreground", settings->foreground);
	valid &= get_color(files, display, "dim_foreground", settings->dim_foreground);

	settings->title = get_string(files, "text", "title");
	settings->subtitle = get_string(files, "text", "subtitle");
	settings->fingerprint_subtitle = get_string(files, "text", "fingerprint_subtitle");
	settings->footer = get_string(files, "text", "footer");
	settings->caps_lock = get_string(files, "text", "caps_lock");
	settings->failed_attempt = get_string(files, "text", "failed_attempt");
	settings->failed_attempts = get_string(files, "text", "failed_attempts");
	settings->emoji_font = get_string(files, "fonts", "emoji");
	settings->title_font = get_string(files, "fonts", "title");
	settings->subtitle_font = get_string(files, "fonts", "subtitle");
	settings->footer_font = get_string(files, "fonts", "footer");
	settings->show_emoji = get_bool(files, "appearance", "show_emoji");
	settings->typing_background_feedback = get_bool(files, "appearance", "typing_background_feedback");
	settings->text_gap = get_integer(files, "appearance", "text_gap");
	settings->bottom_margin = get_integer(files, "appearance", "bottom_margin");
	settings->title_datetime = get_bool(files, "datetime", "title");
	settings->subtitle_datetime = get_bool(files, "datetime", "subtitle");
	settings->footer_datetime = get_bool(files, "datetime", "footer");
	settings->fingerprint_enabled = get_bool(files, "fingerprint", "enabled");
	settings->show_fingerprint_icon = get_bool(files, "fingerprint", "show_icon");
	settings->fail_on_clear = get_bool(files, "lock", "fail_on_clear");
	settings->monitor_timeout = get_integer(files, "lock", "monitor_timeout");

done:
	g_key_file_unref(files.defaults);
	g_clear_pointer(&files.user, g_key_file_unref);
	return valid;
}

char *
settings_format_attempts(const char *format, int count)
{
	GString *text = g_string_new(NULL);

	/* Expand the two documented tokens and copy every other byte literally. */
	while (*format) {
		if (format[0] == '%' && format[1] == 'd') {
			g_string_append_printf(text, "%d", count);
			format += 2;
		} else {
			if (format[0] == '%' && format[1] == '%')
				format++;
			g_string_append_c(text, *format++);
		}
	}
	return g_string_free(text, FALSE);
}
