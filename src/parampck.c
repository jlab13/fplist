#include "parampck.h"

#include <stdio.h>
#include <string.h>

#include "mav.h"

enum { AP_PARAM_INT8 = 1, AP_PARAM_INT16 = 2, AP_PARAM_INT32 = 3, AP_PARAM_FLOAT = 4 };

static bool read_value(const uint8_t *buf, size_t len, size_t *pos, uint8_t type, double *out) {
	size_t n = type == AP_PARAM_INT8 ? 1 : type == AP_PARAM_INT16 ? 2 : 4;
	if (*pos + n > len)
		return false;
	const uint8_t *p = buf + *pos;
	switch (type) {
	case AP_PARAM_INT8:
		*out = (int8_t)p[0];
		break;
	case AP_PARAM_INT16:
		*out = (int16_t)(uint16_t)(p[0] | p[1] << 8);
		break;
	case AP_PARAM_INT32:
		*out = (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
		break;
	default: {
		uint32_t u = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
		float f;
		memcpy(&f, &u, sizeof f);
		*out = f;
		break;
	}
	}
	*pos += n;
	return true;
}

static uint8_t mav_type(uint8_t t) {
	switch (t) {
	case AP_PARAM_INT8:
		return MAV_PARAM_TYPE_INT8;
	case AP_PARAM_INT16:
		return MAV_PARAM_TYPE_INT16;
	case AP_PARAM_INT32:
		return MAV_PARAM_TYPE_INT32;
	default:
		return MAV_PARAM_TYPE_REAL32;
	}
}

int parampck_parse(const uint8_t *buf, size_t len, parampck_cb cb, void *ctx, char *err, size_t errlen) {
	if (len < 6) {
		snprintf(err, errlen, "param.pck: short header");
		return -1;
	}
	uint16_t magic = (uint16_t)(buf[0] | buf[1] << 8);
	uint16_t num   = (uint16_t)(buf[2] | buf[3] << 8);
	uint16_t total = (uint16_t)(buf[4] | buf[5] << 8);
	if (magic != 0x671B && magic != 0x671C) {
		snprintf(err, errlen, "param.pck: bad magic 0x%04x", magic);
		return -1;
	}
	if (num != total) {
		snprintf(err, errlen, "param.pck: %u of %u parameters", num, total);
		return -1;
	}
	char name[17] = "";
	size_t pos	  = 6;
	int count	  = 0;
	while (count < num) {
		while (pos < len && buf[pos] == 0) /* padding */
			pos++;
		if (pos + 2 > len) {
			snprintf(err, errlen, "param.pck: truncated after %d of %u parameters", count, num);
			return -1;
		}
		uint8_t type	 = buf[pos] & 0x0F;
		bool has_default = (buf[pos] >> 4) & 0x01;
		uint8_t nl		 = (uint8_t)(((buf[pos + 1] >> 4) & 0x0F) + 1);
		uint8_t common	 = buf[pos + 1] & 0x0F;
		pos += 2;
		if (common + nl > 16 || common > strlen(name) || pos + nl > len || type < AP_PARAM_INT8 || type > AP_PARAM_FLOAT) {
			snprintf(err, errlen, "param.pck: corrupt entry %d", count);
			return -1;
		}
		memcpy(name + common, buf + pos, nl);
		name[common + nl] = '\0';
		pos += nl;
		double v, d = 0;
		if (!read_value(buf, len, &pos, type, &v) || (has_default && !read_value(buf, len, &pos, type, &d))) {
			snprintf(err, errlen, "param.pck: truncated value of %s", name);
			return -1;
		}
		if (cb)
			cb(ctx, name, mav_type(type), v, has_default, d);
		count++;
	}
	return count;
}
