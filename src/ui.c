/* UI core: notcurses setup, input dispatch, title/status bars, the
 * auto-detect screen, help and dialogs. Table: ui_table.c, editors: ui_edit.c */
#include "ui.h"

#include <math.h>
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "util.h"

#define MSG_SHOW_S 10.0

/* ------------------------------------------------------------------------ */
/* drawing helpers                                                            */

void ui_rgb_fg(struct ncplane *n, uint32_t rgb) { ncplane_set_fg_rgb(n, rgb); }
void ui_rgb_bg(struct ncplane *n, uint32_t rgb) { ncplane_set_bg_rgb(n, rgb); }

void ui_default_colors(struct ncplane *n) {
	ncplane_set_fg_default(n);
	ncplane_set_bg_default(n);
	ncplane_set_styles(n, NCSTYLE_NONE);
}

void ui_fill(struct ncplane *n, int y, int x, int w) {
	static const char spaces[] = "                                                                ";
	while (w > 0) {
		int k = MIN(w, (int)sizeof spaces - 1);
		ncplane_putnstr_yx(n, y, x, (size_t)k, spaces);
		x += k;
		w -= k;
	}
}

/* Writes at most `w` columns of s[0..len) and pads with spaces to `w`.
 * Control characters are shown as spaces. */
void ui_putn(struct ncplane *n, int y, int x, int w, const char *s, int len) {
	if (w <= 0)
		return;
	char buf[1024];
	int bl = 0;
	for (int i = 0; i < len && s[i] && bl < (int)sizeof buf - 1; i++)
		buf[bl++] = (unsigned char)s[i] < 0x20 || s[i] == 0x7f ? ' ' : s[i];
	buf[bl]	   = '\0';
	int cols   = 0;
	size_t fit = utf8_fit(buf, w, &cols);
	if (fit)
		ncplane_putnstr_yx(n, y, x, fit, buf);
	if (cols < w)
		ui_fill(n, y, x + cols, w - cols);
}

void ui_put(struct ncplane *n, int y, int x, int w, const char *s) { ui_putn(n, y, x, w, s ? s : "", s ? (int)strlen(s) : 0); }

void ui_box(struct ncplane *n, int y, int x, int h, int w, const char *title) {
	ui_rgb_bg(n, C_POPUP_BG);
	ncplane_set_fg_default(n);
	for (int r = 0; r < h; r++)
		ui_fill(n, y + r, x, w);
	ui_rgb_fg(n, C_ACCENT);
	for (int c = 1; c < w - 1; c++) {
		ncplane_putstr_yx(n, y, x + c, "─");
		ncplane_putstr_yx(n, y + h - 1, x + c, "─");
	}
	for (int r = 1; r < h - 1; r++) {
		ncplane_putstr_yx(n, y + r, x, "│");
		ncplane_putstr_yx(n, y + r, x + w - 1, "│");
	}
	ncplane_putstr_yx(n, y, x, "┌");
	ncplane_putstr_yx(n, y, x + w - 1, "┐");
	ncplane_putstr_yx(n, y + h - 1, x, "└");
	ncplane_putstr_yx(n, y + h - 1, x + w - 1, "┘");
	if (title && *title) {
		char t[256];
		snprintf(t, sizeof t, " %s ", title);
		ncplane_set_styles(n, NCSTYLE_BOLD);
		int tw = MIN(utf8_width(t), w - 4);
		ui_put(n, y, x + 2, tw, t);
		ncplane_set_styles(n, NCSTYLE_NONE);
	}
	ncplane_set_fg_default(n);
}

/* ------------------------------------------------------------------------ */
/* lifecycle                                                                  */

bool ui_init(app_t *a) {
	ui_t *u					   = &a->ui;
	struct notcurses_options o = {
			.flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_SUPPRESS_BANNERS,
	};
	u->nc = notcurses_core_init(&o, NULL); /* core: no multimedia deps */
	if (!u->nc)
		return false;
	u->std = notcurses_stdplane(u->nc);
	notcurses_mice_enable(u->nc, NCMICE_BUTTON_EVENT);
	notcurses_linesigs_disable(u->nc);
	notcurses_stddim_yx(u->nc, &u->rows, &u->cols);
	u->details		  = true;
	u->view_stale	  = true;
	u->dirty		  = true;
	u->last_click_row = -1;
	return true;
}

void ui_shutdown(app_t *a) {
	ui_t *u = &a->ui;
	if (u->nc)
		notcurses_stop(u->nc);
	u->nc = NULL;
	free(u->view);
	u->view = NULL;
}

int ui_input_fd(app_t *a) { return notcurses_inputready_fd(a->ui.nc); }

void ui_invalidate(app_t *a) { a->ui.dirty = true; }

/* ------------------------------------------------------------------------ */
/* title / status                                                             */

static void draw_title(app_t *a) {
	ui_t *u			  = &a->ui;
	struct ncplane *n = u->std;
	int W			  = (int)u->cols;
	ui_rgb_bg(n, C_TITLE_BG);
	ui_rgb_fg(n, C_TITLE_FG);
	ui_fill(n, 0, 0, W);

	char left[512], right[300];
	if (a->phase == PHASE_DETECT) {
		static const char *spin[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
		int k					  = (int)(now_s() * 10) % 10;
		snprintf(left, sizeof left, " gndtui  │  searching for vehicle %s", spin[k]);
		right[0] = '\0';
	} else {
		vehicle_t *v = &a->veh;
		char ver[64];
		vehicle_version_string(v, ver, sizeof ver);
		char prog[96];
		paramset_t *ps = &a->ps;
		if (v->pdl == PDL_LISTING && ps->expected) {
			int pct		 = ps->got_count * 100 / ps->expected;
			char bar[64] = "";
			int filled	 = pct / 10;
			for (int i = 0; i < 10; i++)
				strcat(bar, i < filled ? "█" : "░");
			snprintf(prog, sizeof prog, "Params %d/%d %s", ps->got_count, ps->expected, bar);
		} else if (v->pdl == PDL_LISTING) {
			snprintf(prog, sizeof prog, "Requesting params...");
		} else {
			snprintf(prog, sizeof prog, "Params %d", ps->n);
		}
		int pend		= ps_pending_count(ps);
		char pend_s[48] = "";
		if (pend)
			snprintf(pend_s, sizeof pend_s, "  │  %d modified", pend);
		snprintf(left, sizeof left, " gndtui  │  %s %s %s  │  sys %u  │  %s  │  %s%s", vehicle_fw_name(v), vehicle_type_name(v->mav_type),
				ver, v->sysid, v->link.name, prog, pend_s);
		snprintf(right, sizeof right, "%s", a->meta_status);
	}
	/* Right side: link indicator always, metadata source when it fits. */
	const char *ind = NULL;
	bool alive		= false;
	if (a->phase == PHASE_VEHICLE) {
		alive = vehicle_alive(&a->veh, now_s());
		ind	  = alive ? " ● LINK " : " ○ NO LINK ";
	}
	int indw  = ind ? utf8_width(ind) : 0;
	int lw	  = utf8_width(left);
	int metaw = right[0] ? utf8_width(right) + 2 : 0;
	if (lw + metaw + indw > W)
		metaw = 0;
	ncplane_set_styles(n, NCSTYLE_BOLD);
	ui_put(n, 0, 0, MAX(W - indw - metaw, 0), left);
	ncplane_set_styles(n, NCSTYLE_NONE);
	if (metaw) {
		ui_rgb_fg(n, 0xc8e6c9);
		ui_put(n, 0, W - indw - metaw, metaw, right);
	}
	if (ind) {
		ui_rgb_fg(n, alive ? 0xb9f6ca : 0xff8a80);
		ncplane_set_styles(n, NCSTYLE_BOLD);
		ui_put(n, 0, W - indw, indw, ind);
		ncplane_set_styles(n, NCSTYLE_NONE);
	}
	ui_default_colors(n);
}

static uint32_t level_color(int level) {
	switch (level) {
	case MSG_OK:
		return C_OK;
	case MSG_WARN:
		return C_WARN;
	case MSG_ERR:
		return C_ERR;
	default:
		return C_HDR_FG;
	}
}

/* Draws a line editor; returns the cursor column. */
static int draw_lineedit(struct ncplane *n, int y, int x, int w, const lineedit_t *le) {
	if (w <= 1)
		return x;
	int skip = 0;
	char tmp[sizeof le->buf];
	for (;;) {
		int l = le->cur - skip;
		memcpy(tmp, le->buf + skip, (size_t)l);
		tmp[l] = '\0';
		if (utf8_width(tmp) <= w - 1 || skip >= le->cur)
			break;
		skip++;
		while (skip < le->cur && ((unsigned char)le->buf[skip] & 0xC0) == 0x80)
			skip++;
	}
	int curx = x + utf8_width(tmp);
	ui_putn(n, y, x, w, le->buf + skip, le->len - skip);
	return curx;
}

static void draw_status(app_t *a, int *cursor_y, int *cursor_x) {
	ui_t *u			  = &a->ui;
	struct ncplane *n = u->std;
	int W = (int)u->cols, y = (int)u->rows - 1;
	ui_rgb_bg(n, C_HDR_BG);
	ui_rgb_fg(n, C_HDR_FG);
	ui_fill(n, y, 0, W);

	char right[64] = "";
	if (a->phase == PHASE_VEHICLE)
		snprintf(right, sizeof right, " %d/%d ", u->nview, a->ps.n);
	int rw	  = utf8_width(right);
	int avail = W - rw;

	switch (u->mode) {
	case MODE_SEARCH: {
		ncplane_set_styles(n, NCSTYLE_BOLD);
		ui_put(n, y, 0, 9, " Search: ");
		ncplane_set_styles(n, NCSTYLE_NONE);
		const char *hint = "  Enter: keep  Esc: clear";
		int hw			 = utf8_width(hint);
		*cursor_y		 = y;
		*cursor_x		 = draw_lineedit(n, y, 9, MAX(avail - 9 - hw, 4), &u->search);
		ui_rgb_fg(n, C_DIM);
		if (avail - hw > 13)
			ui_put(n, y, avail - hw, hw, hint);
		break;
	}
	case MODE_EDIT: {
		const param_t *p = &a->ps.v[u->edit_param];
		char lab[48];
		snprintf(lab, sizeof lab, " %s = ", p->name);
		int lw = utf8_width(lab);
		ncplane_set_styles(n, NCSTYLE_BOLD);
		ui_put(n, y, 0, lw, lab);
		ncplane_set_styles(n, NCSTYLE_NONE);
		int ew = 18;
		ui_rgb_bg(n, 0x101010);
		*cursor_y = y;
		*cursor_x = draw_lineedit(n, y, lw, ew, &u->edit);
		ui_rgb_bg(n, C_HDR_BG);
		char hint[200];
		if (u->edit_err[0]) {
			ui_rgb_fg(n, C_ERR);
			snprintf(hint, sizeof hint, "  %s", u->edit_err);
		} else {
			ui_rgb_fg(n, C_DIM);
			const param_meta_t *m = p->meta;
			char rng[96]		  = "";
			if (m && m->has_range) {
				char lo[32], hi[32];
				fmt_number(m->min, false, lo, sizeof lo);
				fmt_number(m->max, false, hi, sizeof hi);
				snprintf(rng, sizeof rng, "range %s..%s%s%s  ", lo, hi, m->units ? " " : "", m->units ? m->units : "");
			}
			snprintf(hint, sizeof hint, "  %s%sEnter: set  Esc: cancel%s", rng, param_type_is_int(p->type) ? "" : "",
					m && (m->nvalues || m->nbits) ? "  Tab: list" : "");
		}
		ui_put(n, y, lw + ew, MAX(avail - lw - ew, 0), hint);
		break;
	}
	case MODE_PROMPT: {
		const char *lab = u->prompt_kind == PROMPT_SAVE ? " Save to: " : " Load from: ";
		int lw			= utf8_width(lab);
		ncplane_set_styles(n, NCSTYLE_BOLD);
		ui_put(n, y, 0, lw, lab);
		ncplane_set_styles(n, NCSTYLE_NONE);
		*cursor_y = y;
		*cursor_x = draw_lineedit(n, y, lw, MAX(avail - lw - 26, 8), &u->prompt);
		ui_rgb_fg(n, C_DIM);
		ui_put(n, y, avail - 26, 26, "  Enter: ok  Esc: cancel");
		break;
	}
	case MODE_CONFIRM: {
		ui_rgb_fg(n, C_WARN);
		ncplane_set_styles(n, NCSTYLE_BOLD);
		char t[256];
		snprintf(t, sizeof t, " %s  [y/n]", u->confirm_text);
		ui_put(n, y, 0, avail, t);
		ncplane_set_styles(n, NCSTYLE_NONE);
		break;
	}
	default: {
		double now = now_s();
		if (a->msg[0] && now - a->msg_time < MSG_SHOW_S) {
			ui_rgb_fg(n, level_color(a->msg_level));
			char t[300];
			snprintf(t, sizeof t, " %s", a->msg);
			ui_put(n, y, 0, avail, t);
		} else if (a->phase == PHASE_DETECT) {
			ui_put(n, y, 0, avail, " q: quit");
		} else {
			ui_put(n, y, 0, avail,
					" ↑↓ move  Enter edit  / search  w write  r refresh  f fav  m modified  "
					"s save  l load  v wrap  d details  ? help  q quit");
		}
		break;
	}
	}
	ui_rgb_bg(n, C_HDR_BG);
	ui_rgb_fg(n, C_DIM);
	if (rw && avail > 0)
		ui_put(n, y, avail, rw, right);
	ui_default_colors(n);
}

/* ------------------------------------------------------------------------ */
/* detect screen                                                              */

static void draw_detect(app_t *a) {
	ui_t *u			  = &a->ui;
	struct ncplane *n = u->std;
	int W = (int)u->cols, H = (int)u->rows;
	detector_t *d = &a->det;

	int y = 2;
	ncplane_set_styles(n, NCSTYLE_BOLD);
	ui_put(n, y++, 2, W - 4, d->opt.forced ? "Connecting:" : "Looking for a vehicle on these connections:");
	ncplane_set_styles(n, NCSTYLE_NONE);
	y++;
	int lw = MIN(60, W / 2);
	ui_rgb_bg(n, C_HDR_BG);
	ui_rgb_fg(n, C_HDR_FG);
	ui_fill(n, y, 2, W - 4);
	ui_put(n, y, 3, lw - 1, "Connection");
	ui_put(n, y, 2 + lw, W - 4 - lw, "Status");
	ui_default_colors(n);
	y++;
	int shown = 0;
	for (int i = 0; i < DETECT_MAX && y < H - 6; i++) {
		detect_cand_t *c = &d->c[i];
		if (!c->used)
			continue;
		shown++;
		ui_put(n, y, 3, lw - 1, c->label);
		bool open = link_is_open(&c->link) && !c->link.connecting;
		bool bad  = !link_is_open(&c->link) && !c->skip && strcmp(c->status, "waiting") != 0;
		ui_rgb_fg(n, bad ? C_ERR : open ? C_OK : C_DIM);
		char st[260];
		snprintf(st, sizeof st, "%s%s", c->status, c->msgs_seen ? "" : "");
		ui_put(n, y, 2 + lw, W - 4 - lw, st);
		if (open && c->link.rx_msgs) {
			char cnt[48];
			snprintf(cnt, sizeof cnt, " %llu msgs", (unsigned long long)c->link.rx_msgs);
			int cw = utf8_width(cnt);
			if (W - 4 - lw > cw + utf8_width(st) + 2) {
				ui_rgb_fg(n, C_DIM);
				ui_put(n, y, W - 2 - cw, cw, cnt);
			}
		}
		ui_default_colors(n);
		y++;
	}
	if (!shown) {
		ui_rgb_fg(n, C_DIM);
		ui_put(n, y++, 3, W - 6, "(no candidate ports yet - plug in a flight controller)");
		ui_default_colors(n);
	}

	static const char *hints[] = {
			"Auto-detect (like QGroundControl): USB flight controllers & SiK radios,",
			"UDP listen on :14550, TCP 127.0.0.1:5760 (ArduPilot SITL).",
			"Force a connection:  gndtui /dev/ttyACM0   gndtui udp:14550   gndtui tcp:host:5760",
	};
	int hy = H - 2 - (int)ARRAY_LEN(hints);
	ui_rgb_fg(n, C_DIM);
	for (size_t i = 0; i < ARRAY_LEN(hints); i++)
		if (hy + (int)i > y)
			ui_put(n, hy + (int)i, 2, W - 4, hints[i]);
	ui_default_colors(n);
}

/* ------------------------------------------------------------------------ */
/* help                                                                       */

static void draw_help(app_t *a) {
	static const char *lines[] = {
			"Navigation",
			"  ↑ ↓ / j k         move            PgUp PgDn   page",
			"  Home End / g G    first / last     mouse       click, wheel",
			"Editing",
			"  Enter / e         edit value (list / bitmask editor if known)",
			"  z / Z             undo edit of this / all parameters",
			"  w                 write modified parameters to the vehicle",
			"  r                 refresh (re-download) all parameters",
			"View",
			"  /                 search name & description   Esc clears",
			"  m                 show only modified parameters",
			"  f / Space         toggle favourite (favourites sort first)",
			"  v                 wrap rows (Mission Planner style)",
			"  d                 toggle the details pane",
			"Files",
			"  s                 save parameters (Mission Planner .param)",
			"  l                 load parameters from file (become edits)",
			"",
			"  q / Ctrl-C        quit",
	};
	ui_t *u = &a->ui;
	int w	= 0;
	for (size_t i = 0; i < ARRAY_LEN(lines); i++)
		w = MAX(w, utf8_width(lines[i]));
	w	   = MIN(w + 6, (int)u->cols - 2);
	int h  = MIN((int)ARRAY_LEN(lines) + 4, (int)u->rows - 2);
	int y0 = ((int)u->rows - h) / 2, x0 = ((int)u->cols - w) / 2;
	ui_box(u->std, y0, x0, h, w, "gndtui " GNDTUI_VERSION " - keys");
	struct ncplane *n = u->std;
	ui_rgb_bg(n, C_POPUP_BG);
	for (int i = 0; i < (int)ARRAY_LEN(lines) && i < h - 4; i++) {
		bool head = lines[i][0] && lines[i][0] != ' ';
		if (head) {
			ui_rgb_fg(n, C_ACCENT);
			ncplane_set_styles(n, NCSTYLE_BOLD);
		} else {
			ncplane_set_fg_default(n);
		}
		ui_put(n, y0 + 2 + i, x0 + 3, w - 6, lines[i]);
		ncplane_set_styles(n, NCSTYLE_NONE);
	}
	ui_default_colors(n);
}

/* ------------------------------------------------------------------------ */
/* render                                                                     */

static void render(app_t *a) {
	ui_t *u = &a->ui;
	notcurses_stddim_yx(u->nc, &u->rows, &u->cols);
	struct ncplane *n = u->std;
	ui_default_colors(n);
	ncplane_erase(n);
	if (u->rows < 6 || u->cols < 30) {
		ui_put(n, 0, 0, (int)u->cols, "terminal too small");
		notcurses_render(u->nc);
		return;
	}
	int cy = -1, cx = -1;
	draw_title(a);
	if (a->phase == PHASE_DETECT) {
		draw_detect(a);
	} else {
		if (u->view_stale)
			ui_view_rebuild(a);
		ui_draw_table(a);
	}
	draw_status(a, &cy, &cx);
	if (u->mode == MODE_PICK || u->mode == MODE_BITS)
		ui_draw_popup(a);
	else if (u->mode == MODE_HELP)
		draw_help(a);
	if (cy >= 0)
		notcurses_cursor_enable(u->nc, cy, cx);
	else
		notcurses_cursor_disable(u->nc);
	notcurses_render(u->nc);
}

void ui_maybe_render(app_t *a, double now) {
	ui_t *u = &a->ui;
	if (a->ps.generation != u->seen_generation) {
		u->seen_generation = a->ps.generation;
		u->view_stale	   = true;
		u->dirty		   = true;
	}
	if (a->ps.revision != u->seen_revision) {
		u->seen_revision = a->ps.revision;
		u->dirty		 = true;
	}
	/* expire the status message */
	if (a->msg[0] && now - a->msg_time >= MSG_SHOW_S && now - a->msg_time < MSG_SHOW_S + 0.2)
		u->dirty = true;
	if (now - u->last_render > 1.0) /* clocks: link state, message expiry */
		u->dirty = true;
	double min_gap = a->phase == PHASE_DETECT ? 0.1 : 0.04;
	if (!u->dirty || now - u->last_render < min_gap)
		return;
	u->dirty	   = false;
	u->last_render = now;
	render(a);
}

/* ------------------------------------------------------------------------ */
/* input                                                                      */

static int page_rows(app_t *a) { return MAX(1, a->ui.nrowpos - 1); }

static void move_sel(app_t *a, int delta) {
	ui_t *u = &a->ui;
	if (u->nview == 0)
		return;
	u->user_moved = true;
	u->sel		  = CLAMP(u->sel + delta, 0, u->nview - 1);
	ui_ensure_visible(a);
}

static void begin_prompt(app_t *a, prompt_kind_t kind) {
	ui_t *u		   = &a->ui;
	u->prompt_kind = kind;
	char def[256];
	if (kind == PROMPT_SAVE)
		app_default_filename(a, def, sizeof def);
	else
		def[0] = '\0';
	le_set(&u->prompt, def);
	u->mode = MODE_PROMPT;
}

static void confirm(app_t *a, confirm_kind_t kind, const char *text) {
	a->ui.confirm_kind = kind;
	xstrlcpy(a->ui.confirm_text, text, sizeof a->ui.confirm_text);
	a->ui.mode = MODE_CONFIRM;
}

static void request_quit(app_t *a) {
	int pend = a->phase == PHASE_VEHICLE ? ps_pending_count(&a->ps) : 0;
	if (pend) {
		char t[128];
		snprintf(t, sizeof t, "%d parameter edit%s not written to the vehicle. Quit anyway?", pend, pend == 1 ? "" : "s");
		confirm(a, CONFIRM_QUIT, t);
	} else {
		a->quit = true;
	}
}

/* The character a key press produced. With the kitty keyboard protocol
 * (which notcurses enables when the terminal supports it) a shifted key
 * arrives as the *base* key plus SHIFT, e.g. Shift+'-' as '-'; the produced
 * text ('_', or 'Й' on a Ukrainian layout) is in eff_text. Legacy terminals
 * send the shifted character itself, where eff_text simply equals id. */
static uint32_t key_text(const ncinput *ni) {
	uint32_t k = ni->id;
	if (nckey_synthesized_p(k) || ncinput_ctrl_p(ni) || ncinput_alt_p(ni))
		return k;
	uint32_t t = ni->eff_text[0];
	if (t != k && t >= 0x20 && t != 0x7f && !nckey_synthesized_p(t))
		return t;
	/* no text reported (notcurses then copies id into eff_text): assume a
	 * US layout for shifted ASCII punctuation */
	if (ncinput_shift_p(ni) && k < 0x80) {
		static const char base[]	= "`1234567890-=[]\\;',./";
		static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
		const char *p				= strchr(base, (int)k);
		if (p && k)
			return (uint32_t)(unsigned char)shifted[p - base];
	}
	return k;
}

static bool is_quit_key(const ncinput *ni) { return ni->id == 0x03 || (ncinput_ctrl_p(ni) && (ni->id == 'c' || ni->id == 'C')); }

static void handle_mouse(app_t *a, const ncinput *ni) {
	ui_t *u = &a->ui;
	if (ni->id == NCKEY_SCROLL_UP || ni->id == NCKEY_SCROLL_DOWN) {
		int d = ni->id == NCKEY_SCROLL_UP ? -3 : 3;
		if (u->mode == MODE_PICK || u->mode == MODE_BITS) {
			ui_pick_key(a, d < 0 ? NCKEY_UP : NCKEY_DOWN);
			return;
		}
		if (u->mode == MODE_NORMAL)
			move_sel(a, d);
		return;
	}
	if (ni->id != NCKEY_BUTTON1 || ni->evtype == NCTYPE_RELEASE)
		return;
	double now = now_s();
	if (u->mode == MODE_PICK || u->mode == MODE_BITS) {
		bool dbl	  = now - u->last_click < 0.4;
		u->last_click = now;
		ui_popup_click(a, ni->y, ni->x, dbl);
		return;
	}
	if (u->mode != MODE_NORMAL)
		return;
	for (int r = 0; r < u->nrowpos; r++) {
		if (ni->y < u->row_y[r] || ni->y >= u->row_y[r] + u->row_h[r])
			continue;
		int vi			  = u->row_view[r];
		u->user_moved	  = true;
		bool dbl		  = vi == u->last_click_row && now - u->last_click < 0.4;
		u->sel			  = vi;
		u->last_click	  = now;
		u->last_click_row = vi;
		int pidx		  = u->view[vi];
		if (ni->x >= u->col_x[5] && u->col_w[5] > 0)
			app_toggle_fav(a, pidx);
		else if (dbl)
			ui_begin_edit(a, pidx);
		return;
	}
}

static void normal_key(app_t *a, uint32_t k) {
	ui_t *u = &a->ui;
	if (a->phase == PHASE_DETECT) {
		if (k == 'q' || k == 'Q' || k == NCKEY_ESC)
			a->quit = true;
		return;
	}
	int sel_param = ui_selected_param(a);
	switch (k) {
	case NCKEY_UP:
	case 'k':
		move_sel(a, -1);
		break;
	case NCKEY_DOWN:
	case 'j':
		move_sel(a, 1);
		break;
	case NCKEY_PGUP:
		move_sel(a, -page_rows(a));
		break;
	case NCKEY_PGDOWN:
		move_sel(a, page_rows(a));
		break;
	case NCKEY_HOME:
	case 'g':
		move_sel(a, -u->nview);
		break;
	case NCKEY_END:
	case 'G':
		move_sel(a, u->nview);
		break;
	case NCKEY_ENTER:
	case '\n':
	case '\r':
	case 'e':
		if (sel_param >= 0)
			ui_begin_edit(a, sel_param);
		break;
	case '/':
		le_set(&u->search, "");
		u->view_stale = true;
		u->mode		  = MODE_SEARCH;
		break;
	case NCKEY_ESC:
		if (u->search.len) {
			le_set(&u->search, "");
			u->view_stale = true;
		} else if (u->modified_only) {
			u->modified_only = false;
			u->view_stale	 = true;
		}
		break;
	case 'f':
	case ' ':
		if (sel_param >= 0)
			app_toggle_fav(a, sel_param);
		break;
	case 'm':
		u->modified_only = !u->modified_only;
		u->view_stale	 = true;
		app_msg(a, MSG_INFO, u->modified_only ? "Showing modified parameters only (m: all)" : "Showing all parameters");
		break;
	case 'w':
	case 'W':
		app_write(a);
		break;
	case 'r':
	case 'R':
		app_refresh(a);
		break;
	case 'z':
		if (sel_param >= 0 && a->ps.v[sel_param].pending) {
			app_clear_pending(a, sel_param);
			app_msg(a, MSG_INFO, "Reverted %s", a->ps.v[sel_param].name);
		}
		break;
	case 'Z':
		if (ps_pending_count(&a->ps))
			confirm(a, CONFIRM_UNDO_ALL, "Discard all parameter edits?");
		break;
	case 'v':
		u->wrap = !u->wrap;
		ui_ensure_visible(a);
		break;
	case 'd':
		u->details = !u->details;
		ui_ensure_visible(a);
		break;
	case 's':
		begin_prompt(a, PROMPT_SAVE);
		break;
	case 'l':
		begin_prompt(a, PROMPT_LOAD);
		break;
	case '?':
	case 'h':
	case NCKEY_F01:
		u->mode = MODE_HELP;
		break;
	case 'q':
	case 'Q':
		request_quit(a);
		break;
	default:
		break;
	}
}

static void search_key(app_t *a, uint32_t k) {
	ui_t *u = &a->ui;
	if (k == NCKEY_ENTER || k == '\n' || k == '\r' || k == NCKEY_DOWN) {
		u->mode = MODE_NORMAL;
	} else if (k == NCKEY_ESC) {
		le_set(&u->search, "");
		u->mode		  = MODE_NORMAL;
		u->view_stale = true;
	} else if (le_key(&u->search, k)) {
		u->view_stale = true;
		u->sel		  = 0;
		u->top		  = 0;
	}
}

static void prompt_key(app_t *a, uint32_t k) {
	ui_t *u = &a->ui;
	if (k == NCKEY_ESC) {
		u->mode = MODE_NORMAL;
	} else if (k == NCKEY_ENTER || k == '\n' || k == '\r') {
		u->mode = MODE_NORMAL;
		char path[sizeof u->prompt.buf];
		xstrlcpy(path, u->prompt.buf, sizeof path);
		char *p = str_trim(path);
		if (!*p)
			return;
		if (u->prompt_kind == PROMPT_SAVE)
			app_save_file(a, p);
		else
			app_load_file(a, p);
	} else {
		le_key(&u->prompt, k);
	}
}

static void confirm_key(app_t *a, uint32_t k) {
	ui_t *u	 = &a->ui;
	bool yes = k == 'y' || k == 'Y' || k == NCKEY_ENTER || k == '\n' || k == '\r';
	bool no	 = k == 'n' || k == 'N' || k == NCKEY_ESC || k == 'q';
	if (!yes && !no)
		return;
	u->mode = MODE_NORMAL;
	if (!yes)
		return;
	switch (u->confirm_kind) {
	case CONFIRM_QUIT:
		a->quit = true;
		break;
	case CONFIRM_UNDO_ALL: {
		int n = app_clear_all_pending(a);
		app_msg(a, MSG_INFO, "Discarded %d edit%s", n, n == 1 ? "" : "s");
		u->view_stale = true;
		break;
	}
	}
}

void ui_handle_input(app_t *a) {
	ui_t *u = &a->ui;
	ncinput ni;
	uint32_t k;
	while ((k = notcurses_get_nblock(u->nc, &ni)) != 0) {
		if (k == (uint32_t)-1)
			break;
		u->dirty = true;
		if (k == NCKEY_RESIZE) {
			notcurses_refresh(u->nc, &u->rows, &u->cols);
			ui_ensure_visible(a);
			continue;
		}
		if (ni.evtype == NCTYPE_RELEASE)
			continue;
		if (nckey_mouse_p(k)) {
			handle_mouse(a, &ni);
			continue;
		}
		k = key_text(&ni);
		/* notcurses reports Ctrl+U as 'U' + modifier: map to the C0 code */
		if (ncinput_ctrl_p(&ni) && k < 0x80 && ((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z')) && k != 'c' && k != 'C')
			k = k & 0x1f;
		if (is_quit_key(&ni)) {
			if (u->mode == MODE_CONFIRM && u->confirm_kind == CONFIRM_QUIT)
				a->quit = true;
			else
				request_quit(a);
			continue;
		}
		switch (u->mode) {
		case MODE_NORMAL:
			normal_key(a, k);
			break;
		case MODE_SEARCH:
			search_key(a, k);
			break;
		case MODE_EDIT:
			ui_edit_key(a, k);
			break;
		case MODE_PICK:
			ui_pick_key(a, k);
			break;
		case MODE_BITS:
			ui_bits_key(a, k);
			break;
		case MODE_PROMPT:
			prompt_key(a, k);
			break;
		case MODE_CONFIRM:
			confirm_key(a, k);
			break;
		case MODE_HELP:
			u->mode = MODE_NORMAL;
			break;
		}
	}
}
