/* Application state and the single-threaded poll() main loop that glues
 * detection, the vehicle session, metadata loading and the UI together. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "detect.h"
#include "favs.h"
#include "meta.h"
#include "metafetch.h"
#include "params.h"
#include "ui.h"
#include "util.h"
#include "vehicle.h"

typedef struct app_opts {
	bool forced;
	link_spec_t forced_spec;
	int baud;
	bool serial, udp, tcp;
	int udp_port, tcp_port;
	int target_sysid; /* -1: first vehicle seen */
	uint8_t gcs_sysid, gcs_compid;
	char meta_path[1024];
	char meta_lang[16]; /* "auto" (from locale), "" = none, or ll_CC */
	bool offline;
	bool no_ftp; /* never use MAVFTP for parameters */
	bool dump;	 /* headless: print parameters and exit */
	double dump_timeout;
} app_opts_t;

typedef enum { PHASE_DETECT, PHASE_VEHICLE } app_phase_t;

typedef struct app {
	app_opts_t opt;
	app_phase_t phase;
	detector_t det;
	vehicle_t veh;
	paramset_t ps;
	double connected_at;

	meta_db_t *meta;
	metafetch_t mf;
	bool meta_requested, meta_finished;
	char meta_status[256];

	favs_t favs;
	unsigned attached_gen;

	ui_t ui;
	bool tui;
	bool quit;
	int exit_code;

	char msg[256];
	int msg_level;
	double msg_time;
} app_t;

int app_run(app_t *a);
void app_msg(app_t *a, int level, const char *fmt, ...) PRINTF_LIKE(3, 4);

/* Actions (used by the UI). */
bool app_set_pending(app_t *a, int idx, double v); /* false: equals vehicle value */
void app_clear_pending(app_t *a, int idx);
int app_clear_all_pending(app_t *a);
void app_write(app_t *a);
void app_refresh(app_t *a);
void app_toggle_fav(app_t *a, int idx);
void app_save_file(app_t *a, const char *path);
void app_load_file(app_t *a, const char *path);
void app_default_filename(app_t *a, char *out, size_t n);
