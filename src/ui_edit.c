/* Value editors: free-text entry, enum picker and bitmask editor. */
#include <math.h>
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "ui.h"
#include "util.h"

/* ------------------------------------------------------------------------ */
/* line editor                                                                */

void le_set(lineedit_t *le, const char *s) {
	xstrlcpy(le->buf, s, sizeof le->buf);
	le->len = (int)strlen(le->buf);
	le->cur = le->len;
}

static int utf8_prev(const lineedit_t *le, int pos) {
	if (pos <= 0)
		return 0;
	pos--;
	while (pos > 0 && ((unsigned char)le->buf[pos] & 0xC0) == 0x80)
		pos--;
	return pos;
}

static int utf8_next(const lineedit_t *le, int pos) {
	if (pos >= le->len)
		return le->len;
	pos++;
	while (pos < le->len && ((unsigned char)le->buf[pos] & 0xC0) == 0x80)
		pos++;
	return pos;
}

static int encode_utf8(uint32_t c, char *out) {
	if (c < 0x80) {
		out[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		out[0] = (char)(0xC0 | (c >> 6));
		out[1] = (char)(0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000) {
		out[0] = (char)(0xE0 | (c >> 12));
		out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		out[2] = (char)(0x80 | (c & 0x3F));
		return 3;
	}
	if (c < 0x110000) {
		out[0] = (char)(0xF0 | (c >> 18));
		out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
		out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
		out[3] = (char)(0x80 | (c & 0x3F));
		return 4;
	}
	return 0;
}

bool le_key(lineedit_t *le, uint32_t k) {
	if (k == NCKEY_BACKSPACE || k == 0x7f || k == 0x08) {
		int p = utf8_prev(le, le->cur);
		memmove(le->buf + p, le->buf + le->cur, (size_t)(le->len - le->cur + 1));
		le->len -= le->cur - p;
		le->cur = p;
		return true;
	}
	if (k == NCKEY_DEL) {
		int nx = utf8_next(le, le->cur);
		memmove(le->buf + le->cur, le->buf + nx, (size_t)(le->len - nx + 1));
		le->len -= nx - le->cur;
		return true;
	}
	if (k == NCKEY_LEFT) {
		le->cur = utf8_prev(le, le->cur);
		return false;
	}
	if (k == NCKEY_RIGHT) {
		le->cur = utf8_next(le, le->cur);
		return false;
	}
	if (k == NCKEY_HOME || k == 0x01) { /* Ctrl-A */
		le->cur = 0;
		return false;
	}
	if (k == NCKEY_END || k == 0x05) { /* Ctrl-E */
		le->cur = le->len;
		return false;
	}
	if (k == 0x15) { /* Ctrl-U */
		le_set(le, "");
		return true;
	}
	if (k < 0x20 || (k >= 0x7f && k < 0xa0) || nckey_synthesized_p(k))
		return false;
	char enc[4];
	int n = encode_utf8(k, enc);
	if (!n || le->len + n >= (int)sizeof le->buf)
		return false;
	memmove(le->buf + le->cur + n, le->buf + le->cur, (size_t)(le->len - le->cur + 1));
	memcpy(le->buf + le->cur, enc, (size_t)n);
	le->cur += n;
	le->len += n;
	return true;
}

/* ------------------------------------------------------------------------ */
/* helpers                                                                    */

static bool is_enter(uint32_t k) { return k == NCKEY_ENTER || k == '\n' || k == '\r'; }

static double edit_start_value(const param_t *p) { return p->pending ? p->pending_value : p->value; }

static void to_text_edit(app_t *a, double v, const char *initial) {
	ui_t *u			 = &a->ui;
	const param_t *p = &a->ps.v[u->edit_param];
	if (initial) {
		le_set(&u->edit, initial);
	} else {
		char buf[48];
		param_format(p, v, buf, sizeof buf);
		le_set(&u->edit, buf);
	}
	u->edit_force  = false;
	u->edit_err[0] = '\0';
	u->mode		   = MODE_EDIT;
}

static void commit(app_t *a, double v) {
	ui_t *u	   = &a->ui;
	param_t *p = &a->ps.v[u->edit_param];
	char before[48], after[48];
	param_format(p, p->value, before, sizeof before);
	param_format(p, v, after, sizeof after);
	u->mode = MODE_NORMAL;
	if (app_set_pending(a, u->edit_param, v))
		app_msg(a, MSG_INFO, "%s: %s → %s  (w: write to vehicle, z: undo)", p->name, before, after);
	else if (p->wstate == PW_NONE)
		app_msg(a, MSG_INFO, "%s unchanged", p->name);
	u->view_stale = true; /* modified-only view may change */
}

void ui_begin_edit(app_t *a, int pidx) {
	ui_t *u	   = &a->ui;
	param_t *p = &a->ps.v[pidx];
	if (!p->have_value)
		return;
	if (p->wstate == PW_SENT || p->wstate == PW_QUEUED) {
		app_msg(a, MSG_WARN, "%s is being written", p->name);
		return;
	}
	const param_meta_t *m = p->meta;
	if (m && m->readonly) {
		app_msg(a, MSG_WARN, "%s is read-only", p->name);
		return;
	}
	u->edit_param  = pidx;
	u->edit_force  = false;
	u->edit_err[0] = '\0';
	double cur	   = edit_start_value(p);
	if (m && m->nvalues > 0) {
		u->mode		= MODE_PICK;
		u->pick_sel = 0;
		for (int i = 0; i < m->nvalues; i++)
			if (fabs(m->values[i].code - cur) < 1e-6)
				u->pick_sel = i;
		u->pick_top = 0;
	} else if (m && m->nbits > 0 && param_type_is_int(p->type) && cur >= 0) {
		u->mode		= MODE_BITS;
		u->bits		= (uint64_t)llround(cur);
		u->pick_sel = 0;
		u->pick_top = 0;
	} else {
		to_text_edit(a, cur, NULL);
	}
}

/* ------------------------------------------------------------------------ */
/* text editor                                                                */

static void commit_text(app_t *a) {
	ui_t *u				  = &a->ui;
	param_t *p			  = &a->ps.v[u->edit_param];
	const param_meta_t *m = p->meta;
	char buf[sizeof u->edit.buf];
	xstrlcpy(buf, u->edit.buf, sizeof buf);
	char *s = str_trim(buf);
	char *end;
	double v = strtod(s, &end);
	if (!*s || *end || !isfinite(v)) {
		snprintf(u->edit_err, sizeof u->edit_err, "not a number");
		return;
	}
	if (param_type_is_int(p->type)) {
		if (fabs(v - rint(v)) > 1e-9) {
			snprintf(u->edit_err, sizeof u->edit_err, "%s needs an integer", param_type_name(p->type));
			return;
		}
		v = rint(v);
	}
	double lo, hi;
	param_type_limits(p->type, &lo, &hi);
	if (v < lo || v > hi) {
		char a1[32], a2[32];
		fmt_number(lo, true, a1, sizeof a1);
		fmt_number(hi, true, a2, sizeof a2);
		snprintf(u->edit_err, sizeof u->edit_err, "%s range is %s..%s", param_type_name(p->type), a1, a2);
		return;
	}
	if (m && m->has_range && (v < m->min - 1e-9 || v > m->max + 1e-9) && !u->edit_force) {
		char a1[32], a2[32];
		fmt_number(m->min, false, a1, sizeof a1);
		fmt_number(m->max, false, a2, sizeof a2);
		snprintf(u->edit_err, sizeof u->edit_err, "outside recommended range %s..%s - Enter again to set anyway", a1, a2);
		u->edit_force = true;
		return;
	}
	commit(a, v);
}

static void step_value(app_t *a, int dir) {
	ui_t *u				  = &a->ui;
	const param_t *p	  = &a->ps.v[u->edit_param];
	const param_meta_t *m = p->meta;
	double inc			  = m && m->has_increment && m->increment > 0 ? m->increment : param_type_is_int(p->type) ? 1 : 0;
	if (inc <= 0)
		return;
	double v = strtod(u->edit.buf, NULL) + dir * inc;
	if (m && m->has_range)
		v = CLAMP(v, m->min, m->max);
	/* avoid 0.30000000000000004-style noise */
	v = rint(v / inc) * inc;
	char buf[48];
	param_format(p, v, buf, sizeof buf);
	le_set(&u->edit, buf);
	u->edit_force  = false;
	u->edit_err[0] = '\0';
}

void ui_edit_key(app_t *a, uint32_t k) {
	ui_t *u				  = &a->ui;
	const param_t *p	  = &a->ps.v[u->edit_param];
	const param_meta_t *m = p->meta;
	if (k == NCKEY_ESC) {
		u->mode = MODE_NORMAL;
	} else if (is_enter(k)) {
		commit_text(a);
	} else if (k == NCKEY_TAB && m && (m->nvalues || m->nbits)) {
		double v	   = strtod(u->edit.buf, NULL);
		u->edit_err[0] = '\0';
		if (m->nvalues) {
			u->mode = MODE_PICK;
			for (int i = 0; i < m->nvalues; i++)
				if (fabs(m->values[i].code - v) < 1e-6)
					u->pick_sel = i;
		} else {
			u->mode = MODE_BITS;
			u->bits = v >= 0 ? (uint64_t)llround(v) : 0;
		}
	} else if (k == NCKEY_UP) {
		step_value(a, 1);
	} else if (k == NCKEY_DOWN) {
		step_value(a, -1);
	} else if (le_key(&u->edit, k)) {
		u->edit_force  = false;
		u->edit_err[0] = '\0';
	}
}

/* ------------------------------------------------------------------------ */
/* list popups                                                                */

static int popup_count(app_t *a) {
	const param_meta_t *m = a->ps.v[a->ui.edit_param].meta;
	if (!m)
		return 0;
	return a->ui.mode == MODE_PICK ? m->nvalues : m->nbits;
}

static int popup_visible(app_t *a) { return MAX(1, a->ui.popup_h - 4); }

static void list_nav(app_t *a, uint32_t k) {
	ui_t *u	 = &a->ui;
	int n	 = popup_count(a);
	int page = popup_visible(a);
	switch (k) {
	case NCKEY_UP:
	case 'k':
		u->pick_sel--;
		break;
	case NCKEY_DOWN:
	case 'j':
		u->pick_sel++;
		break;
	case NCKEY_PGUP:
		u->pick_sel -= page;
		break;
	case NCKEY_PGDOWN:
		u->pick_sel += page;
		break;
	case NCKEY_HOME:
	case 'g':
		u->pick_sel = 0;
		break;
	case NCKEY_END:
	case 'G':
		u->pick_sel = n - 1;
		break;
	default:
		break;
	}
	u->pick_sel = CLAMP(u->pick_sel, 0, MAX(n - 1, 0));
}

void ui_pick_key(app_t *a, uint32_t k) {
	ui_t *u				  = &a->ui;
	const param_t *p	  = &a->ps.v[u->edit_param];
	const param_meta_t *m = p->meta;
	if (u->mode == MODE_BITS) {
		ui_bits_key(a, k);
		return;
	}
	if (!m || !m->nvalues) {
		u->mode = MODE_NORMAL;
		return;
	}
	if (k == NCKEY_ESC || k == 'q') {
		u->mode = MODE_NORMAL;
	} else if (is_enter(k) || k == ' ') {
		commit(a, m->values[u->pick_sel].code);
	} else if (k == NCKEY_TAB || k == 'e') {
		to_text_edit(a, m->values[u->pick_sel].code, NULL);
	} else if ((k >= '0' && k <= '9') || k == '-' || k == '.') {
		char s[2] = {(char)k, 0};
		to_text_edit(a, 0, s);
	} else {
		list_nav(a, k);
	}
}

void ui_bits_key(app_t *a, uint32_t k) {
	ui_t *u				  = &a->ui;
	const param_t *p	  = &a->ps.v[u->edit_param];
	const param_meta_t *m = p->meta;
	if (!m || !m->nbits) {
		u->mode = MODE_NORMAL;
		return;
	}
	if (k == NCKEY_ESC || k == 'q') {
		u->mode = MODE_NORMAL;
	} else if (is_enter(k)) {
		commit(a, (double)u->bits);
	} else if (k == ' ' || k == 'x') {
		u->bits ^= 1ULL << m->bits[u->pick_sel].bit;
	} else if (k == 'a') {
		for (int i = 0; i < m->nbits; i++)
			u->bits |= 1ULL << m->bits[i].bit;
	} else if (k == 'n') {
		u->bits = 0;
	} else if (k == NCKEY_TAB || k == 'e') {
		to_text_edit(a, (double)u->bits, NULL);
	} else {
		list_nav(a, k);
	}
}

void ui_draw_popup(app_t *a) {
	ui_t *u				  = &a->ui;
	struct ncplane *n	  = u->std;
	const param_t *p	  = &a->ps.v[u->edit_param];
	const param_meta_t *m = p->meta;
	if (!m)
		return;
	bool bits = u->mode == MODE_BITS;
	int count = popup_count(a);

	int maxw = 0;
	for (int i = 0; i < count; i++)
		maxw = MAX(maxw, utf8_width(bits ? m->bits[i].label : m->values[i].label));
	char title[128];
	snprintf(title, sizeof title, "%s%s%s", p->name, m->human ? " - " : "", m->human ? m->human : "");
	const char *foot = bits ? "Space toggle · a all · n none · Enter set · Tab type · Esc" : "Enter select · Tab type a value · Esc cancel";
	int w			 = MAX(maxw + 22, MAX(utf8_width(title) + 6, utf8_width(foot) + 4));
	w				 = MIN(w, (int)u->cols - 4);
	int h			 = MIN(count + 4, (int)u->rows - 4);
	h				 = MAX(h, 5);
	int y0 = ((int)u->rows - h) / 2, x0 = ((int)u->cols - w) / 2;
	u->popup_y0	   = y0;
	u->popup_x0	   = x0;
	u->popup_h	   = h;
	u->popup_w	   = w;
	u->popup_first = y0 + 1;
	ui_box(n, y0, x0, h, w, title);

	int vis = popup_visible(a);
	if (u->pick_sel < u->pick_top)
		u->pick_top = u->pick_sel;
	if (u->pick_sel >= u->pick_top + vis)
		u->pick_top = u->pick_sel - vis + 1;
	u->pick_top = CLAMP(u->pick_top, 0, MAX(0, count - vis));

	double vehicle_v = p->value;
	for (int r = 0; r < vis && u->pick_top + r < count; r++) {
		int i	 = u->pick_top + r;
		bool sel = i == u->pick_sel;
		ui_rgb_bg(n, sel ? C_SEL_BG : C_POPUP_BG);
		ncplane_set_fg_default(n);
		char line[512];
		if (bits) {
			bool on = (u->bits >> m->bits[i].bit) & 1;
			snprintf(line, sizeof line, " %s %2d  %s", on ? "[x]" : "[ ]", m->bits[i].bit, m->bits[i].label);
			if (on)
				ui_rgb_fg(n, C_PENDING);
		} else {
			char code[32];
			fmt_number(m->values[i].code, false, code, sizeof code);
			bool is_cur = fabs(m->values[i].code - vehicle_v) < 1e-6;
			snprintf(line, sizeof line, " %s %6s  %s%s", sel ? "▶" : " ", code, m->values[i].label, is_cur ? "   ← current" : "");
			if (is_cur)
				ui_rgb_fg(n, C_OK);
		}
		ui_put(n, u->popup_first + r, x0 + 1, w - 2, line);
	}
	/* footer */
	ui_rgb_bg(n, C_POPUP_BG);
	ui_rgb_fg(n, C_DIM);
	char f[256];
	if (bits)
		snprintf(f, sizeof f, " value = %llu   %s", (unsigned long long)u->bits, foot);
	else
		snprintf(f, sizeof f, " %s", foot);
	ui_put(n, y0 + h - 2, x0 + 1, w - 2, f);
	if (count > vis) {
		char more[32];
		snprintf(more, sizeof more, " %d/%d ", u->pick_sel + 1, count);
		ui_rgb_fg(n, C_ACCENT);
		ui_put(n, y0 + h - 1, x0 + w - 2 - utf8_width(more), utf8_width(more), more);
	}
	ui_default_colors(n);
}

void ui_popup_click(app_t *a, int y, int x, bool dbl) {
	ui_t *u = &a->ui;
	if (y < u->popup_y0 || y >= u->popup_y0 + u->popup_h || x < u->popup_x0 || x >= u->popup_x0 + u->popup_w) {
		u->mode = MODE_NORMAL; /* click outside cancels */
		return;
	}
	int r = y - u->popup_first;
	if (r < 0 || r >= popup_visible(a))
		return;
	int i = u->pick_top + r;
	if (i >= popup_count(a))
		return;
	bool same	= i == u->pick_sel;
	u->pick_sel = i;
	if (u->mode == MODE_BITS)
		ui_bits_key(a, ' ');
	else if (dbl && same)
		ui_pick_key(a, NCKEY_ENTER);
}
