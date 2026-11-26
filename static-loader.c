#include <stddef.h>

/* Xlib and GIO can fall back when optional plugins are unavailable. A static
 * executable cannot safely load plugins built for another system's libc. */
void *
__wrap_dlopen(const char *path, int flags)
{
	(void)path;
	(void)flags;
	return NULL;
}
