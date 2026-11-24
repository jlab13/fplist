#include "app.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "i18n.h"
#include "paramfile.h"

#define META_WAIT_VERSION 3.0 /* wait this long for AUTOPILOT_VERSION */

static volatile sig_atomic_t g_stop;
static void on_signal(int sig) { g_stop = 1; }

void app_msg(app_t *a, int level, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(a->msg, sizeof a->msg, fmt, ap);
	va_end(ap);
	a->msg_level = level;
	a->msg_time	 = now_s();
	LOG("msg[%d]: %s", level, a->msg);
	if (!a->tui)
		fprintf(stderr, "%s\n", a->msg);
	ui_invalidate(a);
}

static void vehicle_msg(void *ctx, int level, const char *text) { app_msg(ctx, level, "%s", text); }

/* ------------------------------------------------------------------------ */
/* actions                                                                    */

static bool values_same(const param_t *p, double a, double b) {
	if (param_type_is_int(p->type))
		return llround(a) == llround(b);
	return (float)a == (float)b;
}

bool app_set_pending(app_t *a, int idx, double v) {
	param_t *p = &a->ps.v[idx];
	if (p->wstate == PW_SENT || p->wstate == PW_QUEUED) {
		app_msg(a, MSG_WARN, _("%s is being written, try again in a moment"), p->name);
		return false;
	}
	a->ps.revision++;
	if (values_same(p, v, p->value)) {
		p->pending = false;
		p->wstate  = PW_NONE;
		return false;
	}
	p->pending		 = true;
	p->pending_value = v;
	p->wstate		 = PW_NONE;
	return true;
}

void app_clear_pending(app_t *a, int idx) {
	param_t *p = &a->ps.v[idx];
	if (p->wstate == PW_SENT || p->wstate == PW_QUEUED)
		return;
	p->pending = false;
	p->wstate  = PW_NONE;
	a->ps.revision++;
}

int app_clear_all_pending(app_t *a) {
	int n = 0;
	for (int i = 0; i < a->ps.n; i++) {
		if (a->ps.v[i].pending) {
			app_clear_pending(a, i);
			n += !a->ps.v[i].pending;
		}
	}
	return n;
}

void app_write(app_t *a) {
	if (a->phase != PHASE_VEHICLE)
		return;
	if (!a->veh.link_up) {
		app_msg(a, MSG_ERR, "%s", _("Not connected"));
		return;
	}
	int n = vehicle_write_pending(&a->veh);
	if (n == 0)
		app_msg(a, MSG_INFO, "%s", _("Nothing to write - edit a value first (Enter)"));
	else
		app_msg(a, MSG_INFO, ngettext("Writing %d parameter...", "Writing %d parameters...", n), n);
}

void app_refresh(app_t *a) {
	if (a->phase != PHASE_VEHICLE)
		return;
	if (vehicle_writes_in_flight(&a->veh)) {
		app_msg(a, MSG_WARN, "%s", _("Wait for the write to finish"));
		return;
	}
	vehicle_request_params(&a->veh);
	app_msg(a, MSG_INFO, "%s", _("Refreshing parameters..."));
}

void app_toggle_fav(app_t *a, int idx) {
	param_t *p = &a->ps.v[idx];
	p->fav	   = favs_toggle(&a->favs, p->name);
	a->ps.generation++; /* resort: favourites go first */
	a->ps.revision++;
}

void app_default_filename(app_t *a, char *out, size_t n) {
	char ts[32];
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", &tm);
	const char *fw = a->phase == PHASE_VEHICLE ? vehicle_fw_name(&a->veh) : "vehicle";
	const char *ty = a->phase == PHASE_VEHICLE ? vehicle_type_name(a->veh.mav_type) : "";
	char name[128];
	snprintf(name, sizeof name, "%s-%s-%s.param", fw, ty, ts);
	for (char *c = name; *c; c++)
		if (*c == ' ')
			*c = '_';
	snprintf(out, n, "%s", name);
}

void app_save_file(app_t *a, const char *path_in) {
	char path[1024], err[512], header[256];
	path_expand_home(path_in, path, sizeof path);
	char ver[64] = "?";
	if (a->phase == PHASE_VEHICLE)
		vehicle_version_string(&a->veh, ver, sizeof ver);
	time_t t = time(NULL);
	char ts[64];
	strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
	snprintf(header, sizeof header, "%s, %s %s %s, saved by fpl", ts, vehicle_fw_name(&a->veh), vehicle_type_name(a->veh.mav_type), ver);
	if (paramfile_save(&a->ps, path, header, err, sizeof err))
		app_msg(a, MSG_OK, ngettext("Saved %d parameter to %s", "Saved %d parameters to %s", a->ps.n), a->ps.n, path);
	else
		app_msg(a, MSG_ERR, _("Save failed: %s"), err);
}

void app_load_file(app_t *a, const char *path_in) {
	char path[1024], err[512];
	path_expand_home(path_in, path, sizeof path);
	int n;
	pf_entry_t *e = paramfile_load(path, &n, err, sizeof err);
	if (!e) {
		app_msg(a, MSG_ERR, _("Load failed: %s"), err);
		return;
	}
	int changed = 0, same = 0, unknown = 0;
	for (int i = 0; i < n; i++) {
		int idx = ps_find(&a->ps, e[i].name);
		if (idx < 0 || !a->ps.v[idx].have_value) {
			unknown++;
			continue;
		}
		if (app_set_pending(a, idx, e[i].value))
			changed++;
		else
			same++;
	}
	free(e);
	app_msg(a, changed ? MSG_OK : MSG_INFO, _("Loaded %s: %d changed, %d same, %d not on vehicle%s"), path, changed, same, unknown,
			changed ? _(" - press w to write") : "");
}

/* ------------------------------------------------------------------------ */
/* connection lifecycle                                                       */

static void start_vehicle(app_t *a) {
	link_t l;
	mavlink_message_t hb;
	if (!detector_take(&a->det, &l, &hb))
		return;
	ps_clear(&a->ps);
	vehicle_init(&a->veh, &l, &hb, a->opt.gcs_sysid, a->opt.gcs_compid, &a->ps, vehicle_msg, a);
	a->veh.ftp_disabled = a->opt.no_ftp;
	a->phase			= PHASE_VEHICLE;
	a->connected_at		= now_s();
	app_msg(a, MSG_OK, _("Connected to %s %s (sysid %u) via %s"), vehicle_fw_name(&a->veh), _(vehicle_type_name(a->veh.mav_type)),
			a->veh.sysid, a->veh.link.name);
	vehicle_request_params(&a->veh);
}

static void maybe_start_meta(app_t *a, double now) {
	if (a->meta_requested || a->phase != PHASE_VEHICLE)
		return;
	vehicle_t *v = &a->veh;
	if (!a->opt.meta_path[0] && !v->have_version && now - a->connected_at < META_WAIT_VERSION)
		return;
	metafetch_req_t r = {0};
	r.offline		  = a->opt.offline;
	if (strcmp(a->opt.meta_lang, "auto") == 0)
		i18n_meta_lang(r.lang, sizeof r.lang);
	else
		snprintf(r.lang, sizeof r.lang, "%s", a->opt.meta_lang);
	snprintf(r.override_path, sizeof r.override_path, "%s", a->opt.meta_path);
	r.ardupilot = v->fw != VFW_PX4;
	if (r.ardupilot) {
		vehicle_apm_catalog(v, r.apm_master, sizeof r.apm_master, r.apm_versioned, sizeof r.apm_versioned);
		uint32_t sw = v->flight_sw_version;
		if (v->have_version && sw && (sw & 0xff) == FIRMWARE_VERSION_TYPE_OFFICIAL)
			snprintf(r.version, sizeof r.version, "%u.%u.%u", (sw >> 24) & 0xff, (sw >> 16) & 0xff, (sw >> 8) & 0xff);
	}
	a->meta_requested = true;
	snprintf(a->meta_status, sizeof a->meta_status, "%s", _("loading metadata..."));
	metafetch_start(&a->mf, &r);
	ui_invalidate(a);
}

static void poll_meta(app_t *a) {
	meta_db_t *db;
	char msg[512];
	if (!metafetch_poll(&a->mf, &db, msg, sizeof msg))
		return;
	a->meta_finished = true;
	if (db) {
		meta_db_free(a->meta);
		a->meta = db;
		snprintf(a->meta_status, sizeof a->meta_status, "%s", db->source);
		ps_attach_meta(&a->ps, a->meta);
		ps_assign_groups(&a->ps, a->veh.fw != VFW_PX4);
		a->attached_gen = a->ps.generation;
		app_msg(a, MSG_INFO, "%s", msg);
	} else {
		snprintf(a->meta_status, sizeof a->meta_status, "%s", _("no metadata"));
		app_msg(a, MSG_WARN, "%s", msg);
	}
}

/* ------------------------------------------------------------------------ */
/* headless dump                                                              */

static int cmp_name(const void *x, const void *y, void *ctx) {
	const paramset_t *ps = ctx;
	return strcmp(ps->v[*(const int *)x].name, ps->v[*(const int *)y].name);
}

static void dump_params(app_t *a) {
	int *idx = xmalloc((size_t)MAX(a->ps.n, 1) * sizeof *idx);
	for (int i = 0; i < a->ps.n; i++)
		idx[i] = i;
	qsort_r(idx, (size_t)a->ps.n, sizeof *idx, cmp_name, &a->ps);
	printf("Command\tValue\tUnits\tOptions\tDesc\tFav\n");
	for (int k = 0; k < a->ps.n; k++) {
		const param_t *p = &a->ps.v[idx[k]];
		char val[48];
		param_format(p, p->value, val, sizeof val);
		const param_meta_t *m = p->meta;
		printf("%s\t%s\t%s\t%s\t%s\t%s\n", p->name, val, m && m->units ? m->units : "", m && m->options ? m->options : "",
				m && m->desc ? m->desc : "", p->fav ? "1" : "0");
	}
	free(idx);
	fflush(stdout);
}

static bool dump_finished(app_t *a, double now) {
	if (a->phase != PHASE_VEHICLE)
		return false;
	if (a->veh.pdl != PDL_DONE && a->veh.pdl != PDL_FAILED)
		return false;
	return a->meta_finished;
}

/* ------------------------------------------------------------------------ */
/* main loop                                                                  */

int app_run(app_t *a) {
	detect_opts_t dopt = {
			.forced		  = a->opt.forced,
			.forced_spec  = a->opt.forced_spec,
			.serial		  = a->opt.serial,
			.udp		  = a->opt.udp,
			.tcp		  = a->opt.tcp,
			.udp_port	  = a->opt.udp_port,
			.tcp_port	  = a->opt.tcp_port,
			.target_sysid = a->opt.target_sysid,
			.gcs_sysid	  = a->opt.gcs_sysid,
			.gcs_compid	  = a->opt.gcs_compid,
	};
	detector_init(&a->det, &dopt);
	ps_init(&a->ps);
	favs_load(&a->favs);
	a->phase = PHASE_DETECT;
	a->tui	 = !a->opt.dump;

	if (a->opt.dump) {
		signal(SIGINT, on_signal);
		signal(SIGTERM, on_signal);
	}
	signal(SIGPIPE, SIG_IGN);

	if (a->tui && !ui_init(a)) {
		fputs(_("fpl: cannot initialise the terminal\n"), stderr);
		return 1;
	}
	if (!a->tui)
		fputs(_("Searching for vehicle...\n"), stderr);

	double started = now_s();
	while (!a->quit && !g_stop) {
		struct pollfd pfd[DETECT_MAX + 2];
		int cand[DETECT_MAX + 2];
		int n = 0, ui_slot = -1, veh_slot = -1;
		if (a->tui) {
			pfd[n].fd	   = ui_input_fd(a);
			pfd[n].events  = POLLIN;
			pfd[n].revents = 0;
			ui_slot		   = n++;
		}
		int det_first = n;
		if (a->phase == PHASE_DETECT) {
			n += detector_pollfds(&a->det, pfd + n, cand + n, DETECT_MAX);
		} else if (vehicle_pollfd(&a->veh, &pfd[n])) {
			veh_slot = n++;
		}

		int rc = poll(pfd, (nfds_t)n, 50);
		if (rc < 0 && errno != EINTR) {
			a->exit_code = 1;
			break;
		}
		double now = now_s();

		if (ui_slot >= 0 && (pfd[ui_slot].revents & POLLIN))
			ui_handle_input(a);

		if (a->phase == PHASE_DETECT) {
			for (int i = det_first; i < n; i++)
				if (pfd[i].revents)
					detector_service(&a->det, cand[i], pfd[i].revents);
			detector_tick(&a->det, now);
			if (a->det.winner >= 0)
				start_vehicle(a);
			ui_invalidate(a); /* candidate status / spinner */
		} else {
			if (veh_slot >= 0 && pfd[veh_slot].revents)
				vehicle_service(&a->veh, pfd[veh_slot].revents);
			vehicle_tick(&a->veh, now);
			maybe_start_meta(a, now);
			poll_meta(a);
			if (a->ps.generation != a->attached_gen) {
				ps_attach_meta(&a->ps, a->meta);
				ps_assign_groups(&a->ps, a->veh.fw != VFW_PX4);
				favs_apply(&a->favs, &a->ps);
				a->attached_gen = a->ps.generation;
			}
		}

		if (a->tui) {
			ui_maybe_render(a, now);
		} else {
			if (dump_finished(a, now)) {
				dump_params(a);
				a->exit_code = a->veh.pdl == PDL_DONE ? 0 : 2;
				break;
			}
			if (a->opt.dump_timeout > 0 && now - started > a->opt.dump_timeout) {
				fprintf(stderr, _("fpl: timeout (%s)\n"), a->phase == PHASE_DETECT ? _("no vehicle found") : _("parameter download"));
				if (a->phase == PHASE_VEHICLE && a->ps.n)
					dump_params(a);
				a->exit_code = 3;
				break;
			}
		}
	}

	if (a->tui)
		ui_shutdown(a);
	metafetch_cancel(&a->mf);
	if (a->phase == PHASE_VEHICLE)
		vehicle_close(&a->veh);
	detector_shutdown(&a->det);
	meta_db_free(a->meta);
	ps_free(&a->ps);
	favs_free(&a->favs);
	return a->exit_code;
}
