#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>

double now_s(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

size_t xstrlcpy(char *dst, const char *src, size_t dst_size) {
	size_t len = strlen(src);
	if (dst_size) {
		size_t n = len < dst_size - 1 ? len : dst_size - 1;
		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return len;
}

static void oom(void) {
	fputs("gndtui: out of memory\n", stderr);
	abort();
}

void *xmalloc(size_t n) {
	void *p = malloc(n ? n : 1);
	if (!p)
		oom();
	return p;
}

void *xcalloc(size_t n, size_t sz) {
	void *p = calloc(n ? n : 1, sz ? sz : 1);
	if (!p)
		oom();
	return p;
}

void *xrealloc(void *p, size_t n) {
	p = realloc(p, n ? n : 1);
	if (!p)
		oom();
	return p;
}

char *xstrdup(const char *s) {
	if (!s)
		return NULL;
	char *d = strdup(s);
	if (!d)
		oom();
	return d;
}

char *str_trim(char *s) {
	while (isspace((unsigned char)*s))
		s++;
	size_t n = strlen(s);
	while (n && isspace((unsigned char)s[n - 1]))
		s[--n] = '\0';
	return s;
}

bool str_icontains(const char *hay, const char *needle) {
	if (!needle || !*needle)
		return true;
	if (!hay)
		return false;
	return strcasestr(hay, needle) != NULL;
}

void str_squash_ws(char *s) {
	char *w = s;
	bool sp = false;
	for (char *r = s; *r; r++) {
		if (isspace((unsigned char)*r)) {
			sp = true;
			continue;
		}
		if (sp && w != s)
			*w++ = ' ';
		sp	 = false;
		*w++ = *r;
	}
	*w = '\0';
}

bool mkdir_p(const char *path) {
	char tmp[1024];
	xstrlcpy(tmp, path, sizeof tmp);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) && errno != EEXIST)
				return false;
			*p = '/';
		}
	}
	return !(mkdir(tmp, 0755) && errno != EEXIST);
}

static bool xdg_dir(const char *env, const char *fallback, char *out, size_t n) {
	const char *base = getenv(env);
	if (base && *base) {
		snprintf(out, n, "%s/gndtui", base);
	} else {
		const char *home = getenv("HOME");
		if (!home || !*home)
			return false;
		snprintf(out, n, "%s/%s/gndtui", home, fallback);
	}
	return mkdir_p(out);
}

bool path_cache_dir(char *out, size_t n) { return xdg_dir("XDG_CACHE_HOME", ".cache", out, n); }

bool path_config_dir(char *out, size_t n) { return xdg_dir("XDG_CONFIG_HOME", ".config", out, n); }

void path_expand_home(const char *in, char *out, size_t n) {
	const char *home = getenv("HOME");
	if (in[0] == '~' && (in[1] == '/' || in[1] == '\0') && home)
		snprintf(out, n, "%s%s", home, in + 1);
	else
		xstrlcpy(out, in, n);
}

/* Decode one UTF-8 character; returns its byte length (>=1) and width. */
static int utf8_char(const char *s, int *w) {
	mbstate_t st;
	memset(&st, 0, sizeof st);
	wchar_t wc;
	size_t r = mbrtowc(&wc, s, MB_CUR_MAX, &st);
	if (r == (size_t)-1 || r == (size_t)-2 || r == 0) {
		*w = 1; /* invalid byte: treat as one column so we never stall */
		return 1;
	}
	int cw = wcwidth(wc);
	*w	   = cw < 0 ? 1 : cw;
	return (int)r;
}

int utf8_width(const char *s) {
	int cols = 0;
	while (*s) {
		int w;
		s += utf8_char(s, &w);
		cols += w;
	}
	return cols;
}

size_t utf8_fit(const char *s, int max_cols, int *cols) {
	const char *p = s;
	int used	  = 0;
	while (*p && *p != '\n') {
		int w;
		int len = utf8_char(p, &w);
		if (used + w > max_cols)
			break;
		used += w;
		p += len;
	}
	if (cols)
		*cols = used;
	return (size_t)(p - s);
}

int wrap_text(const char *text, int width, span_t *lines, int max_lines) {
	if (width < 1)
		width = 1;
	int count	  = 0;
	const char *p = text;
	while (*p) {
		/* skip leading spaces of a line */
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		if (*p == '\n') {
			p++;
			continue;
		}
		int cols;
		size_t fit	= utf8_fit(p, width, &cols);
		size_t take = fit;
		if (p[fit] && p[fit] != '\n' && p[fit] != ' ') {
			/* break at last space inside the fitted part, if any */
			size_t sp = fit;
			while (sp > 0 && p[sp - 1] != ' ')
				sp--;
			if (sp > 0)
				take = sp;
		}
		if (take == 0) {
			/* a single character wider than the column: force progress */
			int w;
			take = (size_t)utf8_char(p, &w);
		}
		if (count < max_lines) {
			size_t l = take;
			while (l > 0 && p[l - 1] == ' ')
				l--;
			lines[count].off = (int)(p - text);
			lines[count].len = (int)l;
		}
		count++;
		p += take;
	}
	return count;
}

size_t sappendf(char *buf, size_t cap, size_t len, const char *fmt, ...) {
	if (!cap || len >= cap - 1)
		return cap ? cap - 1 : 0;
	va_list ap;
	va_start(ap, fmt);
	int r = vsnprintf(buf + len, cap - len, fmt, ap);
	va_end(ap);
	if (r < 0)
		return len;
	len += (size_t)r;
	return len < cap ? len : cap - 1;
}

void fmt_number(double v, bool integer, char *out, size_t n) {
	if (isnan(v)) {
		snprintf(out, n, "nan");
		return;
	}
	if (integer) {
		snprintf(out, n, "%.0f", v);
		return;
	}
	/* Parameters are float32: 7 significant digits is the honest precision. */
	snprintf(out, n, "%.7g", v);
	if (strchr(out, 'e')) {
		double a = fabs(v);
		if (a >= 1e-6 && a < 1e15) {
			snprintf(out, n, "%.9f", v);
			char *dot = strchr(out, '.');
			if (dot) {
				char *e = out + strlen(out) - 1;
				while (e > dot && *e == '0')
					*e-- = '\0';
				if (e == dot)
					*e = '\0';
			}
		}
	}
	if (strcmp(out, "-0") == 0)
		snprintf(out, n, "0");
}

static FILE *g_log;

bool log_open(const char *path) {
	g_log = fopen(path, "a");
	if (g_log)
		setvbuf(g_log, NULL, _IOLBF, 0);
	return g_log != NULL;
}

void log_close(void) {
	if (g_log)
		fclose(g_log);
	g_log = NULL;
}

void logf_(const char *fmt, ...) {
	if (!g_log)
		return;
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm tm;
	localtime_r(&ts.tv_sec, &tm);
	fprintf(g_log, "%02d:%02d:%02d.%03ld ", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fputc('\n', g_log);
}
