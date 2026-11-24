#include "paramfile.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static int cmp_idx(const void *a, const void *b, void *ctx) {
	const paramset_t *ps = ctx;
	return strcmp(ps->v[*(const int *)a].name, ps->v[*(const int *)b].name);
}

bool paramfile_save(const paramset_t *ps, const char *path, const char *header, char *err, size_t errlen) {
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return false;
	}
	int *idx = xmalloc((size_t)MAX(ps->n, 1) * sizeof *idx);
	int n	 = 0;
	for (int i = 0; i < ps->n; i++)
		if (ps->v[i].have_value)
			idx[n++] = i;
	qsort_r(idx, (size_t)n, sizeof *idx, cmp_idx, (void *)ps);
	if (header && *header)
		fprintf(f, "#NOTE: %s\n", header);
	for (int k = 0; k < n; k++) {
		const param_t *p = &ps->v[idx[k]];
		char val[48];
		param_format(p, p->value, val, sizeof val);
		fprintf(f, "%s,%s\n", p->name, val);
	}
	free(idx);
	if (fclose(f) != 0 || rename(tmp, path) != 0) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		unlink(tmp);
		return false;
	}
	return true;
}

static bool valid_name(const char *s) {
	size_t n = strlen(s);
	if (n == 0 || n > PARAM_NAME_LEN)
		return false;
	for (size_t i = 0; i < n; i++)
		if (!(isalnum((unsigned char)s[i]) || s[i] == '_'))
			return false;
	return true;
}

pf_entry_t *paramfile_load(const char *path, int *count, char *err, size_t errlen) {
	FILE *f = fopen(path, "r");
	if (!f) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return NULL;
	}
	int cap = 256, n = 0, bad = 0;
	pf_entry_t *out = xmalloc((size_t)cap * sizeof *out);
	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *s = str_trim(line);
		if (!*s || *s == '#')
			continue;
		char *tok[6];
		int nt	   = 0;
		char *save = NULL;
		for (char *t = strtok_r(s, ",\t ", &save); t && nt < 6; t = strtok_r(NULL, ",\t ", &save))
			tok[nt++] = t;
		const char *name = NULL, *val = NULL;
		if (nt >= 4 && isdigit((unsigned char)tok[0][0]) && isdigit((unsigned char)tok[1][0])) {
			name = tok[2]; /* QGC: sysid compid name value type */
			val	 = tok[3];
		} else if (nt >= 2) {
			name = tok[0];
			val	 = tok[1];
		}
		char *end = NULL;
		double v  = val ? strtod(val, &end) : 0;
		if (!name || !valid_name(name) || end == val || (end && *end)) {
			bad++;
			continue;
		}
		if (n == cap) {
			cap *= 2;
			out = xrealloc(out, (size_t)cap * sizeof *out);
		}
		xstrlcpy(out[n].name, name, sizeof out[n].name);
		out[n].value = v;
		n++;
	}
	fclose(f);
	if (n == 0) {
		snprintf(err, errlen, "%s: no parameters found (%d unparsable lines)", path, bad);
		free(out);
		return NULL;
	}
	*count = n;
	return out;
}
