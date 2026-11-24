#include "detect.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "i18n.h"
#include "util.h"

/* ------------------------------------------------------------------------ */
/* serial port enumeration (Linux sysfs)                                      */

static bool read_line_file(const char *path, char *out, size_t n) {
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	bool ok = fgets(out, (int)n, f) != NULL;
	fclose(f);
	if (ok) {
		char *t = str_trim(out);
		memmove(out, t, strlen(t) + 1);
	}
	return ok;
}

static int cmp_port(const void *a, const void *b) {
	return strcmp(((const serial_port_info_t *)a)->dev, ((const serial_port_info_t *)b)->dev);
}

int serial_enumerate(serial_port_info_t *out, int max) {
	DIR *d = opendir("/sys/class/tty");
	if (!d)
		return 0;
	int n = 0;
	struct dirent *e;
	while ((e = readdir(d)) && n < max) {
		if (strncmp(e->d_name, "ttyACM", 6) && strncmp(e->d_name, "ttyUSB", 6))
			continue;
		char link[PATH_MAX], real[PATH_MAX];
		snprintf(link, sizeof link, "/sys/class/tty/%s/device", e->d_name);
		if (!realpath(link, real))
			continue;
		serial_port_info_t *p = &out[n];
		memset(p, 0, sizeof *p);
		snprintf(p->dev, sizeof p->dev, "/dev/%s", e->d_name);
		/* walk up from the interface to the USB device that has idVendor */
		for (int up = 0; up < 4; up++) {
			char f[PATH_MAX + 32], buf[64];
			snprintf(f, sizeof f, "%s/idVendor", real);
			if (read_line_file(f, buf, sizeof buf)) {
				p->usb = true;
				p->vid = (uint16_t)strtoul(buf, NULL, 16);
				snprintf(f, sizeof f, "%s/idProduct", real);
				if (read_line_file(f, buf, sizeof buf))
					p->pid = (uint16_t)strtoul(buf, NULL, 16);
				snprintf(f, sizeof f, "%s/manufacturer", real);
				read_line_file(f, p->manufacturer, sizeof p->manufacturer);
				snprintf(f, sizeof f, "%s/product", real);
				read_line_file(f, p->product, sizeof p->product);
				break;
			}
			char *slash = strrchr(real, '/');
			if (!slash || slash == real)
				break;
			*slash = '\0';
		}
		n++;
	}
	closedir(d);
	qsort(out, (size_t)n, sizeof *out, cmp_port);
	return n;
}

static bool has_word_bl(const char *s) {
	if (str_icontains(s, "bootloader"))
		return true;
	for (const char *p = s; (p = strstr(p, "BL")); p += 2) {
		bool left  = p == s || !isalnum((unsigned char)p[-1]);
		bool right = !isalnum((unsigned char)p[2]);
		if (left && right)
			return true;
	}
	return false;
}

cand_kind_t serial_classify(const serial_port_info_t *p, int *baud, bool *bootloader, char *what, size_t n) {
	static const struct {
		uint16_t vid, pid; /* pid 0 = any */
		cand_kind_t kind;
		const char *name;
	} ids[] = {
			{0x26AC, 0,		 CAND_SERIAL_FC,	"3DR/PX4"			},
			{0x2DAE, 0,		 CAND_SERIAL_FC,	"CubePilot"			},
			{0x3162, 0,		 CAND_SERIAL_FC,	"Holybro"			},
			{0x3163, 0,		 CAND_SERIAL_FC,	"CUAV"				},
			{0x35A7, 0,		 CAND_SERIAL_FC,	"ARK"				},
			{0x1FC9, 0,		 CAND_SERIAL_FC,	"NXP"				},
			{0x1209, 0x5740, CAND_SERIAL_FC,	"ArduPilot"			},
			{0x1209, 0x5741, CAND_SERIAL_FC,	"ArduPilot"			},
			{0x0483, 0x5740, CAND_SERIAL_FC,	"STM32 VCP"			},
			{0x0403, 0x6015, CAND_SERIAL_RADIO, "SiK radio (FTDI)"	},
			{0x10C4, 0xEA60, CAND_SERIAL_RADIO, "SiK radio (CP210x)"},
	};
	*bootloader		   = has_word_bl(p->product) || has_word_bl(p->manufacturer);
	*baud			   = 115200;
	cand_kind_t kind   = CAND_SERIAL_OTHER;
	const char *vendor = NULL;
	for (size_t i = 0; i < ARRAY_LEN(ids); i++) {
		if (p->vid == ids[i].vid && (!ids[i].pid || p->pid == ids[i].pid)) {
			kind   = ids[i].kind;
			vendor = ids[i].name;
			break;
		}
	}
	if (kind == CAND_SERIAL_OTHER) {
		static const char *fc_words[] = {"PX4", "ArduPilot", "Pixhawk", "FMU", "Cube"};
		for (size_t i = 0; i < ARRAY_LEN(fc_words); i++)
			if (str_icontains(p->product, fc_words[i]) || str_icontains(p->manufacturer, fc_words[i]))
				kind = CAND_SERIAL_FC;
	}
	if (kind == CAND_SERIAL_RADIO)
		*baud = 57600;

	char ids_s[16] = "";
	if (p->usb)
		snprintf(ids_s, sizeof ids_s, "%04x:%04x", p->vid, p->pid);
	const char *mf = p->manufacturer[0] ? p->manufacturer : (vendor ? vendor : "");
	snprintf(what, n, "%s%s%s%s%s%s", mf, *mf && p->product[0] ? " " : "", p->product, ids_s[0] ? " [" : "", ids_s, ids_s[0] ? "]" : "");
	return kind;
}

bool heartbeat_is_vehicle(const mavlink_message_t *msg, uint8_t gcs_sysid) {
	if (msg->msgid != MAVLINK_MSG_ID_HEARTBEAT || msg->sysid == gcs_sysid)
		return false;
	mavlink_heartbeat_t hb;
	mavlink_msg_heartbeat_decode(msg, &hb);
	if (hb.autopilot == MAV_AUTOPILOT_INVALID)
		return false;
	switch (hb.type) {
	case MAV_TYPE_GCS:
	case MAV_TYPE_GIMBAL:
	case MAV_TYPE_ADSB:
	case MAV_TYPE_CAMERA:
	case MAV_TYPE_ONBOARD_CONTROLLER:
		return false;
	default:
		return true;
	}
}

/* ------------------------------------------------------------------------ */
/* candidates                                                                 */

static detect_cand_t *cand_add(detector_t *d, cand_kind_t kind, const link_spec_t *spec, const char *label, double now) {
	for (int i = 0; i < DETECT_MAX; i++) {
		detect_cand_t *c = &d->c[i];
		if (c->used)
			continue;
		memset(c, 0, sizeof *c);
		link_init(&c->link);
		c->used		  = true;
		c->kind		  = kind;
		c->spec		  = *spec;
		c->present	  = true;
		c->first_seen = now;
		c->next_try	  = now;
		xstrlcpy(c->label, label, sizeof c->label);
		xstrlcpy(c->status, _("waiting"), sizeof c->status);
		return c;
	}
	return NULL;
}

static void cand_remove(detect_cand_t *c) {
	link_close(&c->link);
	c->used = false;
}

void detector_init(detector_t *d, const detect_opts_t *opt) {
	memset(d, 0, sizeof *d);
	d->opt	   = *opt;
	d->winner  = -1;
	double now = now_s();
	d->started = now;
	if (opt->forced) {
		char desc[300];
		link_spec_describe(&opt->forced_spec, desc, sizeof desc);
		cand_add(d, CAND_FORCED, &opt->forced_spec, desc, now);
		return;
	}
	if (opt->udp) {
		link_spec_t s = {.kind = LINK_UDP_IN, .port = opt->udp_port};
		xstrlcpy(s.host, "0.0.0.0", sizeof s.host);
		char label[64];
		snprintf(label, sizeof label, _("UDP listen :%d"), opt->udp_port);
		cand_add(d, CAND_UDP, &s, label, now);
	}
	if (opt->tcp) {
		link_spec_t s = {.kind = LINK_TCP, .port = opt->tcp_port};
		xstrlcpy(s.host, "127.0.0.1", sizeof s.host);
		char label[64];
		snprintf(label, sizeof label, "TCP 127.0.0.1:%d (SITL)", opt->tcp_port);
		cand_add(d, CAND_TCP, &s, label, now);
	}
}

void detector_shutdown(detector_t *d) {
	for (int i = 0; i < DETECT_MAX; i++)
		if (d->c[i].used)
			cand_remove(&d->c[i]);
}

static void scan_serial(detector_t *d, double now) {
	serial_port_info_t ports[DETECT_MAX];
	int n = serial_enumerate(ports, DETECT_MAX);
	for (int i = 0; i < DETECT_MAX; i++)
		if (d->c[i].used && d->c[i].spec.kind == LINK_SERIAL)
			d->c[i].present = false;

	for (int k = 0; k < n; k++) {
		int baud;
		bool bl;
		char what[160];
		cand_kind_t kind = serial_classify(&ports[k], &baud, &bl, what, sizeof what);
		bool acm		 = strncmp(ports[k].dev, "/dev/ttyACM", 11) == 0;
		if (kind == CAND_SERIAL_OTHER && !acm)
			continue; /* random USB-UART: don't poke it (QGC doesn't either) */
		if (ports[k].vid == 0x1546)
			continue; /* u-blox GPS */

		detect_cand_t *c = NULL;
		for (int i = 0; i < DETECT_MAX; i++)
			if (d->c[i].used && d->c[i].spec.kind == LINK_SERIAL && strcmp(d->c[i].spec.host, ports[k].dev) == 0)
				c = &d->c[i];
		if (!c) {
			link_spec_t s = {.kind = LINK_SERIAL, .baud = baud};
			xstrlcpy(s.host, ports[k].dev, sizeof s.host);
			char label[160];
			snprintf(label, sizeof label, "%s  %s", ports[k].dev, what);
			c = cand_add(d, kind, &s, label, now);
			if (!c)
				continue;
			/* Give a freshly plugged board time to leave its bootloader. */
			c->next_try = now + 1.0;
			LOG("detect: new serial candidate %s", label);
		}
		c->present = true;
		if (bl != c->skip) {
			c->skip = bl;
			if (bl) {
				link_close(&c->link);
				xstrlcpy(c->status, _("bootloader - waiting for firmware to start"), sizeof c->status);
			} else {
				c->next_try = now + 1.0;
			}
		}
	}
	for (int i = 0; i < DETECT_MAX; i++)
		if (d->c[i].used && d->c[i].spec.kind == LINK_SERIAL && !d->c[i].present && d->c[i].kind != CAND_FORCED) {
			LOG("detect: serial candidate %s disappeared", d->c[i].spec.host);
			cand_remove(&d->c[i]);
		}
}

static void permission_hint(const char *dev, char *out, size_t n) {
	struct stat st;
	const char *grp = "?";
	if (stat(dev, &st) == 0) {
		struct group *g = getgrgid(st.st_gid);
		if (g)
			grp = g->gr_name;
	}
	snprintf(out, n, _("permission denied - add yourself to group '%s'"), grp);
}

static void try_open(detector_t *d, detect_cand_t *c, double now) {
	if (link_open(&c->link, &c->spec)) {
		c->opened_at = now;
		c->msgs_seen = 0;
		if (c->link.connecting)
			xstrlcpy(c->status, _("connecting..."), sizeof c->status);
		else if (c->spec.kind == LINK_UDP_IN)
			xstrlcpy(c->status, _("listening, waiting for heartbeat"), sizeof c->status);
		else
			snprintf(c->status, sizeof c->status, _("open @%d, waiting for heartbeat"), c->spec.baud);
		LOG("detect: opened %s", c->link.name);
		return;
	}
	if (c->spec.kind == LINK_SERIAL && strstr(c->link.err, "Permission denied"))
		permission_hint(c->spec.host, c->status, sizeof c->status);
	else
		xstrlcpy(c->status, c->link.err, sizeof c->status);
	c->next_try = now + (c->spec.kind == LINK_UDP_IN ? 5.0 : 2.0);
	LOG("detect: open %s failed: %s", c->label, c->link.err);
}

static void on_msg(void *ctx, link_t *l, const mavlink_message_t *msg) {
	detector_t *d = ctx;
	int idx		  = -1;
	for (int i = 0; i < DETECT_MAX; i++)
		if (&d->c[i].link == l)
			idx = i;
	if (idx < 0)
		return;
	detect_cand_t *c = &d->c[idx];
	c->msgs_seen++;
	if (d->winner >= 0)
		return;
	if (!heartbeat_is_vehicle(msg, d->opt.gcs_sysid)) {
		if (c->msgs_seen == 1)
			xstrlcpy(c->status, _("MAVLink traffic, no autopilot heartbeat yet"), sizeof c->status);
		return;
	}
	if (d->opt.target_sysid >= 0 && msg->sysid != (uint32_t)d->opt.target_sysid)
		return;
	d->winner	 = idx;
	d->winner_hb = *msg;
	snprintf(c->status, sizeof c->status, _("vehicle found (sysid %u)"), msg->sysid);
	LOG("detect: vehicle heartbeat on %s sysid=%u compid=%u", l->name, msg->sysid, msg->compid);
}

int detector_pollfds(detector_t *d, struct pollfd *pfd, int *idx, int max) {
	int n = 0;
	for (int i = 0; i < DETECT_MAX && n < max; i++) {
		detect_cand_t *c = &d->c[i];
		if (!c->used || !link_is_open(&c->link))
			continue;
		pfd[n].fd	   = c->link.fd;
		pfd[n].events  = link_poll_events(&c->link);
		pfd[n].revents = 0;
		idx[n]		   = i;
		n++;
	}
	return n;
}

void detector_service(detector_t *d, int cand, short revents) {
	detect_cand_t *c = &d->c[cand];
	if (!c->used || !link_is_open(&c->link))
		return;
	bool was_connecting = c->link.connecting;
	if (!link_service(&c->link, revents, on_msg, d)) {
		xstrlcpy(c->status, c->link.err, sizeof c->status);
		c->next_try = now_s() + 2.0;
		LOG("detect: %s: %s", c->label, c->link.err);
		return;
	}
	if (was_connecting && !c->link.connecting)
		xstrlcpy(c->status, _("connected, waiting for heartbeat"), sizeof c->status);
}

static void send_heartbeat(detector_t *d, link_t *l) {
	mavlink_message_t msg;
	mavlink_heartbeat_t hb = {
			.type		   = MAV_TYPE_GCS,
			.autopilot	   = MAV_AUTOPILOT_INVALID,
			.system_status = MAV_STATE_ACTIVE,
	};
	mavlink_msg_heartbeat_encode_status(d->opt.gcs_sysid, d->opt.gcs_compid, &l->tx_status, &msg, &hb);
	link_send(l, &msg);
}

void detector_tick(detector_t *d, double now) {
	if (d->winner >= 0)
		return;
	if (!d->opt.forced && d->opt.serial && now >= d->next_scan) {
		scan_serial(d, now);
		d->next_scan = now + 1.0;
	}
	for (int i = 0; i < DETECT_MAX; i++) {
		detect_cand_t *c = &d->c[i];
		if (!c->used)
			continue;
		if (!link_is_open(&c->link)) {
			if (!c->skip && now >= c->next_try)
				try_open(d, c, now);
			continue;
		}
		/* Serial radios / unknown ports: alternate the two common bauds when
		 * nothing parses (USB CDC ignores baud, so FCs are unaffected). */
		if (c->spec.kind == LINK_SERIAL && c->kind != CAND_SERIAL_FC && c->kind != CAND_FORCED && c->link.rx_msgs == 0 &&
				now - c->opened_at > 4.0) {
			c->spec.baud = c->spec.baud == 57600 ? 115200 : 57600;
			link_close(&c->link);
			c->next_try = now;
		}
	}
	if (now >= d->next_hb) {
		d->next_hb = now + 1.0;
		for (int i = 0; i < DETECT_MAX; i++)
			if (d->c[i].used && link_is_open(&d->c[i].link))
				send_heartbeat(d, &d->c[i].link);
	}
}

bool detector_take(detector_t *d, link_t *out, mavlink_message_t *hb) {
	if (d->winner < 0)
		return false;
	detect_cand_t *w = &d->c[d->winner];
	*out			 = w->link; /* struct copy moves fd + parser state */
	link_init(&w->link);
	if (hb)
		*hb = d->winner_hb;
	for (int i = 0; i < DETECT_MAX; i++)
		if (d->c[i].used)
			cand_remove(&d->c[i]);
	d->winner = -1;
	return true;
}
