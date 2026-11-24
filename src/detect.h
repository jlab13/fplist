/* Vehicle auto-detection, modelled on QGroundControl's auto-connect:
 *  - UDP: listen on 0.0.0.0:14550 (vehicles / SITL / companions send there)
 *  - USB serial: enumerate ttyACM/ttyUSB, classify by USB VID/PID
 *    (Pixhawk-family flight controllers, SiK radios), skip bootloaders,
 *    wait a moment after a port appears before opening it
 *  - TCP: additionally probe 127.0.0.1:5760 (ArduPilot SITL), which
 *    Mission Planner users expect
 * All candidates are opened at once; GCS heartbeats are sent on each (PX4
 * only starts MAVLink on USB after it sees traffic) and the first link that
 * delivers an autopilot HEARTBEAT wins. A forced connection (command line)
 * is just a single candidate that is retried until it works. */
#pragma once

#include <poll.h>
#include <stdbool.h>
#include <stdint.h>

#include "link.h"

#define DETECT_MAX 24

typedef enum {
	CAND_SERIAL_FC,	   /* known flight controller USB ids */
	CAND_SERIAL_RADIO, /* telemetry radio (SiK etc) */
	CAND_SERIAL_OTHER, /* unknown CDC-ACM device */
	CAND_UDP,
	CAND_TCP,
	CAND_FORCED,
} cand_kind_t;

typedef struct serial_port_info {
	char dev[64]; /* /dev/ttyACM0 */
	uint16_t vid, pid;
	char manufacturer[64];
	char product[64];
	bool usb;
} serial_port_info_t;

typedef struct detect_cand {
	bool used;
	cand_kind_t kind;
	link_spec_t spec;
	link_t link;
	char label[160];  /* human description, e.g. "ArduPilot speedybeef4v3" */
	char status[200]; /* current state, e.g. "waiting for heartbeat" */
	bool present;	  /* serial: still enumerated */
	bool skip;		  /* serial: bootloader etc., do not open */
	double first_seen;
	double next_try;
	double opened_at;
	uint64_t msgs_seen;
} detect_cand_t;

typedef struct detect_opts {
	bool forced;
	link_spec_t forced_spec;
	bool serial, udp, tcp;
	int udp_port, tcp_port;
	int target_sysid; /* -1: any */
	uint8_t gcs_sysid, gcs_compid;
} detect_opts_t;

typedef struct detector {
	detect_opts_t opt;
	detect_cand_t c[DETECT_MAX];
	double next_scan;
	double next_hb;
	double started;
	int winner; /* index or -1 */
	mavlink_message_t winner_hb;
} detector_t;

void detector_init(detector_t *d, const detect_opts_t *opt);
void detector_shutdown(detector_t *d);

/* Fills pollfds for open candidates; idx[i] gets the candidate index.
 * Returns number of entries written (<= max). */
int detector_pollfds(detector_t *d, struct pollfd *pfd, int *idx, int max);
void detector_service(detector_t *d, int cand, short revents);
void detector_tick(detector_t *d, double now);

/* If a vehicle was found, moves the winning link into *out (other
 * candidates are closed) and returns true. */
bool detector_take(detector_t *d, link_t *out, mavlink_message_t *hb);

/* Enumerate USB/serial ports (Linux sysfs). Returns count. */
int serial_enumerate(serial_port_info_t *out, int max);
/* Classification used by auto-connect. Returns a description in `what`. */
cand_kind_t serial_classify(const serial_port_info_t *p, int *baud, bool *bootloader, char *what, size_t n);

/* Is this heartbeat from something we should talk parameters to? */
bool heartbeat_is_vehicle(const mavlink_message_t *msg, uint8_t gcs_sysid);
