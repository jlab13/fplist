/* Parameter files.
 * Save: Mission Planner format ("NAME,VALUE" per line, '#' comments).
 * Load: Mission Planner (comma/space/tab separated) and QGroundControl
 *       ("sysid compid NAME VALUE TYPE", tab separated) formats. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "params.h"

typedef struct pf_entry {
	char name[PARAM_NAME_LEN + 1];
	double value;
} pf_entry_t;

bool paramfile_save(const paramset_t *ps, const char *path, const char *header, char *err, size_t errlen);
/* Returns malloc'd array (caller frees) and count, or NULL on error. */
pf_entry_t *paramfile_load(const char *path, int *count, char *err, size_t errlen);
