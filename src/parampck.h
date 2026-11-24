/* ArduPilot packed parameter file (@PARAM/param.pck, AP_Filesystem_Param):
 *   header: uint16 magic (0x671B, 0x671C = with defaults), num_params,
 *           total_params (little endian)
 *   entries (zero bytes between them are padding):
 *     byte  type:4 (low) | flags:4 (bit0 = has default)
 *     byte  common_len:4 (low) | (name_len - 1):4
 *     name  name_len bytes, appended to the first common_len bytes of the
 *           previous name
 *     value int8/int16/int32/float (LE), then the default when flagged */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*parampck_cb)(void *ctx, const char *name, uint8_t mav_type, double value, bool has_default, double default_value);

/* Returns the number of parameters, or -1 with err filled. */
int parampck_parse(const uint8_t *buf, size_t len, parampck_cb cb, void *ctx, char *err, size_t errlen);
