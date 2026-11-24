#include "vehicle.h"

#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "i18n.h"
#include "util.h"

#define HB_PERIOD 1.0
#define HB_TIMEOUT 3.5
#define LIST_TIMEOUT 3.0 /* no PARAM_VALUE at all after REQUEST_LIST */
#define LIST_TRIES 4
#define GAP_TIMEOUT 0.6 /* silence before asking for missing indices */
#define GAP_BATCH 32
#define GAP_MAX_STALLS 12 /* gap rounds without any progress */
#define WRITE_TIMEOUT 1.2
#define WRITE_TRIES 4
#define WRITE_IN_FLIGHT 8

static void vmsg(vehicle_t *v, int level, const char *fmt, ...) PRINTF_LIKE(3, 4);
static void vmsg(vehicle_t *v, int level, const char *fmt, ...) {
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	LOG("vehicle: %s", buf);
	if (v->on_msg)
		v->on_msg(v->on_msg_ctx, level, buf);
}

/* ------------------------------------------------------------------------ */
/* sending                                                                    */

static void send_heartbeat(vehicle_t *v) {
	mavlink_message_t msg;
	mavlink_heartbeat_t hb = {
			.type		   = MAV_TYPE_GCS,
			.autopilot	   = MAV_AUTOPILOT_INVALID,
			.system_status = MAV_STATE_ACTIVE,
	};
	mavlink_msg_heartbeat_encode_status(v->gcs_sysid, v->gcs_compid, &v->link.tx_status, &msg, &hb);
	link_send(&v->link, &msg);
}

static void send_command(vehicle_t *v, uint16_t cmd, float p1) {
	mavlink_message_t msg;
	mavlink_command_long_t c = {
			.target_system	  = v->sysid,
			.target_component = v->compid,
			.command		  = cmd,
			.param1			  = p1,
	};
	mavlink_msg_command_long_encode_status(v->gcs_sysid, v->gcs_compid, &v->link.tx_status, &msg, &c);
	link_send(&v->link, &msg);
}

static void send_request_list(vehicle_t *v) {
	mavlink_message_t msg;
	mavlink_param_request_list_t r = {.target_system = v->sysid, .target_component = v->compid};
	mavlink_msg_param_request_list_encode_status(v->gcs_sysid, v->gcs_compid, &v->link.tx_status, &msg, &r);
	link_send(&v->link, &msg);
	LOG("vehicle: PARAM_REQUEST_LIST (try %d)", v->pdl_list_tries);
}

static void send_request_read(vehicle_t *v, int index) {
	mavlink_message_t msg;
	mavlink_param_request_read_t r = {
			.target_system	  = v->sysid,
			.target_component = v->compid,
			.param_index	  = (int16_t)index,
	};
	mavlink_msg_param_request_read_encode_status(v->gcs_sysid, v->gcs_compid, &v->link.tx_status, &msg, &r);
	link_send(&v->link, &msg);
}

static void send_param_set(vehicle_t *v, param_t *p) {
	mavlink_message_t msg;
	mavlink_param_set_t s = {
			.param_value	  = param_encode(v->enc, p->type, p->pending_value),
			.target_system	  = v->sysid,
			.target_component = v->compid,
			.param_type		  = p->type,
	};
	memcpy(s.param_id, p->name, strnlen(p->name, sizeof s.param_id));
	mavlink_msg_param_set_encode_status(v->gcs_sysid, v->gcs_compid, &v->link.tx_status, &msg, &s);
	link_send(&v->link, &msg);
	p->wstate	= PW_SENT;
	p->wsent_at = now_s();
	p->wtries++;
	LOG("vehicle: PARAM_SET %s = %.9g (try %d)", p->name, p->pending_value, p->wtries);
}

/* ------------------------------------------------------------------------ */
/* receiving                                                                  */

static bool values_equal(uint8_t type, double a, double b) {
	if (param_type_is_int(type))
		return llround(a) == llround(b);
	return (float)a == (float)b || fabs(a - b) <= 1e-7 * MAX(fabs(a), fabs(b));
}

static void redecode_all(vehicle_t *v) {
	for (int i = 0; i < v->ps->n; i++) {
		param_t *p = &v->ps->v[i];
		if (p->have_value)
			p->value = param_decode(v->enc, p->type, p->raw);
	}
	v->ps->revision++;
}

static void download_finished(vehicle_t *v, bool complete) {
	v->pdl			= complete ? PDL_DONE : PDL_FAILED;
	v->pdl_duration = now_s() - v->pdl_started;
	if (complete)
		vmsg(v, MSG_OK, ngettext("Downloaded %d parameter in %.1f s", "Downloaded %d parameters in %.1f s", v->ps->expected),
				v->ps->expected, v->pdl_duration);
	else
		vmsg(v, MSG_ERR, _("Parameter download incomplete: %d of %d received"), v->ps->got_count, v->ps->expected);
}

static void on_param_value(vehicle_t *v, const mavlink_message_t *msg) {
	mavlink_param_value_t pv;
	mavlink_msg_param_value_decode(msg, &pv);
	char name[PARAM_NAME_LEN + 1];
	memcpy(name, pv.param_id, PARAM_NAME_LEN);
	name[PARAM_NAME_LEN] = '\0';
	if (!name[0] || strcmp(name, "_HASH_CHECK") == 0)
		return;

	paramset_t *ps = v->ps;
	int i		   = ps_upsert(ps, name, NULL);
	param_t *p	   = &ps->v[i];
	p->type		   = pv.param_type;
	p->raw		   = pv.param_value;
	p->value	   = param_decode(v->enc, p->type, p->raw);
	p->have_value  = true;
	ps->revision++;

	if (pv.param_index != UINT16_MAX && pv.param_count > 0) {
		ps_set_expected(ps, pv.param_count);
		p->index = pv.param_index;
		ps_mark_index(ps, pv.param_index);
	}

	if (p->wstate == PW_SENT && values_equal(p->type, p->value, p->pending_value)) {
		p->wstate  = PW_NONE;
		p->pending = false;
		v->w_ok++;
		if (p->meta && p->meta->reboot_required)
			v->w_reboot_needed = true;
		LOG("vehicle: %s written OK", p->name);
	} else if (!p->pending && p->wstate == PW_FAILED) {
		p->wstate = PW_NONE;
	}

	if (v->pdl == PDL_LISTING) {
		v->pdl_last_rx = now_s();
		if (ps->expected > 0 && ps->got_count >= ps->expected)
			download_finished(v, true);
	}
}

static void on_autopilot_version(vehicle_t *v, const mavlink_message_t *msg) {
	mavlink_autopilot_version_t av;
	mavlink_msg_autopilot_version_decode(msg, &av);
	bool first			 = !v->have_version;
	v->have_version		 = true;
	v->capabilities		 = av.capabilities;
	v->flight_sw_version = av.flight_sw_version;
	param_enc_t enc		 = v->enc;
	if (av.capabilities & MAV_PROTOCOL_CAPABILITY_PARAM_ENCODE_C_CAST)
		enc = PENC_CCAST;
	else if (av.capabilities & MAV_PROTOCOL_CAPABILITY_PARAM_ENCODE_BYTEWISE)
		enc = PENC_BYTEWISE;
	if (enc != v->enc) {
		v->enc = enc;
		redecode_all(v);
		LOG("vehicle: param encoding switched to %s", enc == PENC_CCAST ? "c-cast" : "bytewise");
	}
	if (first) {
		char ver[64];
		vehicle_version_string(v, ver, sizeof ver);
		LOG("vehicle: AUTOPILOT_VERSION %s caps=0x%llx", ver, (unsigned long long)av.capabilities);
	}
}

static void on_message(void *ctx, link_t *l, const mavlink_message_t *msg) {
	vehicle_t *v = ctx;
	if (msg->sysid != v->sysid)
		return;

	switch (msg->msgid) {
	case MAVLINK_MSG_ID_HEARTBEAT: {
		if (msg->compid != v->compid)
			break;
		mavlink_heartbeat_t hb;
		mavlink_msg_heartbeat_decode(msg, &hb);
		bool was_alive	 = vehicle_alive(v, now_s());
		v->mav_type		 = hb.type;
		v->autopilot	 = hb.autopilot;
		v->base_mode	 = hb.base_mode;
		v->custom_mode	 = hb.custom_mode;
		v->system_status = hb.system_status;
		v->last_hb_rx	 = now_s();
		bool v1			 = msg->magic == MAVLINK_STX_MAVLINK1;
		if (v1 != v->mavlink1) {
			v->mavlink1 = v1;
			link_set_mavlink1(&v->link, v1);
		}
		v->hb_lost = false;
		if (v->reconnecting) {
			v->reconnecting = false;
			vmsg(v, MSG_OK, _("Reconnected via %s"), v->link.name);
		} else if (!was_alive) {
			vmsg(v, MSG_OK, "%s", _("Vehicle heartbeat restored"));
		}
		break;
	}
	case MAVLINK_MSG_ID_PARAM_VALUE:
		if (msg->compid == v->compid)
			on_param_value(v, msg);
		break;
	case MAVLINK_MSG_ID_AUTOPILOT_VERSION:
		if (msg->compid == v->compid)
			on_autopilot_version(v, msg);
		break;
	case MAVLINK_MSG_ID_STATUSTEXT: {
		mavlink_statustext_t st;
		mavlink_msg_statustext_decode(msg, &st);
		char text[51];
		memcpy(text, st.text, 50);
		text[50] = '\0';
		xstrlcpy(v->statustext, text, sizeof v->statustext);
		v->statustext_sev  = st.severity;
		v->statustext_time = now_s();
		int level		   = st.severity <= MAV_SEVERITY_ERROR ? MSG_ERR : st.severity <= MAV_SEVERITY_WARNING ? MSG_WARN : MSG_INFO;
		char buf[80];
		snprintf(buf, sizeof buf, "FC: %s", text);
		LOG("vehicle: STATUSTEXT[%u] %s", st.severity, text);
		if (v->on_msg)
			v->on_msg(v->on_msg_ctx, level, buf);
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* lifecycle                                                                  */

void vehicle_init(vehicle_t *v, link_t *link, const mavlink_message_t *hbmsg, uint8_t gcs_sysid, uint8_t gcs_compid, paramset_t *ps,
		vehicle_msg_fn fn, void *ctx) {
	memset(v, 0, sizeof *v);
	v->link = *link;
	link_init(link);
	v->spec		  = v->link.spec;
	v->link_up	  = true;
	v->gcs_sysid  = gcs_sysid;
	v->gcs_compid = gcs_compid;
	v->ps		  = ps;
	v->on_msg	  = fn;
	v->on_msg_ctx = ctx;
	v->sysid	  = hbmsg->sysid;
	v->compid	  = hbmsg->compid;

	mavlink_heartbeat_t hb;
	mavlink_msg_heartbeat_decode(hbmsg, &hb);
	v->mav_type		 = hb.type;
	v->autopilot	 = hb.autopilot;
	v->base_mode	 = hb.base_mode;
	v->custom_mode	 = hb.custom_mode;
	v->system_status = hb.system_status;
	v->last_hb_rx	 = now_s();
	v->mavlink1		 = hbmsg->magic == MAVLINK_STX_MAVLINK1;
	link_set_mavlink1(&v->link, v->mavlink1);

	switch (hb.autopilot) {
	case MAV_AUTOPILOT_ARDUPILOTMEGA:
		v->fw = VFW_ARDUPILOT;
		break;
	case MAV_AUTOPILOT_PX4:
		v->fw = VFW_PX4;
		break;
	default:
		v->fw = VFW_OTHER;
		break;
	}
	/* Defaults until AUTOPILOT_VERSION tells us otherwise. */
	v->enc			= v->fw == VFW_ARDUPILOT ? PENC_CCAST : PENC_BYTEWISE;
	v->version_next = 0;
	LOG("vehicle: connected sysid=%u compid=%u autopilot=%u type=%u mavlink%d via %s", v->sysid, v->compid, hb.autopilot, hb.type,
			v->mavlink1 ? 1 : 2, v->link.name);
}

void vehicle_close(vehicle_t *v) {
	link_close(&v->link);
	v->link_up = false;
}

bool vehicle_alive(const vehicle_t *v, double now) { return v->link_up && now - v->last_hb_rx < HB_TIMEOUT; }

int vehicle_pollfd(vehicle_t *v, struct pollfd *pfd) {
	if (!v->link_up || !link_is_open(&v->link))
		return 0;
	pfd->fd		 = v->link.fd;
	pfd->events	 = link_poll_events(&v->link);
	pfd->revents = 0;
	return 1;
}

void vehicle_service(vehicle_t *v, short revents) {
	if (!v->link_up)
		return;
	if (!link_service(&v->link, revents, on_message, v)) {
		v->link_up		  = false;
		v->next_reconnect = now_s() + 1.0;
		if (!v->reconnecting)
			vmsg(v, MSG_ERR, _("Link lost (%s) - reconnecting"), v->link.err);
		v->reconnecting = true;
	}
}

/* ------------------------------------------------------------------------ */
/* parameter protocol                                                         */

void vehicle_request_params(vehicle_t *v) {
	ps_reset_download(v->ps);
	v->pdl		   = PDL_LISTING;
	v->pdl_started = v->pdl_last_rx = now_s();
	v->pdl_next_action				= 0;
	v->pdl_list_tries				= 1;
	v->pdl_stall_rounds				= 0;
	v->pdl_last_count				= 0;
	v->gap_n						= 0;
	send_request_list(v);
}

int vehicle_write_pending(vehicle_t *v) {
	if (vehicle_writes_in_flight(v) == 0) {
		v->w_total = v->w_ok = v->w_failed = 0;
		v->w_reboot_needed				   = false;
	}
	int n = 0;
	for (int i = 0; i < v->ps->n; i++) {
		param_t *p = &v->ps->v[i];
		if (!p->pending || p->wstate == PW_SENT || p->wstate == PW_QUEUED)
			continue;
		p->wstate = PW_QUEUED;
		p->wtries = 0;
		n++;
	}
	v->w_total += n;
	v->ps->revision++;
	return n;
}

int vehicle_writes_in_flight(const vehicle_t *v) {
	int n = 0;
	for (int i = 0; i < v->ps->n; i++)
		n += v->ps->v[i].wstate == PW_QUEUED || v->ps->v[i].wstate == PW_SENT;
	return n;
}

static void tick_download(vehicle_t *v, double now) {
	paramset_t *ps = v->ps;
	if (v->pdl != PDL_LISTING)
		return;
	if (ps->expected == 0) {
		if (now - v->pdl_last_rx < LIST_TIMEOUT)
			return;
		if (v->pdl_list_tries >= LIST_TRIES) {
			v->pdl = PDL_FAILED;
			vmsg(v, MSG_ERR, "%s", _("No response to parameter list request"));
			return;
		}
		v->pdl_list_tries++;
		v->pdl_last_rx = now;
		send_request_list(v);
		return;
	}
	/* Next batch as soon as the previous one fully arrived (fast links), or
	 * after a quiet period (lossy / slow links). */
	bool batch_done = v->gap_n > 0;
	for (int i = 0; i < v->gap_n && batch_done; i++)
		batch_done = v->gap_req[i] >= ps->expected || ps->got[v->gap_req[i]];
	if (!batch_done && (now - v->pdl_last_rx < GAP_TIMEOUT || now < v->pdl_next_action))
		return;

	if (ps->got_count == v->pdl_last_count)
		v->pdl_stall_rounds++;
	else
		v->pdl_stall_rounds = 0;
	v->pdl_last_count = ps->got_count;
	if (v->pdl_stall_rounds > GAP_MAX_STALLS) {
		download_finished(v, false);
		return;
	}
	int sent = 0;
	for (int i = 0; i < ps->expected && sent < GAP_BATCH; i++) {
		if (!ps->got[i]) {
			send_request_read(v, i);
			v->gap_req[sent++] = i;
		}
	}
	v->gap_n = sent;
	LOG("vehicle: requested %d missing params (%d/%d)", sent, ps->got_count, ps->expected);
	v->pdl_next_action = now + GAP_TIMEOUT;
}

static void tick_writes(vehicle_t *v, double now) {
	paramset_t *ps = v->ps;
	int in_flight = 0, queued = 0;
	for (int i = 0; i < ps->n; i++) {
		param_t *p = &ps->v[i];
		if (p->wstate == PW_SENT) {
			if (now - p->wsent_at < WRITE_TIMEOUT) {
				in_flight++;
				continue;
			}
			if (p->wtries < WRITE_TRIES) {
				send_param_set(v, p);
				in_flight++;
				continue;
			}
			p->wstate = PW_FAILED;
			v->w_failed++;
			ps->revision++;
			char cur[32];
			param_format(p, p->value, cur, sizeof cur);
			vmsg(v, MSG_ERR, _("Write %s failed (vehicle value %s)"), p->name, cur);
		} else if (p->wstate == PW_QUEUED) {
			queued++;
		}
	}
	for (int i = 0; i < ps->n && in_flight < WRITE_IN_FLIGHT && queued > 0; i++) {
		param_t *p = &ps->v[i];
		if (p->wstate == PW_QUEUED) {
			send_param_set(v, p);
			in_flight++;
			queued--;
		}
	}
	if (v->w_total && in_flight == 0 && queued == 0) {
		if (v->w_failed)
			vmsg(v, MSG_ERR, ngettext("Wrote %d of %d parameter, %d failed", "Wrote %d of %d parameters, %d failed", v->w_total), v->w_ok,
					v->w_total, v->w_failed);
		else
			vmsg(v, MSG_OK, ngettext("Wrote %d parameter%s", "Wrote %d parameters%s", v->w_ok), v->w_ok,
					v->w_reboot_needed ? _(" - reboot required for changes to take effect") : "");
		v->w_total = 0;
		ps->revision++;
	}
}

void vehicle_tick(vehicle_t *v, double now) {
	if (!v->link_up) {
		if (now < v->next_reconnect)
			return;
		v->next_reconnect = now + 1.0;
		link_t nl;
		link_init(&nl);
		if (!link_open(&nl, &v->spec))
			return;
		bool v1 = v->mavlink1;
		v->link = nl;
		link_set_mavlink1(&v->link, v1);
		v->link_up		= true;
		v->reconnecting = true;
		LOG("vehicle: link re-opened: %s", v->link.name);
		/* resend anything that was in flight */
		for (int i = 0; i < v->ps->n; i++)
			if (v->ps->v[i].wstate == PW_SENT)
				v->ps->v[i].wsent_at = 0;
	}
	if (v->link.connecting)
		return;

	if (!v->hb_lost && !v->reconnecting && now - v->last_hb_rx >= HB_TIMEOUT) {
		v->hb_lost = true;
		vmsg(v, MSG_WARN, "%s", _("No heartbeat from the vehicle"));
	}
	if (now - v->last_hb_tx >= HB_PERIOD) {
		v->last_hb_tx = now;
		send_heartbeat(v);
	}
	if (!v->have_version && v->version_tries < 4 && now >= v->version_next) {
		v->version_tries++;
		v->version_next = now + 1.0;
		if (v->version_tries <= 3)
			send_command(v, MAV_CMD_REQUEST_MESSAGE, MAVLINK_MSG_ID_AUTOPILOT_VERSION);
		else /* older firmware */
			send_command(v, MAV_CMD_REQUEST_AUTOPILOT_CAPABILITIES, 1);
	}
	tick_download(v, now);
	tick_writes(v, now);
}

/* ------------------------------------------------------------------------ */
/* descriptions                                                               */

const char *vehicle_fw_name(const vehicle_t *v) {
	switch (v->fw) {
	case VFW_ARDUPILOT:
		return "ArduPilot";
	case VFW_PX4:
		return "PX4";
	default:
		return "MAVLink";
	}
}

/* Returns the untranslated (English) name: wrap in _() for display. */
const char *vehicle_type_name(uint8_t t) {
	switch (t) {
	case MAV_TYPE_FIXED_WING:
		return N_("Plane");
	case MAV_TYPE_QUADROTOR:
		return N_("Quadrotor");
	case MAV_TYPE_COAXIAL:
		return N_("Coaxial");
	case MAV_TYPE_HELICOPTER:
		return N_("Helicopter");
	case MAV_TYPE_ANTENNA_TRACKER:
		return N_("Antenna tracker");
	case MAV_TYPE_AIRSHIP:
		return N_("Airship");
	case MAV_TYPE_GROUND_ROVER:
		return N_("Rover");
	case MAV_TYPE_SURFACE_BOAT:
		return N_("Boat");
	case MAV_TYPE_SUBMARINE:
		return N_("Submarine");
	case MAV_TYPE_HEXAROTOR:
		return N_("Hexarotor");
	case MAV_TYPE_OCTOROTOR:
		return N_("Octorotor");
	case MAV_TYPE_TRICOPTER:
		return N_("Tricopter");
	case MAV_TYPE_DODECAROTOR:
		return N_("Dodecarotor");
	case MAV_TYPE_DECAROTOR:
		return N_("Decarotor");
	case MAV_TYPE_VTOL_TAILSITTER_DUOROTOR:
	case MAV_TYPE_VTOL_TAILSITTER_QUADROTOR:
	case MAV_TYPE_VTOL_TILTROTOR:
	case MAV_TYPE_VTOL_FIXEDROTOR:
	case MAV_TYPE_VTOL_TAILSITTER:
	case MAV_TYPE_VTOL_TILTWING:
	case MAV_TYPE_VTOL_RESERVED5:
		return N_("VTOL");
	default:
		return N_("Vehicle");
	}
}

void vehicle_version_string(const vehicle_t *v, char *out, size_t n) {
	if (!v->have_version || !v->flight_sw_version) {
		snprintf(out, n, "?");
		return;
	}
	uint32_t sw		 = v->flight_sw_version;
	const char *type = "";
	switch (sw & 0xff) {
	case FIRMWARE_VERSION_TYPE_DEV:
		type = "-dev";
		break;
	case FIRMWARE_VERSION_TYPE_ALPHA:
		type = "-alpha";
		break;
	case FIRMWARE_VERSION_TYPE_BETA:
		type = "-beta";
		break;
	case FIRMWARE_VERSION_TYPE_RC:
		type = "-rc";
		break;
	default:
		break;
	}
	snprintf(out, n, "%u.%u.%u%s", (sw >> 24) & 0xff, (sw >> 16) & 0xff, (sw >> 8) & 0xff, type);
}

void vehicle_apm_catalog(const vehicle_t *v, char *master, size_t mn, char *versioned, size_t vn) {
	const char *m = "ArduCopter", *ver = "Copter";
	switch (v->mav_type) {
	case MAV_TYPE_FIXED_WING:
	case MAV_TYPE_VTOL_TAILSITTER_DUOROTOR:
	case MAV_TYPE_VTOL_TAILSITTER_QUADROTOR:
	case MAV_TYPE_VTOL_TILTROTOR:
	case MAV_TYPE_VTOL_FIXEDROTOR:
	case MAV_TYPE_VTOL_TAILSITTER:
	case MAV_TYPE_VTOL_TILTWING:
	case MAV_TYPE_VTOL_RESERVED5:
		m	= "ArduPlane";
		ver = "Plane";
		break;
	case MAV_TYPE_GROUND_ROVER:
	case MAV_TYPE_SURFACE_BOAT:
		m	= "Rover";
		ver = "Rover";
		break;
	case MAV_TYPE_SUBMARINE:
		m	= "ArduSub";
		ver = "Sub";
		break;
	case MAV_TYPE_ANTENNA_TRACKER:
		m	= "AntennaTracker";
		ver = "Tracker";
		break;
	case MAV_TYPE_AIRSHIP:
		m	= "Blimp";
		ver = "";
		break;
	case MAV_TYPE_HELICOPTER:
		m	= "Heli";
		ver = "";
		break;
	default:
		break;
	}
	snprintf(master, mn, "%s", m);
	snprintf(versioned, vn, "%s", ver);
}
