#include "i18n.h"

#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

#ifdef FPL_NLS
static bool is_dir(const char *p) {
	struct stat st;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

void i18n_init(void) {
#ifdef FPL_NLS
	const char *dir = getenv("FPL_LOCALEDIR");
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
		dir = FPL_LOCALEDIR;
	bindtextdomain("fpl", dir);
	bind_textdomain_codeset("fpl", "UTF-8");
	textdomain("fpl");
#endif
}

void i18n_meta_lang(char *out, size_t n) {
	static const struct {
		const char *ll, *cc;
	} region[] = {
			{"uk", "UA"},
			{"de", "DE"},
			{"ko", "KR"},
			{"nl", "NL"},
			{"zh", "CN"},
			{"fr", "FR"},
			{"es", "ES"},
			{"it", "IT"},
			{"pl", "PL"},
			{"pt", "BR"},
			{"ja", "JP"},
			{"tr", "TR"},
	};
	out[0]			= '\0';
	const char *loc = setlocale(LC_MESSAGES, NULL);
	if (!loc || strcmp(loc, "C") == 0 || strcmp(loc, "POSIX") == 0)
		return; /* gettext ignores $LANGUAGE in the C locale too */
	const char *lang = getenv("LANGUAGE");
	char buf[64];
	xstrlcpy(buf, lang && *lang ? lang : loc, sizeof buf);
	buf[strcspn(buf, ":.@")] = '\0'; /* first of "uk:en", drop ".UTF-8", "@euro" */
	if (!buf[0] || strcmp(buf, "C") == 0 || strncmp(buf, "en", 2) == 0)
		return;
	if (!strchr(buf, '_')) {
		for (size_t i = 0; i < ARRAY_LEN(region); i++)
			if (strcmp(buf, region[i].ll) == 0) {
				snprintf(out, n, "%s_%s", buf, region[i].cc);
				return;
			}
		return;
	}
	xstrlcpy(out, buf, n);
}
