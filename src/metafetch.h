/* Background download + parse of parameter metadata, with an on-disk cache
 * in $XDG_CACHE_HOME/fpl.
 *   ArduPilot: https://autotest.ardupilot.org/Parameters/versioned/<V>/stable-X.Y.Z/apm.pdef.xml
 *              falling back to .../Parameters/<Vehicle>/apm.pdef.xml (master)
 *   PX4:       PX4ParameterFactMetaData.json from the QGroundControl repo,
 *              translated strings from PX4/PX4-Metadata-Translations
 *              (translated/parameters_<ll_CC>.ts) */
#pragma once

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "meta.h"

typedef struct metafetch_req {
	bool ardupilot;
	char apm_master[32];	/* "ArduCopter", "ArduPlane", "Rover", ... */
	char apm_versioned[32]; /* "Copter", "Plane", ... or "" */
	char version[32];		/* "4.5.7" for official releases, else "" */
	char override_path[1024];
	bool offline;
	char lang[16]; /* PX4 metadata translation, e.g. "uk_UA"; "" = none */
} metafetch_req_t;

typedef struct metafetch {
	pthread_t th;
	bool started;
	atomic_int done;
	metafetch_req_t req;
	meta_db_t *result;
	char msg[512];
} metafetch_t;

void metafetch_global_init(void);
void metafetch_global_cleanup(void);

bool metafetch_start(metafetch_t *mf, const metafetch_req_t *req);
/* Returns true exactly once, when the worker finished. *out may be NULL on
 * failure; msg describes the outcome. Joins the thread. */
bool metafetch_poll(metafetch_t *mf, meta_db_t **out, char *msg, size_t n);
/* Blocks until done (used on shutdown). */
void metafetch_cancel(metafetch_t *mf);
