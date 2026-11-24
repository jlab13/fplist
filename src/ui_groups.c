/* Parameter group chooser, QGroundControl style: Category -> Group.
 * 'g' opens it, typing filters the list, Enter applies the group as a
 * table filter. Categories/groups come from ps_assign_groups(). */
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "i18n.h"
#include "ui.h"
#include "util.h"

/* ------------------------------------------------------------------------ */
/* labels                                                                     */

static const char *cat_label(const char *category, const char *tr) { return tr ? tr : _(category); }

static const char *group_label(const char *group, const char *tr) {
	if (tr)
		return tr;
	return strcmp(group, PARAM_GROUP_MISC) == 0 ? _("Misc") : group;
}

/* ------------------------------------------------------------------------ */
/* building the entry list                                                    */

static int cat_rank(const char *c) {
	if (strcmp(c, "Standard") == 0)
		return 0;
	if (strcmp(c, PARAM_CATEGORY_OTHER) == 0)
		return 2;
	return 1;
}

static int cmp_param_group(const void *x, const void *y, void *ctx) {
	const paramset_t *ps = ctx;
	const param_t *a = &ps->v[*(const int *)x], *b = &ps->v[*(const int *)y];
	int r = cat_rank(a->category) - cat_rank(b->category);
	if (r)
		return r;
	if ((r = strcmp(a->category, b->category)))
		return r;
	bool ma = strcmp(a->group, PARAM_GROUP_MISC) == 0, mb = strcmp(b->group, PARAM_GROUP_MISC) == 0;
	if (ma != mb)
		return ma ? 1 : -1; /* Misc last */
	if ((r = strcasecmp(a->group, b->group)))
		return r;
	return strcmp(a->name, b->name);
}

static ui_group_entry_t *push(ui_t *u) {
	if (u->ngent == u->capgent) {
		u->capgent = u->capgent ? u->capgent * 2 : 128;
		u->gent	   = xrealloc(u->gent, (size_t)u->capgent * sizeof *u->gent);
	}
	ui_group_entry_t *e = &u->gent[u->ngent++];
	memset(e, 0, sizeof *e);
	return e;
}

static void build_entries(app_t *a) {
	ui_t *u		   = &a->ui;
	paramset_t *ps = &a->ps;
	u->ngent	   = 0;
	int *idx	   = xmalloc((size_t)MAX(ps->n, 1) * sizeof *idx);
	int n		   = 0;
	for (int i = 0; i < ps->n; i++)
		if (ps->v[i].have_value && ps->v[i].category)
			idx[n++] = i;
	qsort_r(idx, (size_t)n, sizeof *idx, cmp_param_group, ps);

	ui_group_entry_t *all = push(u);
	all->kind			  = GE_ALL;
	all->count			  = n;
	int cat_entry = -1, grp_entry = -1;
	for (int k = 0; k < n; k++) {
		const param_t *p = &ps->v[idx[k]];
		if (cat_entry < 0 || strcmp(u->gent[cat_entry].category, p->category) != 0) {
			ui_group_entry_t *c = push(u);
			c->kind				= GE_CATEGORY;
			c->category			= p->category;
			c->category_tr		= meta_user_tr(p->meta);
			cat_entry			= u->ngent - 1;
			grp_entry			= -1;
		}
		if (grp_entry < 0 || strcmp(u->gent[grp_entry].group, p->group) != 0) {
			ui_group_entry_t *g = push(u);
			g->kind				= GE_GROUP;
			g->category			= p->category;
			g->category_tr		= u->gent[cat_entry].category_tr;
			xstrlcpy(g->group, p->group, sizeof g->group);
			g->group_tr = meta_group_tr(p->meta);
			grp_entry	= u->ngent - 1;
		}
		u->gent[cat_entry].count++;
		u->gent[grp_entry].count++;
	}
	free(idx);
}

static bool selectable(const ui_group_entry_t *e) { return e->kind != GE_CATEGORY; }

static bool group_matches(const ui_group_entry_t *e, const char *f) {
	return str_icontains(e->group, f) || (e->group_tr && str_icontains(e->group_tr, f)) || str_icontains(group_label(e->group, NULL), f);
}

static bool cat_matches(const ui_group_entry_t *e, const char *f) {
	return str_icontains(e->category, f) || str_icontains(cat_label(e->category, e->category_tr), f);
}

/* Recompute the visible rows for the current filter text. */
static void refilter(app_t *a) {
	ui_t *u		  = &a->ui;
	const char *f = u->gsearch.buf;
	u->gvis		  = xrealloc(u->gvis, (size_t)MAX(u->ngent, 1) * sizeof *u->gvis);
	u->ngvis	  = 0;
	for (int i = 0; i < u->ngent; i++) {
		const ui_group_entry_t *e = &u->gent[i];
		if (e->kind == GE_ALL || !*f) {
			u->gvis[u->ngvis++] = i;
			continue;
		}
		if (e->kind != GE_CATEGORY)
			continue;
		/* a matching category shows all its groups; otherwise only matching groups */
		bool whole			= cat_matches(e, f);
		int head			= u->ngvis;
		u->gvis[u->ngvis++] = i;
		int shown			= 0;
		for (int j = i + 1; j < u->ngent && u->gent[j].kind == GE_GROUP; j++)
			if (whole || group_matches(&u->gent[j], f)) {
				u->gvis[u->ngvis++] = j;
				shown++;
			}
		if (!shown)
			u->ngvis = head; /* drop the empty heading */
	}
	u->gsel = CLAMP(u->gsel, 0, MAX(u->ngvis - 1, 0));
	if (u->ngvis && !selectable(&u->gent[u->gvis[u->gsel]])) {
		while (u->gsel < u->ngvis - 1 && !selectable(&u->gent[u->gvis[u->gsel]]))
			u->gsel++;
	}
}

/* ------------------------------------------------------------------------ */
/* actions                                                                    */

void ui_groups_open(app_t *a) {
	ui_t *u = &a->ui;
	if (a->phase != PHASE_VEHICLE)
		return;
	build_entries(a);
	le_set(&u->gsearch, "");
	u->gsel = 0;
	u->gtop = 0;
	refilter(a);
	/* preselect the active group */
	for (int k = 0; k < u->ngvis && u->group_name[0]; k++) {
		const ui_group_entry_t *e = &u->gent[u->gvis[k]];
		if (e->kind == GE_GROUP && strcmp(e->category, u->group_cat) == 0 && strcmp(e->group, u->group_name) == 0) {
			u->gsel = k;
			break;
		}
	}
	u->mode = MODE_GROUPS;
}

void ui_groups_clear_filter(app_t *a) {
	ui_t *u			 = &a->ui;
	u->group_cat[0]	 = '\0';
	u->group_name[0] = '\0';
	u->view_stale	 = true;
	u->sel = u->top = 0;
}

static void apply(app_t *a) {
	ui_t *u = &a->ui;
	if (!u->ngvis)
		return;
	const ui_group_entry_t *e = &u->gent[u->gvis[u->gsel]];
	if (e->kind == GE_CATEGORY)
		return;
	if (e->kind == GE_ALL) {
		ui_groups_clear_filter(a);
	} else {
		xstrlcpy(u->group_cat, e->category, sizeof u->group_cat);
		xstrlcpy(u->group_name, e->group, sizeof u->group_name);
		u->view_stale = true;
		u->sel = u->top = 0;
		u->user_moved	= false;
	}
	u->mode = MODE_NORMAL;
}

static void move(app_t *a, int delta) {
	ui_t *u = &a->ui;
	if (!u->ngvis)
		return;
	int dir = delta < 0 ? -1 : 1;
	int s	= CLAMP(u->gsel + delta, 0, u->ngvis - 1);
	/* skip category headings in the direction of travel, then the other way */
	while (s >= 0 && s < u->ngvis && !selectable(&u->gent[u->gvis[s]]))
		s += dir;
	if (s < 0 || s >= u->ngvis) {
		s = CLAMP(u->gsel + delta, 0, u->ngvis - 1);
		while (s >= 0 && s < u->ngvis && !selectable(&u->gent[u->gvis[s]]))
			s -= dir;
	}
	if (s >= 0 && s < u->ngvis)
		u->gsel = s;
}

static int visible_rows(app_t *a) { return MAX(1, a->ui.gpop_h - 3); }

void ui_groups_scroll(app_t *a, int delta) { move(a, delta); }

void ui_groups_key(app_t *a, uint32_t k) {
	ui_t *u	 = &a->ui;
	int page = visible_rows(a);
	switch (k) {
	case NCKEY_ENTER:
	case '\n':
	case '\r':
		apply(a);
		return;
	case NCKEY_ESC:
		if (u->gsearch.len) { /* first Esc clears the filter, second closes */
			le_set(&u->gsearch, "");
			refilter(a);
		} else {
			u->mode = MODE_NORMAL;
		}
		return;
	case NCKEY_UP:
		move(a, -1);
		return;
	case NCKEY_DOWN:
		move(a, 1);
		return;
	case NCKEY_PGUP:
		move(a, -page);
		return;
	case NCKEY_PGDOWN:
		move(a, page);
		return;
	case NCKEY_HOME:
		u->gsel = 0;
		return;
	case NCKEY_END:
		u->gsel = MAX(u->ngvis - 1, 0);
		move(a, 0);
		return;
	default:
		break;
	}
	/* everything else edits the filter: type to filter */
	if (le_key(&u->gsearch, k)) {
		u->gsel = 0;
		refilter(a);
		if (u->gsearch.len)
			move(a, 1); /* first matching group rather than "all" */
	}
}

void ui_groups_click(app_t *a, int y, int x) {
	ui_t *u = &a->ui;
	if (y < u->gpop_y0 || y >= u->gpop_y0 + u->gpop_h || x < u->gpop_x0 || x >= u->gpop_x0 + u->gpop_w) {
		u->mode = MODE_NORMAL; /* click outside closes */
		return;
	}
	int r = y - u->gpop_first;
	if (r < 0 || r >= visible_rows(a) || u->gtop + r >= u->ngvis)
		return;
	u->gsel = u->gtop + r;
	if (selectable(&u->gent[u->gvis[u->gsel]]))
		apply(a);
}

void ui_groups_label(app_t *a, char *out, size_t n) {
	ui_t *u = &a->ui;
	out[0]	= '\0';
	if (!u->group_name[0])
		return;
	const char *ctr = NULL, *gtr = NULL;
	for (int i = 0; i < a->ps.n; i++) {
		const param_t *p = &a->ps.v[i];
		if (p->category && strcmp(p->category, u->group_cat) == 0 && strcmp(p->group, u->group_name) == 0) {
			ctr = meta_user_tr(p->meta);
			gtr = meta_group_tr(p->meta);
			break;
		}
	}
	snprintf(out, n, "%s › %s", cat_label(u->group_cat, ctr), group_label(u->group_name, gtr));
}

/* ------------------------------------------------------------------------ */
/* drawing                                                                    */

void ui_groups_draw(app_t *a, int *cursor_y, int *cursor_x) {
	ui_t *u			  = &a->ui;
	struct ncplane *n = u->std;
	int W = (int)u->cols, H = (int)u->rows;

	int maxw = utf8_width(_("All parameters"));
	for (int i = 0; i < u->ngent; i++) {
		const ui_group_entry_t *e = &u->gent[i];
		const char *l			  = e->kind == GE_CATEGORY ? cat_label(e->category, e->category_tr) : group_label(e->group, e->group_tr);
		maxw					  = MAX(maxw, utf8_width(l));
	}
	const char *hint = _("type to filter · Enter select · Esc close");
	int w			 = MAX(maxw + 16, utf8_width(hint) + 8);
	w				 = CLAMP(w, 30, W - 4);
	int h			 = CLAMP(u->ngvis + 3, 8, H - 4); /* shrinks with the filter */
	int y0 = (H - h) / 2, x0 = (W - w) / 2;
	u->gpop_y0	  = y0;
	u->gpop_x0	  = x0;
	u->gpop_h	  = h;
	u->gpop_w	  = w;
	u->gpop_first = y0 + 1;
	ui_box(n, y0, x0, h, w, _("Parameter groups"));

	int vis = visible_rows(a);
	if (u->gsel < u->gtop)
		u->gtop = u->gsel;
	if (u->gsel >= u->gtop + vis)
		u->gtop = u->gsel - vis + 1;
	u->gtop = CLAMP(u->gtop, 0, MAX(0, u->ngvis - vis));
	/* keep the category heading of the first visible group on screen */
	if (u->gtop > 0 && u->gent[u->gvis[u->gtop]].kind == GE_GROUP && u->gent[u->gvis[u->gtop - 1]].kind == GE_CATEGORY &&
			u->gsel < u->gtop + vis - 1)
		u->gtop--;

	for (int r = 0; r < vis; r++) {
		int y = u->gpop_first + r;
		ui_rgb_bg(n, C_POPUP_BG);
		ncplane_set_fg_default(n);
		if (u->gtop + r >= u->ngvis) {
			ui_fill(n, y, x0 + 1, w - 2);
			continue;
		}
		int k					  = u->gtop + r;
		const ui_group_entry_t *e = &u->gent[u->gvis[k]];
		bool sel				  = k == u->gsel;
		bool active				  = e->kind == GE_GROUP ? (strcmp(e->category, u->group_cat) == 0 && strcmp(e->group, u->group_name) == 0)
									: e->kind == GE_ALL ? !u->group_name[0]
														: false;
		char cnt[16];
		snprintf(cnt, sizeof cnt, "%d ", e->count);
		int cw = utf8_width(cnt);
		char line[256];
		if (sel)
			ui_rgb_bg(n, C_SEL_BG);
		if (e->kind == GE_CATEGORY) {
			ui_rgb_fg(n, C_ACCENT);
			ncplane_set_styles(n, NCSTYLE_BOLD);
			snprintf(line, sizeof line, " %s", cat_label(e->category, e->category_tr));
		} else if (e->kind == GE_ALL) {
			ncplane_set_styles(n, NCSTYLE_BOLD);
			snprintf(line, sizeof line, " %s %s", active ? "●" : " ", _("All parameters"));
		} else {
			if (active)
				ui_rgb_fg(n, C_PENDING);
			snprintf(line, sizeof line, "   %s %s", active ? "●" : " ", group_label(e->group, e->group_tr));
		}
		ui_put(n, y, x0 + 1, w - 2 - cw, line);
		ncplane_set_styles(n, NCSTYLE_NONE);
		ui_rgb_fg(n, C_DIM);
		ui_put(n, y, x0 + w - 1 - cw, cw, cnt);
	}

	/* footer: filter line or hints */
	int fy = y0 + h - 2;
	ui_rgb_bg(n, C_POPUP_BG);
	/* the filter line is always active: "> text" with the cursor, or a hint */
	ncplane_set_fg_default(n);
	ui_put(n, fy, x0 + 1, 3, " > ");
	int cx = x0 + 4;
	if (u->gsearch.len) {
		ui_put(n, fy, cx, w - 5, u->gsearch.buf);
	} else {
		ui_rgb_fg(n, C_DIM);
		ui_put(n, fy, cx, w - 5, hint);
	}
	char before[sizeof u->gsearch.buf];
	memcpy(before, u->gsearch.buf, (size_t)u->gsearch.cur);
	before[u->gsearch.cur] = '\0';
	*cursor_y			   = fy;
	*cursor_x			   = cx + MIN(utf8_width(before), w - 6);
	ui_default_colors(n);
}
