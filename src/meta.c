#include "meta.h"

#include <ctype.h>
#include <errno.h>
#include <expat.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "util.h"

/* ------------------------------------------------------------------------ */
/* database                                                                   */

static uint32_t hash_name(const char *s) {
	uint32_t h = 2166136261u;
	for (; *s; s++) {
		h ^= (uint8_t)*s;
		h *= 16777619u;
	}
	return h;
}

static void rehash(meta_db_t *db, int cap) {
	free(db->htab);
	db->hcap = cap;
	db->htab = xcalloc((size_t)cap, sizeof(int));
	for (int i = 0; i < db->n; i++) {
		uint32_t h = hash_name(db->v[i]->name) & (uint32_t)(cap - 1);
		while (db->htab[h])
			h = (h + 1) & (uint32_t)(cap - 1);
		db->htab[h] = i + 1;
	}
}

meta_db_t *meta_db_new(void) {
	meta_db_t *db = xcalloc(1, sizeof *db);
	rehash(db, 1024);
	return db;
}

static void meta_free_one(param_meta_t *m) {
	if (!m)
		return;
	free(m->name);
	free(m->human);
	free(m->desc);
	free(m->units);
	free(m->user);
	free(m->options);
	for (int i = 0; i < m->nvalues; i++)
		free(m->values[i].label);
	free(m->values);
	for (int i = 0; i < m->nbits; i++)
		free(m->bits[i].label);
	free(m->bits);
	free(m);
}

void meta_db_free(meta_db_t *db) {
	if (!db)
		return;
	for (int i = 0; i < db->n; i++)
		meta_free_one(db->v[i]);
	free(db->v);
	free(db->htab);
	free(db);
}

const param_meta_t *meta_find(const meta_db_t *db, const char *name) {
	if (!db || !name)
		return NULL;
	uint32_t mask = (uint32_t)(db->hcap - 1);
	for (uint32_t h = hash_name(name) & mask;; h = (h + 1) & mask) {
		int e = db->htab[h];
		if (!e)
			return NULL;
		if (strcmp(db->v[e - 1]->name, name) == 0)
			return db->v[e - 1];
	}
}

/* Takes ownership of m. Returns false (and frees m) on duplicates. */
static bool meta_insert(meta_db_t *db, param_meta_t *m) {
	if (!m->name || !*m->name || meta_find(db, m->name)) {
		meta_free_one(m);
		return false;
	}
	if (db->n == db->cap) {
		db->cap = db->cap ? db->cap * 2 : 1024;
		db->v	= xrealloc(db->v, (size_t)db->cap * sizeof *db->v);
	}
	db->v[db->n++] = m;
	if (db->n * 2 > db->hcap) {
		rehash(db, db->hcap * 2);
	} else {
		uint32_t mask = (uint32_t)(db->hcap - 1);
		uint32_t h	  = hash_name(m->name) & mask;
		while (db->htab[h])
			h = (h + 1) & mask;
		db->htab[h] = db->n;
	}
	return true;
}

const char *meta_value_label(const param_meta_t *m, double v) {
	if (!m)
		return NULL;
	for (int i = 0; i < m->nvalues; i++)
		if (fabs(m->values[i].code - v) < 1e-6)
			return m->values[i].label;
	return NULL;
}

static void add_value(param_meta_t *m, double code, const char *label) {
	m->values					= xrealloc(m->values, (size_t)(m->nvalues + 1) * sizeof *m->values);
	m->values[m->nvalues].code	= code;
	m->values[m->nvalues].label = xstrdup(label);
	m->nvalues++;
}

static void add_bit(param_meta_t *m, int bit, const char *label) {
	if (bit < 0 || bit > 63)
		return;
	m->bits					= xrealloc(m->bits, (size_t)(m->nbits + 1) * sizeof *m->bits);
	m->bits[m->nbits].bit	= bit;
	m->bits[m->nbits].label = xstrdup(label);
	m->nbits++;
}

/* Builds the Mission Planner style "Options" text. */
static void finalize(param_meta_t *m) {
	if (m->desc)
		str_squash_ws(m->desc);
	if (m->human)
		str_squash_ws(m->human);

	size_t cap = 64, len = 0;
	for (int i = 0; i < m->nvalues; i++)
		cap += strlen(m->values[i].label) + 24;
	for (int i = 0; i < m->nbits; i++)
		cap += strlen(m->bits[i].label) + 24;
	char *s = xmalloc(cap);
	s[0]	= '\0';
	char num[32];
	if (m->nvalues) {
		for (int i = 0; i < m->nvalues; i++) {
			fmt_number(m->values[i].code, false, num, sizeof num);
			len = sappendf(s, cap, len, "%s%s:%s", i ? " " : "", num, m->values[i].label);
		}
	} else if (m->nbits) {
		len = sappendf(s, cap, len, "Bitmask");
		for (int i = 0; i < m->nbits; i++)
			len = sappendf(s, cap, len, " %d:%s", m->bits[i].bit, m->bits[i].label);
	} else if (m->has_range) {
		char a[32], b[32];
		fmt_number(m->min, false, a, sizeof a);
		fmt_number(m->max, false, b, sizeof b);
		snprintf(s, cap, "%s %s", a, b);
	}
	m->options = s;
}

/* "0:Disabled,1:Enabled" style lists (old ArduPilot "Values"/"Bitmask" fields). */
static void parse_pairs(param_meta_t *m, const char *text, bool bits) {
	char *dup  = xstrdup(text);
	char *save = NULL;
	for (char *tok = strtok_r(dup, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		char *colon = strchr(tok, ':');
		if (!colon)
			continue;
		*colon		= '\0';
		char *code	= str_trim(tok);
		char *label = str_trim(colon + 1);
		char *end;
		double v = strtod(code, &end);
		if (end == code)
			continue;
		if (bits)
			add_bit(m, (int)v, label);
		else
			add_value(m, v, label);
	}
	free(dup);
}

/* ------------------------------------------------------------------------ */
/* ArduPilot apm.pdef.xml (expat)                                             */

typedef struct apm_parser {
	meta_db_t *db;
	param_meta_t *cur;
	char field[32];
	double code;
	enum { T_NONE, T_FIELD, T_VALUE, T_BIT } collecting;
	char *text;
	size_t tlen, tcap;
	bool bits_from_field, values_from_field;
} apm_parser_t;

static const char *attr(const XML_Char **atts, const char *key) {
	for (int i = 0; atts[i]; i += 2)
		if (strcmp(atts[i], key) == 0)
			return atts[i + 1];
	return NULL;
}

static void XMLCALL apm_start(void *ud, const XML_Char *el, const XML_Char **atts) {
	apm_parser_t *p = ud;
	if (strcmp(el, "param") == 0) {
		meta_free_one(p->cur);
		p->cur			   = xcalloc(1, sizeof *p->cur);
		p->cur->decimals   = -1;
		p->bits_from_field = p->values_from_field = false;
		const char *name						  = attr(atts, "name");
		if (name) {
			const char *colon = strchr(name, ':'); /* "ArduCopter:FOO" */
			p->cur->name	  = xstrdup(colon ? colon + 1 : name);
		}
		p->cur->human = xstrdup(attr(atts, "humanName"));
		p->cur->desc  = xstrdup(attr(atts, "documentation"));
		p->cur->user  = xstrdup(attr(atts, "user"));
		return;
	}
	if (!p->cur)
		return;
	const char *code = attr(atts, "code");
	if (strcmp(el, "field") == 0) {
		const char *n = attr(atts, "name");
		xstrlcpy(p->field, n ? n : "", sizeof p->field);
		p->collecting = T_FIELD;
	} else if (strcmp(el, "value") == 0 && code) {
		p->code		  = strtod(code, NULL);
		p->collecting = T_VALUE;
	} else if (strcmp(el, "bit") == 0 && code) {
		p->code		  = strtod(code, NULL);
		p->collecting = T_BIT;
	} else {
		return;
	}
	p->tlen = 0;
	if (p->text)
		p->text[0] = '\0';
}

static void XMLCALL apm_chars(void *ud, const XML_Char *s, int len) {
	apm_parser_t *p = ud;
	if (p->collecting == T_NONE)
		return;
	if (p->tlen + (size_t)len + 1 > p->tcap) {
		p->tcap = (p->tlen + (size_t)len + 1) * 2;
		p->text = xrealloc(p->text, p->tcap);
	}
	memcpy(p->text + p->tlen, s, (size_t)len);
	p->tlen += (size_t)len;
	p->text[p->tlen] = '\0';
}

static void apm_field(apm_parser_t *p, const char *f, char *v) {
	param_meta_t *m = p->cur;
	v				= str_trim(v);
	if (strcmp(f, "Units") == 0) {
		free(m->units);
		m->units = xstrdup(v);
	} else if (strcmp(f, "Range") == 0) {
		char *end;
		double lo = strtod(v, &end);
		if (end != v) {
			char *end2;
			double hi = strtod(end, &end2);
			if (end2 != end) {
				m->has_range = true;
				m->min		 = lo;
				m->max		 = hi;
			}
		}
	} else if (strcmp(f, "Increment") == 0) {
		char *end;
		double inc = strtod(v, &end);
		if (end != v) {
			m->has_increment = true;
			m->increment	 = inc;
		}
	} else if (strcmp(f, "ReadOnly") == 0) {
		m->readonly = strcasecmp(v, "True") == 0;
	} else if (strcmp(f, "RebootRequired") == 0) {
		m->reboot_required = strcasecmp(v, "True") == 0;
	} else if (strcmp(f, "Bitmask") == 0) {
		if (m->nbits == 0) {
			parse_pairs(m, v, true);
			p->bits_from_field = true;
		}
	} else if (strcmp(f, "Values") == 0) {
		if (m->nvalues == 0) {
			parse_pairs(m, v, false);
			p->values_from_field = true;
		}
	} else if (strcmp(f, "Description") == 0 && (!m->desc || !*m->desc)) {
		free(m->desc);
		m->desc = xstrdup(v);
	}
}

static void XMLCALL apm_end(void *ud, const XML_Char *el) {
	apm_parser_t *p = ud;
	if (!p->cur)
		return;
	const char *text = p->text ? p->text : "";
	if (strcmp(el, "param") == 0) {
		finalize(p->cur);
		meta_insert(p->db, p->cur);
		p->cur = NULL;
	} else if (p->collecting == T_FIELD && strcmp(el, "field") == 0) {
		char *dup = xstrdup(text);
		apm_field(p, p->field, dup);
		free(dup);
	} else if (p->collecting == T_VALUE && strcmp(el, "value") == 0) {
		if (p->values_from_field) { /* element list is authoritative */
			for (int i = 0; i < p->cur->nvalues; i++)
				free(p->cur->values[i].label);
			p->cur->nvalues		 = 0;
			p->values_from_field = false;
		}
		char *dup = xstrdup(text);
		add_value(p->cur, p->code, str_trim(dup));
		free(dup);
	} else if (p->collecting == T_BIT && strcmp(el, "bit") == 0) {
		if (p->bits_from_field) {
			for (int i = 0; i < p->cur->nbits; i++)
				free(p->cur->bits[i].label);
			p->cur->nbits	   = 0;
			p->bits_from_field = false;
		}
		char *dup = xstrdup(text);
		add_bit(p->cur, (int)p->code, str_trim(dup));
		free(dup);
	} else {
		return;
	}
	p->collecting = T_NONE;
}

bool meta_load_apm_xml(meta_db_t *db, const char *path, char *err, size_t errlen) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return false;
	}
	XML_Parser xp  = XML_ParserCreate(NULL);
	apm_parser_t p = {.db = db};
	XML_SetUserData(xp, &p);
	XML_SetElementHandler(xp, apm_start, apm_end);
	XML_SetCharacterDataHandler(xp, apm_chars);
	bool ok = true;
	char buf[65536];
	for (;;) {
		size_t n  = fread(buf, 1, sizeof buf, f);
		bool last = n < sizeof buf;
		if (XML_Parse(xp, buf, (int)n, last) == XML_STATUS_ERROR) {
#if XML_MAJOR_VERSION > 2 || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
			unsigned long line = (unsigned long)XML_GetCurrentLineNumber64(xp);
#else
			unsigned long line = (unsigned long)XML_GetCurrentLineNumber(xp);
#endif
			snprintf(err, errlen, "%s:%lu: %s", path, line, XML_ErrorString(XML_GetErrorCode(xp)));
			ok = false;
			break;
		}
		if (last)
			break;
	}
	XML_ParserFree(xp);
	meta_free_one(p.cur);
	free(p.text);
	fclose(f);
	if (ok && db->n == 0) {
		snprintf(err, errlen, "%s: no parameters found", path);
		ok = false;
	}
	return ok;
}

/* ------------------------------------------------------------------------ */
/* PX4 parameter JSON (as used by QGC / component metadata)                   */

static char *read_file(const char *path, size_t *len, char *err, size_t errlen) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return NULL;
	}
	size_t cap = 1 << 20, n = 0;
	char *buf = xmalloc(cap);
	for (;;) {
		if (n + 65536 > cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		size_t r = fread(buf + n, 1, cap - n - 1, f);
		n += r;
		if (r == 0)
			break;
	}
	fclose(f);
	buf[n] = '\0';
	*len   = n;
	return buf;
}

static const char *jstr(const cJSON *o, const char *k) {
	const cJSON *j = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsString(j) ? j->valuestring : NULL;
}

static bool jnum(const cJSON *o, const char *k, double *out) {
	const cJSON *j = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsNumber(j)) {
		*out = j->valuedouble;
		return true;
	}
	if (cJSON_IsString(j)) { /* some generators emit numbers as strings */
		char *end;
		double v = strtod(j->valuestring, &end);
		if (end != j->valuestring) {
			*out = v;
			return true;
		}
	}
	return false;
}

bool meta_load_px4_json(meta_db_t *db, const char *path, char *err, size_t errlen) {
	size_t len;
	char *txt = read_file(path, &len, err, errlen);
	if (!txt)
		return false;
	cJSON *root = cJSON_ParseWithLength(txt, len);
	free(txt);
	if (!root) {
		snprintf(err, errlen, "%s: invalid JSON", path);
		return false;
	}
	const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "parameters");
	const cJSON *it;
	cJSON_ArrayForEach(it, arr) {
		const char *name = jstr(it, "name");
		if (!name)
			continue;
		param_meta_t *m = xcalloc(1, sizeof *m);
		m->decimals		= -1;
		m->name			= xstrdup(name);
		const char *sd = jstr(it, "shortDesc"), *ld = jstr(it, "longDesc");
		m->human = xstrdup(sd);
		if (sd && ld && *ld) {
			size_t n  = strlen(sd) + strlen(ld) + 4;
			m->desc	  = xmalloc(n);
			size_t sl = strlen(sd);
			bool dot  = sl && (sd[sl - 1] == '.' || sd[sl - 1] == ':');
			snprintf(m->desc, n, "%s%s %s", sd, dot ? "" : ".", ld);
		} else {
			m->desc = xstrdup(sd ? sd : ld);
		}
		m->units = xstrdup(jstr(it, "units"));
		m->user	 = xstrdup(jstr(it, "category"));
		double lo, hi, v;
		bool hl = jnum(it, "min", &lo), hh = jnum(it, "max", &hi);
		if (hl || hh) {
			m->has_range = true;
			m->min		 = hl ? lo : -INFINITY;
			m->max		 = hh ? hi : INFINITY;
			if (!hl || !hh)
				m->has_range = hl && hh; /* only show closed ranges */
		}
		if (jnum(it, "increment", &v)) {
			m->has_increment = true;
			m->increment	 = v;
		}
		if (jnum(it, "default", &v)) {
			m->has_default = true;
			m->def		   = v;
		}
		if (jnum(it, "decimalPlaces", &v))
			m->decimals = (int)v;
		m->reboot_required = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "rebootRequired"));
		const cJSON *vals  = cJSON_GetObjectItemCaseSensitive(it, "values"), *e;
		cJSON_ArrayForEach(e, vals) {
			double code;
			const char *d = jstr(e, "description");
			if (jnum(e, "value", &code))
				add_value(m, code, d ? d : "");
		}
		const cJSON *bits = cJSON_GetObjectItemCaseSensitive(it, "bitmask");
		cJSON_ArrayForEach(e, bits) {
			double idx;
			const char *d = jstr(e, "description");
			if (jnum(e, "index", &idx))
				add_bit(m, (int)idx, d ? d : "");
		}
		finalize(m);
		meta_insert(db, m);
	}
	cJSON_Delete(root);
	if (db->n == 0) {
		snprintf(err, errlen, "%s: no parameters found", path);
		return false;
	}
	return true;
}

bool meta_load_file(meta_db_t *db, const char *path, char *err, size_t errlen) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return false;
	}
	int c;
	while ((c = fgetc(f)) != EOF && isspace(c))
		;
	fclose(f);
	if (c == '{')
		return meta_load_px4_json(db, path, err, errlen);
	if (c == '<')
		return meta_load_apm_xml(db, path, err, errlen);
	snprintf(err, errlen, "%s: unknown metadata format", path);
	return false;
}
