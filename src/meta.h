/* Parameter metadata (units, ranges, enum values, descriptions).
 * Sources: ArduPilot apm.pdef.xml, PX4 parameters JSON (QGC format).
 *
 * Translations: the English fields are always kept; *_tr fields hold a
 * translation when one exists *for exactly that English text* (a stale
 * translation of an older description is never shown). Display code uses
 * the meta_*() accessors, which honour the global on/off switch. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct meta_value {
	double code;
	char *label;
	char *label_tr;
	bool label_mt; /* label_tr is a machine translation */
} meta_value_t;

typedef struct meta_bit {
	int bit;
	char *label;
	char *label_tr;
	bool label_mt;
} meta_bit_t;

typedef struct param_meta {
	char *name;
	char *human; /* short name / short description */
	char *desc;	 /* full description */
	char *units;
	char *user;	 /* "Standard" / "Advanced" (ArduPilot) or category (PX4) */
	char *group; /* PX4 parameter group ("Multicopter Position Control"), NULL for ArduPilot */
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

	char *src_long; /* PX4 longDesc as published (to match translations) */
	char *human_tr, *long_tr, *desc_tr, *options_tr, *user_tr, *group_tr;
	bool human_mt, long_mt, desc_mt; /* machine translations (unreviewed) */
	bool desc_composed;				 /* PX4: desc = shortDesc + longDesc */
} param_meta_t;

typedef struct meta_db {
	param_meta_t **v;
	int n, cap;
	int *htab;
	int hcap;
	char source[256]; /* description for the UI */

	char tr_lang[16];  /* language of applied translations, "" if none */
	int tr_strings;	   /* translated strings applied */
	int tr_stale;	   /* translations skipped: English text changed */
	int tr_mt_strings; /* of tr_strings: machine translations */
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

/* PX4 metadata translations (Qt Linguist .ts from PX4-Metadata-Translations). */
/* machine: the file holds machine translations; they only fill strings the
 * reviewed (community) translation does not have, and are flagged. */
bool meta_load_qt_ts(meta_db_t *db, const char *path, const char *lang, bool machine, char *err, size_t errlen);

/* Display accessors: translated text when available and enabled. */
void meta_set_translate(bool on);
bool meta_translate_enabled(void);
const char *meta_human(const param_meta_t *m);
const char *meta_desc(const param_meta_t *m); /* falls back to the short name */
const char *meta_options(const param_meta_t *m);
const char *meta_vlabel(const meta_value_t *v);
const char *meta_blabel(const meta_bit_t *b);
const char *meta_user_tr(const param_meta_t *m);  /* NULL: use gettext */
const char *meta_group_tr(const param_meta_t *m); /* NULL: show the English group */
/* A translated description is being shown (so the original is worth showing too). */
bool meta_desc_translated(const param_meta_t *m);
/* The shown description / labels contain machine translation. */
bool meta_desc_machine(const param_meta_t *m);
bool meta_labels_machine(const param_meta_t *m);
