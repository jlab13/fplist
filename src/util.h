/* Small shared helpers: time, strings, paths, UTF-8 width, debug log. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

#if defined(__GNUC__)
#define PRINTF_LIKE(f, a) __attribute__((format(printf, f, a)))
#else
#define PRINTF_LIKE(f, a)
#endif

/* Monotonic seconds. */
double now_s(void);

/* Bounded copy, always NUL-terminates (dst_size > 0). Returns strlen(src). */
size_t xstrlcpy(char *dst, const char *src, size_t dst_size);
/* strdup that aborts on OOM; NULL in -> NULL out. */
char *xstrdup(const char *s);
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
/* Trim leading/trailing whitespace in place, returns s (possibly advanced). */
char *str_trim(char *s);
/* Case-insensitive substring search. */
bool str_icontains(const char *hay, const char *needle);
/* Collapse runs of whitespace (incl. newlines) into a single space, in place. */
void str_squash_ws(char *s);

/* Directories (created on demand). Return false if HOME is unknown. */
bool path_cache_dir(char *out, size_t n);  /* $XDG_CACHE_HOME/fpl */
bool path_config_dir(char *out, size_t n); /* $XDG_CONFIG_HOME/fpl */
bool mkdir_p(const char *path);
/* Find a bundled data file (relative path, e.g. "px4/x.ts"): $FPL_DATADIR,
 * then <exe dir>/data (build tree), then the install prefix. */
bool find_data_file(const char *rel, char *out, size_t n);
/* Expand a leading "~/" using $HOME. */
void path_expand_home(const char *in, char *out, size_t n);

/* UTF-8 aware terminal width helpers. */
int utf8_width(const char *s);
/* Number of bytes of s that fit into max_cols columns; *cols gets the used
 * width. Stops at NUL or at newline. */
size_t utf8_fit(const char *s, int max_cols, int *cols);

/* Word-wrap `text` into lines of at most `width` columns. Writes up to
 * `max_lines` (start, length) byte spans and returns the number of lines
 * needed (which can exceed max_lines). */
typedef struct {
	int off;
	int len;
} span_t;
int wrap_text(const char *text, int width, span_t *lines, int max_lines);

/* snprintf-append that never overruns: returns the new length (< cap). */
size_t sappendf(char *buf, size_t cap, size_t len, const char *fmt, ...) PRINTF_LIKE(4, 5);

/* Format a double compactly without exponent where reasonable. */
void fmt_number(double v, bool integer, char *out, size_t n);

/* Debug log (no-op unless log_open() succeeded). */
bool log_open(const char *path);
void log_close(void);
void logf_(const char *fmt, ...) PRINTF_LIKE(1, 2);
#define LOG(...) logf_(__VA_ARGS__)
