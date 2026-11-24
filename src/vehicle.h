/* A connected vehicle: heartbeat bookkeeping, AUTOPILOT_VERSION, and the
 * MAVLink parameter protocol (download with gap filling, writes with retry).
 * Mirrors what QGroundControl's ParameterManager and Mission Planner do. */
#pragma once

#include <poll.h>
#include <stdbool.h>
#include <stdint.h>

#include "ftp.h"
#include "link.h"
#include "params.h"

typedef enum { VFW_ARDUPILOT, VFW_PX4, VFW_OTHER } vehicle_fw_t;

typedef enum {
	PDL_IDLE,
	PDL_FTP,	 /* MAVFTP @PARAM/param.pck (ArduPilot) */
	PDL_LISTING, /* PARAM_REQUEST_LIST sent, streaming / filling gaps */
	PDL_DONE,
	PDL_FAILED,
} pdl_state_t;

typedef void (*vehicle_msg_fn)(void *ctx, int level, const char *text);

enum { MSG_INFO = 0, MSG_OK, MSG_WARN, MSG_ERR };

typedef struct vehicle {
	link_t link;
	link_spec_t spec; /* for reconnects */
	bool link_up;
	bool reconnecting; /* link re-opened, waiting for the vehicle to talk */
	double next_reconnect;

	uint8_t gcs_sysid, gcs_compid;
	uint8_t sysid, compid;

	/* last heartbeat */
	uint8_t mav_type, autopilot, base_mode, system_status;
	uint32_t custom_mode;
	double last_hb_rx, last_hb_tx;
	bool mavlink1;
	bool hb_lost; /* already reported */

	/* AUTOPILOT_VERSION */
	bool have_version;
	uint64_t capabilities;
	uint32_t flight_sw_version;
	int version_tries;
	double version_next;

	vehicle_fw_t fw;
	param_enc_t enc;

	paramset_t *ps;
	pdl_state_t pdl;
	double pdl_started, pdl_last_rx, pdl_next_action;
	int pdl_list_tries;
	int pdl_stall_rounds;
	int pdl_last_count;
	double pdl_duration;
	int gap_req[64]; /* indices asked for in the current gap-fill batch */
	int gap_n;

	/* MAVFTP parameter download (ArduPilot), with defaults */
	ftp_t ftp;
	bool ftp_disabled; /* failed before: use PARAM_REQUEST_LIST */
	bool via_ftp;	   /* last complete download came over MAVFTP */

	/* write session statistics */
	int w_total, w_ok, w_failed;
	bool w_reboot_needed;

	char statustext[160];
	uint8_t statustext_sev;
	double statustext_time;

	vehicle_msg_fn on_msg;
	void *on_msg_ctx;
} vehicle_t;

/* Takes ownership of an already-open link (from the detector). */
void vehicle_init(vehicle_t *v, link_t *link, const mavlink_message_t *hb, uint8_t gcs_sysid, uint8_t gcs_compid, paramset_t *ps,
		vehicle_msg_fn fn, void *ctx);
void vehicle_close(vehicle_t *v);

/* poll() integration */
int vehicle_pollfd(vehicle_t *v, struct pollfd *pfd);
void vehicle_service(vehicle_t *v, short revents);
void vehicle_tick(vehicle_t *v, double now);

bool vehicle_alive(const vehicle_t *v, double now);

/* Parameter operations */
void vehicle_request_params(vehicle_t *v);
/* Queue all pending edits for writing. Returns number queued. */
int vehicle_write_pending(vehicle_t *v);
int vehicle_writes_in_flight(const vehicle_t *v);

/* Descriptions */
const char *vehicle_fw_name(const vehicle_t *v);
const char *vehicle_type_name(uint8_t mav_type);
void vehicle_version_string(const vehicle_t *v, char *out, size_t n);
/* Metadata catalog names for ArduPilot ("ArduCopter"/"Copter" ...). */
void vehicle_apm_catalog(const vehicle_t *v, char *master, size_t mn, char *versioned, size_t vn);
