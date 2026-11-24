/* The parameter table (Mission Planner "Full Parameter List" columns:
 * Command | Value | Units | Options | Desc | Fav) and the details pane. */
#include <math.h>
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "i18n.h"
#include "ui.h"
#include "util.h"

enum { COL_NAME, COL_VALUE, COL_UNITS, COL_OPTIONS, COL_DESC, COL_FAV, NCOLS };
/* Parameter categories that come from metadata (ArduPilot "user", PX4
 * "category"); listed here only so xgettext extracts them. */
static const char *const meta_categories[]
		__attribute__((unused)) = {N_("Standard"), N_("Advanced"), N_("User"), N_("Developer"), N_("System")};

/* TRANSLATORS: table column titles. Widths: Command 14, Value 12, Units 8, Fav 3 columns. */
static const char *col_titles[NCOLS] = {N_("Command"), N_("Value"), N_("Units"), N_("Options"), N_("Desc"), N_("Fav")};

#define DETAILS_H 6 /* heading + 5 lines */
#define MAX_WRAP 64

/* ------------------------------------------------------------------------ */
/* view: filter + sort                                                        */

typedef struct {
	const paramset_t *ps;
	const char *filter;
} sort_ctx_t;

static int cmp_view(const void *x, const void *y, void *ctx) {
	const sort_ctx_t *c = ctx;
	const param_t *a = &c->ps->v[*(const int *)x], *b = &c->ps->v[*(const int *)y];
	if (c->filter && *c->filter) {
		/* search: parameters whose *name* matches come before description hits */
		bool na = str_icontains(a->name, c->filter), nb = str_icontains(b->name, c->filter);
		if (na != nb)
			return na ? -1 : 1;
	}
	if (a->fav != b->fav)
		return a->fav ? -1 : 1;
	return strcmp(a->name, b->name);
}

static bool matches(const param_t *p, const char *f) {
	if (!f || !*f)
		return true;
	if (str_icontains(p->name, f))
		return true;
	const param_meta_t *m = p->meta;
	/* both languages: users search in whichever they remember */
	return m && (str_icontains(m->human, f) || str_icontains(m->desc, f) || str_icontains(m->human_tr, f) || str_icontains(m->desc_tr, f));
}

int ui_selected_param(app_t *a) {
	ui_t *u = &a->ui;
	if (u->sel < 0 || u->sel >= u->nview)
		return -1;
	return u->view[u->sel];
}

void ui_view_rebuild(app_t *a) {
	ui_t *u							   = &a->ui;
	paramset_t *ps					   = &a->ps;
	int keep						   = ui_selected_param(a);
	char keep_name[PARAM_NAME_LEN + 1] = "";
	if (keep >= 0 && keep < ps->n)
		xstrlcpy(keep_name, ps->v[keep].name, sizeof keep_name);

	if (u->capview < ps->n) {
		u->capview = ps->n + 256;
		u->view	   = xrealloc(u->view, (size_t)u->capview * sizeof *u->view);
	}
	u->nview = 0;
	for (int i = 0; i < ps->n; i++) {
		const param_t *p = &ps->v[i];
		if (!p->have_value)
			continue;
		if (u->modified_only && !p->pending && p->wstate != PW_FAILED)
			continue;
		if (u->group_name[0] && (!p->category || strcmp(p->category, u->group_cat) != 0 || strcmp(p->group, u->group_name) != 0))
			continue;
		if (!matches(p, u->search.buf))
			continue;
		u->view[u->nview++] = i;
	}
	sort_ctx_t sc = {ps, u->search.buf};
	if (u->nview > 1)
		qsort_r(u->view, (size_t)u->nview, sizeof *u->view, cmp_view, &sc);

	u->sel = CLAMP(u->sel, 0, MAX(u->nview - 1, 0));
	if (!u->user_moved) {
		u->sel = 0;
		u->top = 0;
	} else if (keep_name[0]) {
		for (int k = 0; k < u->nview; k++) {
			if (strcmp(ps->v[u->view[k]].name, keep_name) == 0) {
				u->sel = k;
				break;
			}
		}
	}
	u->view_stale = false;
	ui_ensure_visible(a);
}

/* ------------------------------------------------------------------------ */
/* geometry                                                                   */

static bool details_shown(app_t *a) { return a->ui.details && a->ui.rows >= 18; }

static void layout(app_t *a) {
	ui_t *u		 = &a->ui;
	int W		 = (int)u->cols - 1; /* last column: scrollbar */
	int w[NCOLS] = {16, 12, 8, 0, 0, 3};
	int fixed	 = w[COL_NAME] + w[COL_VALUE] + w[COL_UNITS] + w[COL_FAV] + (NCOLS - 1);
	int rest	 = W - fixed;
	if (rest < 24) {
		/* narrow terminal: drop units, then options */
		rest += w[COL_UNITS] + 1;
		w[COL_UNITS] = -1;
	}
	if (rest >= 40) {
		w[COL_OPTIONS] = CLAMP(rest * 30 / 100, 12, 48);
		w[COL_DESC]	   = rest - w[COL_OPTIONS] - 0;
	} else {
		w[COL_OPTIONS] = -1;
		w[COL_DESC]	   = MAX(rest + 1, 0);
	}
	int x = 0;
	for (int c = 0; c < NCOLS; c++) {
		if (w[c] < 0) { /* hidden column, separator is absorbed */
			u->col_x[c] = x;
			u->col_w[c] = 0;
			continue;
		}
		u->col_x[c] = x;
		u->col_w[c] = w[c];
		x += w[c] + 1;
	}
	/* give rounding leftovers to the description */
	int used = u->col_x[COL_FAV] + u->col_w[COL_FAV];
	if (used < W)
		u->col_w[COL_DESC] += W - used, u->col_x[COL_FAV] += W - used;
}

int ui_table_height(app_t *a) {
	ui_t *u = &a->ui;
	int h	= (int)u->rows - 2 /* title + header */ - 1 /* status */;
	if (details_shown(a))
		h -= DETAILS_H;
	return MAX(h, 1);
}

static const char *desc_text(const param_t *p) { return meta_desc(p->meta); }

int ui_row_height(app_t *a, int pidx) {
	ui_t *u = &a->ui;
	if (!u->wrap)
		return 1;
	const param_t *p = &a->ps.v[pidx];
	span_t tmp[1];
	int h = 1;
	if (p->meta) {
		if (u->col_w[COL_OPTIONS] > 0 && p->meta->options)
			h = MAX(h, wrap_text(meta_options(p->meta), u->col_w[COL_OPTIONS], tmp, 0));
		if (u->col_w[COL_DESC] > 0)
			h = MAX(h, wrap_text(desc_text(p), u->col_w[COL_DESC], tmp, 0));
	}
	return MIN(h, ui_table_height(a));
}

void ui_ensure_visible(app_t *a) {
	ui_t *u = &a->ui;
	if (!u->nc)
		return;
	notcurses_stddim_yx(u->nc, &u->rows, &u->cols);
	layout(a);
	if (u->nview == 0) {
		u->sel = u->top = 0;
		return;
	}
	u->sel = CLAMP(u->sel, 0, u->nview - 1);
	u->top = CLAMP(u->top, 0, u->nview - 1);
	if (u->sel < u->top) {
		u->top = u->sel;
		return;
	}
	int avail = ui_table_height(a);
	int sum	  = 0;
	for (int k = u->top; k <= u->sel; k++)
		sum += ui_row_height(a, u->view[k]);
	while (sum > avail && u->top < u->sel) {
		sum -= ui_row_height(a, u->view[u->top]);
		u->top++;
	}
	/* don't leave empty space at the bottom when we could show more */
	while (u->top > 0) {
		int total = 0;
		for (int k = u->top - 1; k < u->nview && total <= avail; k++)
			total += ui_row_height(a, u->view[k]);
		if (total > avail)
			break;
		u->top--;
	}
}

/* ------------------------------------------------------------------------ */
/* drawing                                                                    */

static void draw_separators(struct ncplane *n, ui_t *u, int y) {
	for (int c = 1; c < NCOLS; c++)
		if (u->col_w[c] > 0 && u->col_x[c] > 0)
			ncplane_putstr_yx(n, y, u->col_x[c] - 1, "│");
}

static void draw_header(app_t *a, int y) {
	ui_t *u			  = &a->ui;
	struct ncplane *n = u->std;
	ui_rgb_bg(n, C_HDR_BG);
	ui_rgb_fg(n, C_HDR_FG);
	ncplane_set_styles(n, NCSTYLE_BOLD);
	ui_fill(n, y, 0, (int)u->cols);
	for (int c = 0; c < NCOLS; c++) {
		if (u->col_w[c] <= 0)
			continue;
		const char *t = _(col_titles[c]);
		char buf[64];
		if (c == COL_NAME) {
			snprintf(buf, sizeof buf, "%s ▲", t);
			t = buf;
		}
		ui_put(n, y, u->col_x[c], u->col_w[c], t);
	}
	ncplane_set_styles(n, NCSTYLE_NONE);
	ui_rgb_fg(n, C_GRID);
	draw_separators(n, u, y);
	ui_default_colors(n);
}

static bool out_of_range(const param_t *p, double v) {
	const param_meta_t *m = p->meta;
	return m && m->has_range && (v < m->min - 1e-9 || v > m->max + 1e-9);
}

static void value_text(const param_t *p, char *out, int n, uint32_t *color) {
	double v = p->pending ? p->pending_value : p->value;
	param_format(p, v, out, n);
	*color = 0;
	if (p->wstate == PW_SENT || p->wstate == PW_QUEUED)
		*color = C_SENT;
	else if (p->wstate == PW_FAILED)
		*color = C_FAIL;
	else if (p->pending)
		*color = C_PENDING;
	else if (out_of_range(p, v))
		*color = C_ERR;
}

static void put_wrapped(struct ncplane *n, int y, int x, int w, int h, const char *text) {
	span_t lines[MAX_WRAP];
	int nl = wrap_text(text, w, lines, MAX_WRAP);
	for (int i = 0; i < h; i++) {
		if (i < nl && i < MAX_WRAP)
			ui_putn(n, y + i, x, w, text + lines[i].off, lines[i].len);
		else
			ui_fill(n, y + i, x, w);
	}
}

static void draw_row(app_t *a, int pidx, int y, int h, bool selected) {
	ui_t *u				  = &a->ui;
	struct ncplane *n	  = u->std;
	const param_t *p	  = &a->ps.v[pidx];
	const param_meta_t *m = p->meta;
	uint32_t bg			  = selected ? C_SEL_BG : 0;
#define SET_BG() (selected ? ui_rgb_bg(n, bg) : ncplane_set_bg_default(n))

	SET_BG();
	ncplane_set_fg_default(n);
	for (int i = 0; i < h; i++)
		ui_fill(n, y + i, 0, (int)u->cols);

	/* Command */
	if (p->pending)
		ncplane_set_styles(n, NCSTYLE_BOLD);
	ui_put(n, y, u->col_x[COL_NAME], u->col_w[COL_NAME], p->name);
	ncplane_set_styles(n, NCSTYLE_NONE);

	/* Value */
	char val[48];
	uint32_t vc;
	value_text(p, val, sizeof val, &vc);
	if (vc)
		ui_rgb_fg(n, vc);
	if (p->pending || p->wstate)
		ncplane_set_styles(n, NCSTYLE_BOLD);
	ui_put(n, y, u->col_x[COL_VALUE], u->col_w[COL_VALUE], val);
	ncplane_set_styles(n, NCSTYLE_NONE);
	ncplane_set_fg_default(n);

	/* Units */
	if (u->col_w[COL_UNITS] > 0 && m && m->units) {
		ui_rgb_fg(n, C_DIM);
		ui_put(n, y, u->col_x[COL_UNITS], u->col_w[COL_UNITS], m->units);
	}
	/* Options */
	if (u->col_w[COL_OPTIONS] > 0 && m && m->options) {
		ui_rgb_fg(n, C_OPT);
		if (h > 1)
			put_wrapped(n, y, u->col_x[COL_OPTIONS], u->col_w[COL_OPTIONS], h, meta_options(m));
		else
			ui_put(n, y, u->col_x[COL_OPTIONS], u->col_w[COL_OPTIONS], meta_options(m));
	}
	/* Desc */
	ncplane_set_fg_default(n);
	if (u->col_w[COL_DESC] > 0) {
		if (h > 1)
			put_wrapped(n, y, u->col_x[COL_DESC], u->col_w[COL_DESC], h, desc_text(p));
		else
			ui_put(n, y, u->col_x[COL_DESC], u->col_w[COL_DESC], desc_text(p));
	}
	/* Fav */
	ui_rgb_fg(n, p->fav ? C_PENDING : C_GRID);
	ui_put(n, y, u->col_x[COL_FAV], u->col_w[COL_FAV], p->fav ? "[x]" : "[ ]");

	ui_rgb_fg(n, C_GRID);
	for (int i = 0; i < h; i++)
		draw_separators(n, u, y + i);
	ui_default_colors(n);
#undef SET_BG
}

static void bits_text(const param_meta_t *m, uint64_t bits, char *out, size_t n) {
	size_t len = 0;
	out[0]	   = '\0';
	for (int i = 0; i < m->nbits; i++) {
		if (bits & (1ULL << m->bits[i].bit))
			len = sappendf(out, n, len, "%s%s", len ? " | " : "", meta_blabel(&m->bits[i]));
	}
	if (!len)
		snprintf(out, n, "%s", _("(none)"));
}

static void describe_value(const param_t *p, double v, char *out, size_t n) {
	char num[48];
	param_format(p, v, num, sizeof num);
	const param_meta_t *m = p->meta;
	const char *lab		  = meta_value_label(m, v);
	if (lab) {
		snprintf(out, n, "%s (%s)", num, lab);
	} else if (m && m->nbits && v >= 0) {
		char b[400];
		bits_text(m, (uint64_t)llround(v), b, sizeof b);
		snprintf(out, n, "%s = %s", num, b);
	} else {
		snprintf(out, n, "%s%s%s", num, m && m->units ? " " : "", m && m->units ? m->units : "");
	}
}

static void draw_details(app_t *a, int y0) {
	ui_t *u			  = &a->ui;
	struct ncplane *n = u->std;
	int W			  = (int)u->cols;
	int pidx		  = ui_selected_param(a);

	ui_rgb_bg(n, C_HDR_BG);
	ui_rgb_fg(n, C_HDR_FG);
	ui_fill(n, y0, 0, W);
	for (int i = 1; i < DETAILS_H; i++) {
		ncplane_set_bg_default(n);
		ui_fill(n, y0 + i, 0, W);
	}
	if (pidx < 0) {
		ui_default_colors(n);
		return;
	}
	const param_t *p	  = &a->ps.v[pidx];
	const param_meta_t *m = p->meta;

	/* heading: name, human name, attributes */
	ui_rgb_bg(n, C_HDR_BG);
	char head[256], attrs[256];
	const char *human = meta_human(m);
	snprintf(head, sizeof head, " %s%s%s", p->name, human ? " - " : "", human ? human : "");
	size_t al = (size_t)snprintf(attrs, sizeof attrs, "%s", param_type_name(p->type));
	if (m && m->has_range) {
		char lo[32], hi[32];
		fmt_number(m->min, false, lo, sizeof lo);
		fmt_number(m->max, false, hi, sizeof hi);
		al = sappendf(attrs, sizeof attrs, al, _(" · range %s..%s"), lo, hi);
	}
	if (m && m->has_increment) {
		char inc[32];
		fmt_number(m->increment, false, inc, sizeof inc);
		al = sappendf(attrs, sizeof attrs, al, _(" · step %s"), inc);
	}
	if (m && m->has_default) {
		char d[32];
		fmt_number(m->def, param_type_is_int(p->type), d, sizeof d);
		al = sappendf(attrs, sizeof attrs, al, _(" · default %s"), d);
	}
	if (m && m->user && *m->user)
		al = sappendf(attrs, sizeof attrs, al, " · %s", meta_user_tr(m) ? meta_user_tr(m) : _(m->user));
	if (m && m->reboot_required)
		al = sappendf(attrs, sizeof attrs, al, "%s", _(" · reboot required"));
	if (m && m->readonly)
		al = sappendf(attrs, sizeof attrs, al, "%s", _(" · read-only"));
	if (meta_desc_machine(m) || meta_labels_machine(m))
		al = sappendf(attrs, sizeof attrs, al, "%s", _(" · machine translation"));
	if (p->index >= 0)
		sappendf(attrs, sizeof attrs, al, " · #%d ", p->index);
	int aw = utf8_width(attrs);
	ncplane_set_styles(n, NCSTYLE_BOLD);
	ui_put(n, y0, 0, MAX(W - aw - 1, 1), head);
	ncplane_set_styles(n, NCSTYLE_NONE);
	ui_rgb_fg(n, C_DIM);
	if (aw < W - 20)
		ui_put(n, y0, W - aw, aw, attrs);
	ncplane_set_bg_default(n);

	/* value line */
	int y = y0 + 1;
	char cur[512], line[1200];
	describe_value(p, p->value, cur, sizeof cur);
	ncplane_set_fg_default(n);
	if (p->pending) {
		char pend[512];
		describe_value(p, p->pending_value, pend, sizeof pend);
		snprintf(line, sizeof line, _(" Vehicle: %s   →   new: %s%s"), cur, pend,
				p->wstate == PW_SENT	 ? _("  (writing...)")
				: p->wstate == PW_FAILED ? _("  (write FAILED)")
										 : _("  (not written - press w)"));
		ui_rgb_fg(n, p->wstate == PW_FAILED ? C_FAIL : C_PENDING);
	} else {
		snprintf(line, sizeof line, _(" Value: %s"), cur);
	}
	ui_put(n, y++, 0, W, line);
	ncplane_set_fg_default(n);

	/* description (+ the English original under a translation) + options,
	 * wrapped into the remaining lines */
	int left		 = DETAILS_H - 2;
	const char *desc = desc_text(p);
	const char *opts = m && m->options && *m->options && (m->nvalues || m->nbits) ? meta_options(m) : NULL;
	char orig[2048]	 = "";
	if (meta_desc_translated(m))
		snprintf(orig, sizeof orig, "EN: %s", m->desc && *m->desc ? m->desc : (m->human ? m->human : ""));
	span_t dl[MAX_WRAP], ol[MAX_WRAP], el[MAX_WRAP];
	int ndl = *desc ? wrap_text(desc, W - 2, dl, MAX_WRAP) : 0;
	int nol = opts ? wrap_text(opts, W - 2, ol, MAX_WRAP) : 0;
	int nel = orig[0] ? wrap_text(orig, W - 2, el, MAX_WRAP) : 0;
	/* budget: options keep up to 2 lines (or what they need), the original 1+ */
	int oshow = MIN(nol, 2);
	int eshow = MIN(nel, nel ? 1 : 0);
	int dshow = MIN(ndl, MAX(left - oshow - eshow, 1));
	eshow	  = MIN(nel, MAX(left - dshow - oshow, eshow ? 1 : 0));
	for (int i = 0; i < dshow && i < MAX_WRAP; i++)
		ui_putn(n, y++, 1, W - 2, desc + dl[i].off, dl[i].len);
	ui_rgb_fg(n, C_DIM);
	for (int i = 0; i < eshow && y < y0 + DETAILS_H && i < MAX_WRAP; i++)
		ui_putn(n, y++, 1, W - 2, orig + el[i].off, el[i].len);
	ui_rgb_fg(n, C_OPT);
	for (int i = 0; i < nol && y < y0 + DETAILS_H && i < MAX_WRAP; i++)
		ui_putn(n, y++, 1, W - 2, opts + ol[i].off, ol[i].len);
	if (!m) {
		ui_rgb_fg(n, C_DIM);
		ui_put(n, y, 1, W - 2, a->meta_finished ? _("(no metadata for this parameter)") : _("(metadata loading...)"));
	}
	ui_default_colors(n);
}

void ui_draw_table(app_t *a) {
	ui_t *u			  = &a->ui;
	struct ncplane *n = u->std;
	layout(a);
	int y0		= 2;
	int th		= ui_table_height(a);
	u->table_y0 = y0;
	u->table_y1 = y0 + th;
	draw_header(a, 1);

	u->nrowpos = 0;
	int y	   = y0;
	for (int k = u->top; k < u->nview && y < y0 + th; k++) {
		int pidx = u->view[k];
		int h	 = MIN(ui_row_height(a, pidx), y0 + th - y);
		draw_row(a, pidx, y, h, k == u->sel);
		if (u->nrowpos < UI_MAX_ROWS) {
			u->row_y[u->nrowpos]	= y;
			u->row_h[u->nrowpos]	= h;
			u->row_view[u->nrowpos] = k;
			u->nrowpos++;
		}
		y += h;
	}
	/* empty table area keeps the column grid */
	ui_rgb_fg(n, C_GRID);
	for (; y < y0 + th; y++)
		draw_separators(n, u, y);
	ui_default_colors(n);

	if (u->nview == 0) {
		ui_rgb_fg(n, C_DIM);
		const char *t;
		char buf[200];
		if (a->ps.n == 0)
			t = a->veh.pdl == PDL_FAILED ? _("No parameters received.") : _("Downloading parameters...");
		else if (u->modified_only)
			t = _("No modified parameters (m: show all).");
		else if (u->group_name[0] && !u->search.len)
			t = _("No parameters in this group (Esc: all groups).");
		else {
			snprintf(buf, sizeof buf, _("No parameters match \"%s\" (Esc clears the search)."), u->search.buf);
			t = buf;
		}
		ui_put(n, y0 + 1, 2, (int)u->cols - 4, t);
		ui_default_colors(n);
	}
	/* scrollbar on the right edge */
	if (u->nview > u->nrowpos && th > 2) {
		int bar_h = MAX(1, th * u->nrowpos / u->nview);
		int bar_y = y0 + (th - bar_h) * u->top / MAX(1, u->nview - u->nrowpos);
		ui_rgb_fg(n, C_GRID);
		for (int i = 0; i < bar_h && bar_y + i < y0 + th; i++)
			ncplane_putstr_yx(n, bar_y + i, (int)u->cols - 1, "▐");
		ui_default_colors(n);
	}

	if (details_shown(a))
		draw_details(a, y0 + th);
}
