#include "i18n.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

#ifdef GNDTUI_NLS
static bool is_dir(const char *p) {
	struct stat st;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

void i18n_init(void) {
#ifdef GNDTUI_NLS
	const char *dir = getenv("GNDTUI_LOCALEDIR");
	char exe_locale[PATH_MAX];
	if (!dir || !*dir) {
		/* running from the build tree: catalogs are built into <exe dir>/locale */
		char exe[PATH_MAX];
		ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
		if (n > 0) {
			exe[n]		= '\0';
			char *slash = strrchr(exe, '/');
			if (slash) {
				*slash = '\0';
				snprintf(exe_locale, sizeof exe_locale, "%s/locale", exe);
				if (is_dir(exe_locale))
					dir = exe_locale;
			}
		}
	}
	if (!dir || !*dir)
		dir = GNDTUI_LOCALEDIR;
	bindtextdomain("gndtui", dir);
	bind_textdomain_codeset("gndtui", "UTF-8");
	textdomain("gndtui");
#endif
}
