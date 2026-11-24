#include "params.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mav.h"
#include "util.h"

static uint32_t hash_name(const char *s) {
	uint32_t h = 2166136261u;
	for (; *s; s++) {
		h ^= (uint8_t)*s;
		h *= 16777619u;
	}
	return h;
}

static void rehash(paramset_t *ps, int cap) {
	free(ps->htab);
	ps->hcap = cap;
	ps->htab = xcalloc((size_t)cap, sizeof(int));
	for (int i = 0; i < ps->n; i++) {
		uint32_t h = hash_name(ps->v[i].name) & (uint32_t)(cap - 1);
		while (ps->htab[h])
			h = (h + 1) & (uint32_t)(cap - 1);
		ps->htab[h] = i + 1;
	}
}

void ps_init(paramset_t *ps) {
	memset(ps, 0, sizeof *ps);
	rehash(ps, 256);
}

void ps_free(paramset_t *ps) {
	free(ps->v);
	free(ps->htab);
	free(ps->got);
	memset(ps, 0, sizeof *ps);
}

void ps_clear(paramset_t *ps) {
	unsigned gen = ps->generation, rev = ps->revision;
	ps_free(ps);
	ps_init(ps);
	ps->generation = gen + 1;
	ps->revision   = rev + 1;
}

void ps_reset_download(paramset_t *ps) {
	free(ps->got);
	ps->got		  = NULL;
	ps->expected  = 0;
	ps->got_count = 0;
	ps->revision++;
}

void ps_set_expected(paramset_t *ps, int count) {
	if (count <= 0 || count == ps->expected)
		return;
	uint8_t *g = xcalloc((size_t)count, 1);
	int got	   = 0;
	if (ps->got) {
		int keep = MIN(count, ps->expected);
		memcpy(g, ps->got, (size_t)keep);
		for (int i = 0; i < keep; i++)
			got += g[i];
	}
	free(ps->got);
	ps->got		  = g;
	ps->expected  = count;
	ps->got_count = got;
	ps->revision++;
}

bool ps_mark_index(paramset_t *ps, int index) {
	if (index < 0 || index >= ps->expected || ps->got[index])
		return false;
	ps->got[index] = 1;
	ps->got_count++;
	return true;
}

int ps_find(const paramset_t *ps, const char *name) {
	uint32_t mask = (uint32_t)(ps->hcap - 1);
	for (uint32_t h = hash_name(name) & mask;; h = (h + 1) & mask) {
		int e = ps->htab[h];
		if (!e)
			return -1;
		if (strcmp(ps->v[e - 1].name, name) == 0)
			return e - 1;
	}
}

int ps_upsert(paramset_t *ps, const char *name, bool *created) {
	int i = ps_find(ps, name);
	if (created)
		*created = i < 0;
	if (i >= 0)
		return i;
	if (ps->n == ps->cap) {
		ps->cap = ps->cap ? ps->cap * 2 : 512;
		ps->v	= xrealloc(ps->v, (size_t)ps->cap * sizeof *ps->v);
	}
	param_t *p = &ps->v[ps->n];
	memset(p, 0, sizeof *p);
	xstrlcpy(p->name, name, sizeof p->name);
	p->index = -1;
	p->type	 = MAV_PARAM_TYPE_REAL32;
	ps->n++;
	if (ps->n * 2 > ps->hcap) {
		rehash(ps, ps->hcap * 2);
	} else {
		uint32_t mask = (uint32_t)(ps->hcap - 1);
		uint32_t h	  = hash_name(p->name) & mask;
		while (ps->htab[h])
			h = (h + 1) & mask;
		ps->htab[h] = ps->n;
	}
	ps->generation++;
	ps->revision++;
	return ps->n - 1;
}

int ps_pending_count(const paramset_t *ps) {
	int c = 0;
	for (int i = 0; i < ps->n; i++)
		c += ps->v[i].pending;
	return c;
}

void ps_attach_meta(paramset_t *ps, const meta_db_t *db) {
	for (int i = 0; i < ps->n; i++)
		ps->v[i].meta = db ? meta_find(db, ps->v[i].name) : NULL;
	ps->generation++;
	ps->revision++;
}

bool param_type_is_int(uint8_t type) { return type != MAV_PARAM_TYPE_REAL32 && type != MAV_PARAM_TYPE_REAL64; }

const char *param_type_name(uint8_t type) {
	switch (type) {
	case MAV_PARAM_TYPE_UINT8:
		return "uint8";
	case MAV_PARAM_TYPE_INT8:
		return "int8";
	case MAV_PARAM_TYPE_UINT16:
		return "uint16";
	case MAV_PARAM_TYPE_INT16:
		return "int16";
	case MAV_PARAM_TYPE_UINT32:
		return "uint32";
	case MAV_PARAM_TYPE_INT32:
		return "int32";
	case MAV_PARAM_TYPE_UINT64:
		return "uint64";
	case MAV_PARAM_TYPE_INT64:
		return "int64";
	case MAV_PARAM_TYPE_REAL32:
		return "float";
	case MAV_PARAM_TYPE_REAL64:
		return "double";
	default:
		return "?";
	}
}

void param_type_limits(uint8_t type, double *lo, double *hi) {
	switch (type) {
	case MAV_PARAM_TYPE_UINT8:
		*lo = 0;
		*hi = UINT8_MAX;
		break;
	case MAV_PARAM_TYPE_INT8:
		*lo = INT8_MIN;
		*hi = INT8_MAX;
		break;
	case MAV_PARAM_TYPE_UINT16:
		*lo = 0;
		*hi = UINT16_MAX;
		break;
	case MAV_PARAM_TYPE_INT16:
		*lo = INT16_MIN;
		*hi = INT16_MAX;
		break;
	case MAV_PARAM_TYPE_UINT32:
		*lo = 0;
		*hi = UINT32_MAX;
		break;
	case MAV_PARAM_TYPE_INT32:
		*lo = INT32_MIN;
		*hi = INT32_MAX;
		break;
	default:
		*lo = -3.402823466e38;
		*hi = 3.402823466e38;
		break;
	}
}

void param_format(const param_t *p, double v, char *out, int n) { fmt_number(v, param_type_is_int(p->type), out, (size_t)n); }

typedef union {
	float f;
	int32_t i32;
	uint32_t u32;
	int16_t i16;
	uint16_t u16;
	int8_t i8;
	uint8_t u8;
} pun_t;

double param_decode(param_enc_t enc, uint8_t type, float raw) {
	if (enc == PENC_CCAST) {
		if (param_type_is_int(type))
			return rint((double)raw);
		return (double)raw;
	}
	pun_t u;
	u.f = raw;
	switch (type) {
	case MAV_PARAM_TYPE_UINT8:
		return u.u8;
	case MAV_PARAM_TYPE_INT8:
		return u.i8;
	case MAV_PARAM_TYPE_UINT16:
		return u.u16;
	case MAV_PARAM_TYPE_INT16:
		return u.i16;
	case MAV_PARAM_TYPE_UINT32:
		return u.u32;
	case MAV_PARAM_TYPE_INT32:
		return u.i32;
	default:
		return (double)u.f;
	}
}

float param_encode(param_enc_t enc, uint8_t type, double v) {
	if (enc == PENC_CCAST)
		return (float)v;
	pun_t u;
	memset(&u, 0, sizeof u);
	switch (type) {
	case MAV_PARAM_TYPE_UINT8:
		u.u8 = (uint8_t)llround(v);
		break;
	case MAV_PARAM_TYPE_INT8:
		u.i8 = (int8_t)llround(v);
		break;
	case MAV_PARAM_TYPE_UINT16:
		u.u16 = (uint16_t)llround(v);
		break;
	case MAV_PARAM_TYPE_INT16:
		u.i16 = (int16_t)llround(v);
		break;
	case MAV_PARAM_TYPE_UINT32:
		u.u32 = (uint32_t)llround(v);
		break;
	case MAV_PARAM_TYPE_INT32:
		u.i32 = (int32_t)llround(v);
		break;
	default:
		u.f = (float)v;
		break;
	}
	return u.f;
}
