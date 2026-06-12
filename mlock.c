/* mlock - a pretty screen locker for X
 *
 * Based on slock by the suckless.org community.
 * See LICENSE file for license details. */

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <shadow.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <crypt.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/dpms.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

#include "arg.h"

char *argv0;

enum {
	INIT,
	INPUT,
	INPUT_ALT,
	FAILED,
	CAPS,
	NUMCOLS
};

struct lock {
	int screen;
	Window root, win;
	Pixmap pmap;
	int w, h;
	cairo_surface_t *surface;
};

struct xrandr {
	int active;
	int evbase;
	int errbase;
};

#include "config.h"

static struct xrandr rr;
static double bgcol[NUMCOLS][3];
static double fgcol[3], dimcol[3];
static int failcount = 0;
static int caps = 0;

static void die(const char *errstr, ...) __attribute__((noreturn));

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

#ifdef __linux__
#include <fcntl.h>
#include <linux/oom.h>

static void
dontkillme(void)
{
	FILE *f;
	const char oomfile[] = "/proc/self/oom_score_adj";

	if (!(f = fopen(oomfile, "w"))) {
		if (errno == ENOENT)
			return;
		fprintf(stderr, "mlock: fopen %s: %s\n", oomfile,
		        strerror(errno));
		return;
	}
	fprintf(f, "%d", OOM_SCORE_ADJ_MIN);
	if (fclose(f)) {
		if (errno == EACCES)
			fprintf(stderr, "mlock: unable to disable OOM killer "
			        "(run suid/sgid for full protection)\n");
		else
			fprintf(stderr, "mlock: fclose %s: %s\n",
			        oomfile, strerror(errno));
	}
}
#endif

static const char *
gethash(void)
{
	const char *hash;
	struct passwd *pw;
	struct spwd *sp;

	/* Check if the current user has a password entry */
	errno = 0;
	if (!(pw = getpwuid(getuid()))) {
		if (errno)
			die("mlock: getpwuid: %s\n", strerror(errno));
		else
			die("mlock: cannot retrieve password entry\n");
	}
	hash = pw->pw_passwd;

	if (!strcmp(hash, "x")) {
		if (!(sp = getspnam(pw->pw_name)))
			die("mlock: getspnam: cannot retrieve shadow entry. "
			    "Make sure to suid or sgid mlock.\n");
		hash = sp->sp_pwdp;
	} else if (!strcmp(hash, "*")) {
		die("mlock: getpwuid: cannot retrieve password entry. "
		    "Make sure to suid or sgid mlock.\n");
	}

	return hash;
}

static void
parsecolor(Display *dpy, const char *name, double *rgb)
{
	XColor c;

	if (!XParseColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), name, &c))
		die("mlock: cannot parse color \"%s\"\n", name);
	rgb[0] = c.red   / 65535.0;
	rgb[1] = c.green / 65535.0;
	rgb[2] = c.blue  / 65535.0;
}

static int
getcaps(Display *dpy)
{
	XkbStateRec st;

	if (XkbGetState(dpy, XkbUseCoreKbd, &st) == Success)
		return (st.locked_mods & LockMask) != 0;
	return 0;
}

static PangoLayout *
mklayout(cairo_t *cr, const char *font, const char *text)
{
	PangoLayout *layout;
	PangoFontDescription *desc;

	layout = pango_cairo_create_layout(cr);
	desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, text, -1);
	return layout;
}

/* expand a strftime(3) format string into buf; buf is empty on failure */
static void
fmtdatetime(char *buf, size_t n, const char *fmt)
{
	time_t t;

	buf[0] = '\0';
	if (!fmt || !fmt[0])
		return;
	t = time(NULL);
	if (!strftime(buf, n, fmt, localtime(&t)))
		buf[0] = '\0';
}

/* draw the emoji/title/subtext stack centered in the given rectangle,
 * plus the footer at its bottom edge.
 * Text fields whose datetime_updated flag is set and contain '%' are
 * expanded with strftime(3) every second. */
static void
drawmon(cairo_t *cr, int mx, int my, int mw, int mh, int state)
{
	PangoLayout *lay[3], *flay;
	const double *col[3];
	const char *sub, *title, *footer;
	char titlebuf[256], subbuf[256], failbuf[128], footbuf[256];
	int w[3], h[3], fw, fh, n = 0, i, total = 0, y;

	/* title — expand strftime codes when enabled */
	title = titletext;
	if (titletext && titletext[0] && title_datetime_updated &&
	    strchr(titletext, '%')) {
		fmtdatetime(titlebuf, sizeof(titlebuf), titletext);
		if (titlebuf[0])
			title = titlebuf;
	}

	/* subtitle — expand strftime codes when enabled */
	sub = subtext;
	if (state == FAILED && failcount > 0) {
		snprintf(failbuf, sizeof(failbuf),
		         failcount == 1 ? failformat1 : failformat, failcount);
		sub = failbuf;
	} else if (state == CAPS) {
		sub = capstext;
	} else if (sub && sub[0] && subtitle_datetime_updated &&
	           strchr(sub, '%')) {
		fmtdatetime(subbuf, sizeof(subbuf), sub);
		if (subbuf[0])
			sub = subbuf;
	}

	if (showemoji && emoji[state] && emoji[state][0]) {
		lay[n] = mklayout(cr, emojifont, emoji[state]);
		col[n++] = fgcol;
	}
	if (title && title[0]) {
		lay[n] = mklayout(cr, titlefont, title);
		col[n++] = fgcol;
	}
	if (sub && sub[0]) {
		lay[n] = mklayout(cr, subfont, sub);
		col[n++] = dimcol;
	}

	for (i = 0; i < n; i++) {
		pango_layout_get_pixel_size(lay[i], &w[i], &h[i]);
		total += h[i] + (i > 0 ? textgap : 0);
	}
	y = my + (mh - total) / 2;
	for (i = 0; i < n; i++) {
		cairo_set_source_rgb(cr, col[i][0], col[i][1], col[i][2]);
		cairo_move_to(cr, mx + (mw - w[i]) / 2, y);
		pango_cairo_show_layout(cr, lay[i]);
		y += h[i] + textgap;
		g_object_unref(lay[i]);
	}

	/* footer — expand strftime codes when enabled */
	footer = footertext;
	if (footertext && footertext[0] && footer_datetime_updated &&
	    strchr(footertext, '%')) {
		fmtdatetime(footbuf, sizeof(footbuf), footertext);
		if (footbuf[0])
			footer = footbuf;
	}
	if (footer && footer[0]) {
		flay = mklayout(cr, footerfont, footer);
		pango_layout_get_pixel_size(flay, &fw, &fh);
		cairo_set_source_rgb(cr, dimcol[0], dimcol[1], dimcol[2]);
		cairo_move_to(cr, mx + (mw - fw) / 2, my + mh - fh - bottommargin);
		pango_cairo_show_layout(cr, flay);
		g_object_unref(flay);
	}
}

static void
drawlock(Display *dpy, struct lock *lock, int state)
{
	cairo_t *cr;
	XRRMonitorInfo *mons = NULL;
	int nmon = 0, i;

	cr = cairo_create(lock->surface);
	cairo_set_source_rgb(cr, bgcol[state][0], bgcol[state][1], bgcol[state][2]);
	cairo_paint(cr);

	/* draw once per physical monitor so nothing spans a bezel */
	if (rr.active)
		mons = XRRGetMonitors(dpy, lock->root, True, &nmon);
	if (mons && nmon > 0) {
		for (i = 0; i < nmon; i++)
			drawmon(cr, mons[i].x, mons[i].y,
			        mons[i].width, mons[i].height, state);
	} else {
		drawmon(cr, 0, 0, lock->w, lock->h, state);
	}
	if (mons)
		XRRFreeMonitors(mons);

	cairo_destroy(cr);
	cairo_surface_flush(lock->surface);
	XFlush(dpy);
}

static void
drawall(Display *dpy, struct lock **locks, int nscreens, int state)
{
	int s;

	for (s = 0; s < nscreens; s++)
		drawlock(dpy, locks[s], state);
}

static void
readpw(Display *dpy, struct lock **locks, int nscreens, const char *hash)
{
	XRRScreenChangeNotifyEvent *rre;
	XEvent ev;
	KeySym ksym;
	static char passwd[256];
	char buf[32];
	const char *inputhash;
	unsigned int len = 0;
	int num, s, alt = 0, failure = 0, dirty = 0, running = 1;
	int oldc, color;
	int xfd = ConnectionNumber(dpy);
	int need_timer;
	fd_set fds;
	struct timeval tv;

	/* check if any text field has live strftime codes enabled */
	need_timer =
		(title_datetime_updated && titletext && strchr(titletext, '%')) ||
		(subtitle_datetime_updated && subtext && strchr(subtext, '%')) ||
		(footer_datetime_updated && footertext && strchr(footertext, '%'));

	caps = getcaps(dpy);
	oldc = caps ? CAPS : INIT;
	drawall(dpy, locks, nscreens, oldc);

	while (running) {
		while (running && XPending(dpy)) {
			XNextEvent(dpy, &ev);
			if (ev.type == KeyPress) {
				explicit_bzero(&buf, sizeof(buf));
				num = XLookupString(&ev.xkey, buf, sizeof(buf), &ksym, 0);
				if (IsKeypadKey(ksym)) {
					if (ksym == XK_KP_Enter)
						ksym = XK_Return;
					else if (ksym >= XK_KP_0 && ksym <= XK_KP_9)
						ksym = (ksym - XK_KP_0) + XK_0;
				}
				if (IsFunctionKey(ksym) ||
				    IsKeypadKey(ksym) ||
				    IsMiscFunctionKey(ksym) ||
				    IsPFKey(ksym) ||
				    IsPrivateKeypadKey(ksym))
					continue;
				if ((ev.xkey.state & ControlMask) && ksym == XK_u)
					ksym = XK_Escape;
				switch (ksym) {
				case XK_Return:
					passwd[len] = '\0';
					errno = 0;
					if (!(inputhash = crypt(passwd, hash)))
						fprintf(stderr, "mlock: crypt: %s\n",
						        strerror(errno));
					else
						running = !!strcmp(inputhash, hash);
					if (running) {
						XBell(dpy, 100);
						failure = 1;
						failcount++;
						dirty = 1;
					}
					explicit_bzero(&passwd, sizeof(passwd));
					len = 0;
					break;
				case XK_Escape:
					explicit_bzero(&passwd, sizeof(passwd));
					len = 0;
					break;
				case XK_BackSpace:
					if (len)
						passwd[--len] = '\0';
					break;
				default:
					if (num && !iscntrl((int)buf[0]) &&
					    (len + num < sizeof(passwd))) {
						memcpy(passwd + len, buf, num);
						len += num;
						alt = !alt;
						dirty = 1;
					}
					break;
				}
				caps = getcaps(dpy);
				color = caps ? CAPS
				             : len ? (alt ? INPUT_ALT : INPUT)
				                   : (failure || failonclear) ? FAILED
				                                               : INIT;
				if (running && (oldc != color || dirty)) {
					drawall(dpy, locks, nscreens, color);
					oldc = color;
				}
				dirty = 0;
			} else if (rr.active &&
			           ev.type == rr.evbase + RRScreenChangeNotify) {
				rre = (XRRScreenChangeNotifyEvent *)&ev;
				XRRUpdateConfiguration(&ev);
				for (s = 0; s < nscreens; s++) {
					if (locks[s]->win != rre->window)
						continue;
					if (rre->rotation == RR_Rotate_90 ||
					    rre->rotation == RR_Rotate_270) {
						locks[s]->w = rre->height;
						locks[s]->h = rre->width;
					} else {
						locks[s]->w = rre->width;
						locks[s]->h = rre->height;
					}
					XResizeWindow(dpy, locks[s]->win,
					              locks[s]->w, locks[s]->h);
					cairo_xlib_surface_set_size(locks[s]->surface,
					                            locks[s]->w,
					                            locks[s]->h);
					drawlock(dpy, locks[s], oldc);
					break;
				}
			} else if (ev.type == Expose) {
				if (ev.xexpose.count == 0)
					for (s = 0; s < nscreens; s++)
						if (locks[s]->win == ev.xexpose.window)
							drawlock(dpy, locks[s], oldc);
			} else {
				for (s = 0; s < nscreens; s++)
					XRaiseWindow(dpy, locks[s]->win);
			}
		}
		if (!running)
			break;

		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		if (select(xfd + 1, &fds, NULL, NULL, need_timer ? &tv : NULL) < 0) {
			if (errno != EINTR)
				die("mlock: select: %s\n", strerror(errno));
		}
		if (need_timer) {
			/* redraw so drawmon picks up fresh strftime expansions */
			drawall(dpy, locks, nscreens, oldc);
		}
	}
	explicit_bzero(&passwd, sizeof(passwd));
}

static struct lock *
lockscreen(Display *dpy, int screen)
{
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int i, ptgrab, kbgrab;
	struct lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;

	if (dpy == NULL || screen < 0 || !(lock = malloc(sizeof(struct lock))))
		return NULL;

	lock->screen = screen;
	lock->root = RootWindow(dpy, lock->screen);
	lock->w = DisplayWidth(dpy, lock->screen);
	lock->h = DisplayHeight(dpy, lock->screen);

	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen),
	                 colorname[INIT], &color, &dummy);

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = color.pixel;
	wa.event_mask = ExposureMask | VisibilityChangeMask;
	lock->win = XCreateWindow(dpy, lock->root, 0, 0, lock->w, lock->h,
	                          0, DefaultDepth(dpy, lock->screen),
	                          CopyFromParent,
	                          DefaultVisual(dpy, lock->screen),
	                          CWOverrideRedirect | CWBackPixel | CWEventMask,
	                          &wa);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap,
	                                &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);

	lock->surface = cairo_xlib_surface_create(dpy, lock->win,
	                                          DefaultVisual(dpy, lock->screen),
	                                          lock->w, lock->h);

	/* Try to grab mouse pointer *and* keyboard for 600ms, else fail the lock */
	for (i = 0, ptgrab = kbgrab = -1; i < 6; i++) {
		if (ptgrab != GrabSuccess) {
			ptgrab = XGrabPointer(dpy, lock->root, False,
			                      ButtonPressMask | ButtonReleaseMask |
			                      PointerMotionMask, GrabModeAsync,
			                      GrabModeAsync, None, invisible,
			                      CurrentTime);
		}
		if (kbgrab != GrabSuccess) {
			kbgrab = XGrabKeyboard(dpy, lock->root, True,
			                       GrabModeAsync, GrabModeAsync,
			                       CurrentTime);
		}

		/* input is grabbed: we can lock the screen */
		if (ptgrab == GrabSuccess && kbgrab == GrabSuccess) {
			XMapRaised(dpy, lock->win);
			if (rr.active)
				XRRSelectInput(dpy, lock->win,
				               RRScreenChangeNotifyMask);

			XSelectInput(dpy, lock->root, SubstructureNotifyMask);
			return lock;
		}

		/* retry on AlreadyGrabbed but fail on other errors */
		if ((ptgrab != AlreadyGrabbed && ptgrab != GrabSuccess) ||
		    (kbgrab != AlreadyGrabbed && kbgrab != GrabSuccess))
			break;

		usleep(100000);
	}

	/* we couldn't grab all input: fail out */
	if (ptgrab != GrabSuccess)
		fprintf(stderr, "mlock: unable to grab mouse pointer for screen %d\n",
		        screen);
	if (kbgrab != GrabSuccess)
		fprintf(stderr, "mlock: unable to grab keyboard for screen %d\n",
		        screen);
	cairo_surface_destroy(lock->surface);
	XDestroyWindow(dpy, lock->win);
	XFreePixmap(dpy, lock->pmap);
	free(lock);
	return NULL;
}

static void
unlockscreen(Display *dpy, struct lock *lock)
{
	if (dpy == NULL || lock == NULL)
		return;

	cairo_surface_destroy(lock->surface);
	XDestroyWindow(dpy, lock->win);
	XFreePixmap(dpy, lock->pmap);
	free(lock);
}

static void
usage(void)
{
	die("usage: mlock [-v] [-t title] [-s subtitle] [-b bottomtext]"
	    " [-T 0/1] [-S 0/1] [-B 0/1]"
	    " [cmd [arg ...]]\n");
}

int
main(int argc, char **argv)
{
	struct lock **locks;

	setlocale(LC_ALL, "");
	struct passwd *pwd;
	struct group *grp;
	uid_t duid;
	gid_t dgid;
	const char *hash;
	Display *dpy;
	int i, s, nlocks, nscreens;
	CARD16 dpmsstandby, dpmssuspend, dpmsoff, dpmslevel;
	BOOL dpmson = 0;
	int dpmssaved = 0;

	ARGBEGIN {
	case 'v':
		puts("mlock-"VERSION);
		return 0;
	case 't':
		titletext = EARGF(usage());
		break;
	case 's':
		subtext = EARGF(usage());
		break;
	case 'b':
		footertext = EARGF(usage());
		break;
	case 'T':
		title_datetime_updated = atoi(EARGF(usage()));
		break;
	case 'S':
		subtitle_datetime_updated = atoi(EARGF(usage()));
		break;
	case 'B':
		footer_datetime_updated = atoi(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND

	/* validate drop-user and -group */
	errno = 0;
	if (!(pwd = getpwnam(user)))
		die("mlock: getpwnam %s: %s\n", user,
		    errno ? strerror(errno) : "user entry not found");
	duid = pwd->pw_uid;
	errno = 0;
	if (!(grp = getgrnam(group)))
		die("mlock: getgrnam %s: %s\n", group,
		    errno ? strerror(errno) : "group entry not found");
	dgid = grp->gr_gid;

#ifdef __linux__
	dontkillme();
#endif

	/* the password buffer must never hit swap */
	if (mlockall(MCL_CURRENT) < 0)
		fprintf(stderr, "mlock: mlockall: %s\n", strerror(errno));

	hash = gethash();
	errno = 0;
	if (!crypt("", hash))
		die("mlock: crypt: %s\n", strerror(errno));

	if (!(dpy = XOpenDisplay(NULL)))
		die("mlock: cannot open display\n");

	/* drop privileges */
	if (setgroups(0, NULL) < 0)
		die("mlock: setgroups: %s\n", strerror(errno));
	if (setgid(dgid) < 0)
		die("mlock: setgid: %s\n", strerror(errno));
	if (setuid(duid) < 0)
		die("mlock: setuid: %s\n", strerror(errno));

	/* check for Xrandr support */
	rr.active = XRRQueryExtension(dpy, &rr.evbase, &rr.errbase);

	for (i = 0; i < NUMCOLS; i++)
		parsecolor(dpy, colorname[i], bgcol[i]);
	parsecolor(dpy, textcolor, fgcol);
	parsecolor(dpy, dimtextcolor, dimcol);

	/* get number of screens in display "dpy" and blank them */
	nscreens = ScreenCount(dpy);
	if (!(locks = calloc(nscreens, sizeof(struct lock *))))
		die("mlock: out of memory\n");
	for (nlocks = 0, s = 0; s < nscreens; s++) {
		if ((locks[s] = lockscreen(dpy, s)) != NULL)
			nlocks++;
		else
			break;
	}
	XSync(dpy, 0);

	/* did we manage to lock everything? */
	if (nlocks != nscreens)
		return 1;

	/* turn the monitor off after monitortime seconds */
	if (monitortime > 0 && DPMSCapable(dpy)) {
		DPMSGetTimeouts(dpy, &dpmsstandby, &dpmssuspend, &dpmsoff);
		DPMSInfo(dpy, &dpmslevel, &dpmson);
		if (!dpmson)
			DPMSEnable(dpy);
		DPMSSetTimeouts(dpy, monitortime, monitortime, monitortime);
		dpmssaved = 1;
	}

	/* run post-lock command */
	if (argc > 0) {
		switch (fork()) {
		case -1:
			die("mlock: fork failed: %s\n", strerror(errno));
		case 0:
			if (close(ConnectionNumber(dpy)) < 0)
				die("mlock: close: %s\n", strerror(errno));
			execvp(argv[0], argv);
			fprintf(stderr, "mlock: execvp %s: %s\n",
			        argv[0], strerror(errno));
			_exit(1);
		}
	}

	/* everything is now blank. Wait for the correct password */
	readpw(dpy, locks, nscreens, hash);

	/* password ok, unlock everything and quit */
	if (dpmssaved) {
		DPMSSetTimeouts(dpy, dpmsstandby, dpmssuspend, dpmsoff);
		if (!dpmson)
			DPMSDisable(dpy);
	}
	for (s = 0; s < nscreens; s++)
		unlockscreen(dpy, locks[s]);
	free(locks);
	XCloseDisplay(dpy);

	return 0;
}
