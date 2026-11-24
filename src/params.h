/* Parameter store: values as received from the vehicle plus local edits. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "meta.h"

#define PARAM_NAME_LEN 16

typedef enum {
	PW_NONE = 0, /* nothing to write */
	PW_QUEUED,	 /* user asked to write, not sent yet */
	PW_SENT,	 /* PARAM_SET in flight, waiting for PARAM_VALUE echo */
	PW_FAILED,	 /* gave up / vehicle answered with another value */
} pwrite_state_t;

typedef struct param {
	char name[PARAM_NAME_LEN + 1];
	uint8_t type; /* MAV_PARAM_TYPE_* */
	float raw;	  /* param_value exactly as received (for re-decoding) */
	double value; /* decoded value */
	int index;	  /* vehicle's param_index, -1 if unknown */
	bool have_value;

	bool pending; /* user edited, not yet written */
	double pending_value;
	pwrite_state_t wstate;
	double wsent_at;
	int wtries;

	bool fav;
	const param_meta_t *meta; /* may be NULL */

	/* QGC-style grouping (ps_assign_groups): category is a static or
	 * metadata-owned string, group is copied (PX4 names or name prefix). */
	const char *category;
	char group[48];
} param_t;

typedef struct paramset {
	param_t *v;
	int n, cap;
	int *htab; /* open addressing: index+1, 0 = empty */
	int hcap;

	int expected; /* param_count reported by the vehicle (0 = unknown) */
	uint8_t *got; /* per-index "received" flags, size expected */
	int got_count;

	unsigned generation; /* bumps when params are added/removed */
	unsigned revision;	 /* bumps on any value / state change (for redraw) */
} paramset_t;

void ps_init(paramset_t *ps);
void ps_free(paramset_t *ps);
/* Forget everything (new vehicle / full refresh). */
void ps_clear(paramset_t *ps);
/* Reset the download bookkeeping only (values are kept). */
void ps_reset_download(paramset_t *ps);
void ps_set_expected(paramset_t *ps, int count);
/* Mark an index as received; returns true if it was new. */
bool ps_mark_index(paramset_t *ps, int index);

int ps_find(const paramset_t *ps, const char *name); /* index or -1 */
/* Find or add; *created set when new. Returns index. */
int ps_upsert(paramset_t *ps, const char *name, bool *created);

int ps_pending_count(const paramset_t *ps);
void ps_attach_meta(paramset_t *ps, const meta_db_t *db);

#define PARAM_GROUP_MISC "Misc"
#define PARAM_CATEGORY_OTHER "Other"
/* Category / group like QGroundControl:
 *  PX4:       category and group from the metadata ("Standard" / "Misc"
 *             when missing)
 *  ArduPilot: category from the metadata "User" field ("Advanced" without
 *             metadata, "Other" when empty); group = name up to the first
 *             '_' without trailing digits (SERIAL1_BAUD -> SERIAL); groups
 *             with a single parameter go to "Misc". */
void ps_assign_groups(paramset_t *ps, bool ardupilot);

/* MAV_PARAM_TYPE helpers */
bool param_type_is_int(uint8_t type);
const char *param_type_name(uint8_t type);
/* Representable range of the type (for validation). */
void param_type_limits(uint8_t type, double *lo, double *hi);
void param_format(const param_t *p, double v, char *out, int n);

/* Wire encoding of values in PARAM_VALUE / PARAM_SET. */
typedef enum { PENC_CCAST, PENC_BYTEWISE } param_enc_t;
double param_decode(param_enc_t enc, uint8_t type, float raw);
float param_encode(param_enc_t enc, uint8_t type, double v);
