/* Favourite parameters ("Fav" column), persisted in
 * $XDG_CONFIG_HOME/fpl/favorites (one name per line). */
#pragma once

#include <stdbool.h>

#include "params.h"

typedef struct favs {
	char **names;
	int n, cap;
	char path[1024];
} favs_t;

void favs_load(favs_t *f);
void favs_free(favs_t *f);
bool favs_has(const favs_t *f, const char *name);
/* Toggle and persist. Returns the new state. */
bool favs_toggle(favs_t *f, const char *name);
/* Apply to the param set's `fav` flags. */
void favs_apply(const favs_t *f, paramset_t *ps);
