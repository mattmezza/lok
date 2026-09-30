/* LD_PRELOAD shim for testing lok without root.
 *
 * When LOK_TEST_HASH is set in the environment it:
 *   - makes getpwuid() return that hash as the password entry, so no
 *     shadow access (and thus no setuid) is needed
 *   - redirects the oom_score_adj write to /dev/null
 *   - turns the privilege drop (setgroups/setgid/setuid) into a no-op
 *
 * This only affects the test process; the installed binary is untouched.
 * Build: cc -shared -fPIC -o shim.so shim.c
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int
active(void)
{
	return getenv("LOK_TEST_HASH") != NULL;
}

struct passwd *
getpwuid(uid_t uid)
{
	static struct passwd pw;
	static char hash[512];
	struct passwd *(*real)(uid_t) =
		(struct passwd *(*)(uid_t))dlsym(RTLD_NEXT, "getpwuid");
	struct passwd *p = real(uid);

	if (!p || !active())
		return p;
	pw = *p;
	snprintf(hash, sizeof(hash), "%s", getenv("LOK_TEST_HASH"));
	pw.pw_passwd = hash;
	return &pw;
}

FILE *
fopen(const char *path, const char *mode)
{
	FILE *(*real)(const char *, const char *) =
		(FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen");

	if (active() && !strcmp(path, "/proc/self/oom_score_adj"))
		path = "/dev/null";
	return real(path, mode);
}

int
setgroups(size_t size, const gid_t *list)
{
	int (*real)(size_t, const gid_t *) =
		(int (*)(size_t, const gid_t *))dlsym(RTLD_NEXT, "setgroups");

	return active() ? 0 : real(size, list);
}

int
setgid(gid_t gid)
{
	int (*real)(gid_t) = (int (*)(gid_t))dlsym(RTLD_NEXT, "setgid");

	return active() ? 0 : real(gid);
}

int
setuid(uid_t uid)
{
	int (*real)(uid_t) = (int (*)(uid_t))dlsym(RTLD_NEXT, "setuid");

	return active() ? 0 : real(uid);
}

/* Force a blocked fingerprint scan for the password responsiveness test. */
#include <security/pam_appl.h>
#include <sys/stat.h>

int
lstat(const char *path, struct stat *st)
{
	int (*real)(const char *, struct stat *) = dlsym(RTLD_NEXT, "lstat");
	if (active() && !strcmp(path, "/etc/pam.d/lok-fingerprint")) {
		memset(st, 0, sizeof(*st));
		st->st_mode = S_IFREG | 0644;
		return 0;
	}
	return real(path, st);
}

int
pam_start(const char *service, const char *username,
          const struct pam_conv *conv, pam_handle_t **pamh)
{
	int (*real)(const char *, const char *, const struct pam_conv *,
	            pam_handle_t **) = dlsym(RTLD_NEXT, "pam_start");
	if (active())
		for (;;) pause();
	return real(service, username, conv, pamh);
}
