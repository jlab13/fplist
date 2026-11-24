/* Parameter metadata (units, ranges, enum values, descriptions).
 * Sources: ArduPilot apm.pdef.xml, PX4 parameters JSON (QGC format). */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct meta_value {
	double code;
	char *label;
} meta_value_t;

typedef struct meta_bit {
	int bit;
	char *label;
} meta_bit_t;

typedef struct param_meta {
	char *name;
	char *human; /* short name / short description */
	char *desc;	 /* full description */
	char *units;
	char *user; /* "Standard" / "Advanced" (ArduPilot) or category (PX4) */
	bool has_range;
	double min, max;
	bool has_increment;
	double increment;
	bool has_default;
	double def;
	bool readonly;
	bool reboot_required;
	int decimals; /* -1 unknown */
	meta_value_t *values;
	int nvalues;
	meta_bit_t *bits;
	int nbits;
	char *options; /* Mission-Planner-style "Options" column text */
} param_meta_t;

typedef struct meta_db {
	param_meta_t **v;
	int n, cap;
	int *htab;
	int hcap;
	char source[256]; /* description for the UI */
} meta_db_t;

meta_db_t *meta_db_new(void);
void meta_db_free(meta_db_t *db);
const param_meta_t *meta_find(const meta_db_t *db, const char *name);

/* Loaders return false with err filled. Format is sniffed from content. */
bool meta_load_file(meta_db_t *db, const char *path, char *err, size_t errlen);
bool meta_load_apm_xml(meta_db_t *db, const char *path, char *err, size_t errlen);
bool meta_load_px4_json(meta_db_t *db, const char *path, char *err, size_t errlen);

/* Label for an enum value, or NULL. */
const char *meta_value_label(const param_meta_t *m, double v);
