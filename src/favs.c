#include "favs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static void add(favs_t *f, const char *name) {
	if (f->n == f->cap) {
		f->cap	 = f->cap ? f->cap * 2 : 32;
		f->names = xrealloc(f->names, (size_t)f->cap * sizeof *f->names);
	}
	f->names[f->n++] = xstrdup(name);
}

void favs_load(favs_t *f) {
	memset(f, 0, sizeof *f);
	char dir[900];
	if (!path_config_dir(dir, sizeof dir))
		return;
	snprintf(f->path, sizeof f->path, "%s/favorites", dir);
	FILE *fp = fopen(f->path, "r");
	if (!fp)
		return;
	char line[128];
	while (fgets(line, sizeof line, fp)) {
		char *s = str_trim(line);
		if (*s && *s != '#' && !favs_has(f, s))
			add(f, s);
	}
	fclose(fp);
}

void favs_free(favs_t *f) {
	for (int i = 0; i < f->n; i++)
		free(f->names[i]);
	free(f->names);
	memset(f, 0, sizeof *f);
}

bool favs_has(const favs_t *f, const char *name) {
	for (int i = 0; i < f->n; i++)
		if (strcmp(f->names[i], name) == 0)
			return true;
	return false;
}

static void save(const favs_t *f) {
	if (!f->path[0])
		return;
	FILE *fp = fopen(f->path, "w");
	if (!fp)
		return;
	for (int i = 0; i < f->n; i++)
		fprintf(fp, "%s\n", f->names[i]);
	fclose(fp);
}

bool favs_toggle(favs_t *f, const char *name) {
	for (int i = 0; i < f->n; i++) {
		if (strcmp(f->names[i], name) == 0) {
			free(f->names[i]);
			f->names[i] = f->names[--f->n];
			save(f);
			return false;
		}
	}
	add(f, name);
	save(f);
	return true;
}

void favs_apply(const favs_t *f, paramset_t *ps) {
	for (int i = 0; i < ps->n; i++)
		ps->v[i].fav = favs_has(f, ps->v[i].name);
}
