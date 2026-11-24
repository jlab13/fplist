/* notcurses user interface: Mission-Planner-like full parameter list. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct app;
struct notcurses;
struct ncplane;

typedef enum {
	MODE_NORMAL,
	MODE_SEARCH,
	MODE_EDIT,	  /* free text value entry */
	MODE_PICK,	  /* choose from enumerated values */
	MODE_BITS,	  /* toggle bitmask bits */
	MODE_PROMPT,  /* file name entry */
	MODE_CONFIRM, /* y/n question */
	MODE_HELP,
} ui_mode_t;

typedef enum { PROMPT_SAVE, PROMPT_LOAD } prompt_kind_t;
typedef enum { CONFIRM_QUIT, CONFIRM_UNDO_ALL } confirm_kind_t;

typedef struct lineedit {
	char buf[512];
	int len; /* bytes */
	int cur; /* byte offset */
} lineedit_t;

#define UI_MAX_ROWS 512

typedef struct ui {
	struct notcurses *nc;
	struct ncplane *std;
	unsigned rows, cols;
	bool dirty;
	double last_render;
	unsigned seen_revision, seen_generation;

	/* table view: indices into the paramset, filtered + sorted */
	int *view;
	int nview, capview;
	bool view_stale;
	int sel;		 /* index into view */
	int top;		 /* first visible view row */
	bool user_moved; /* until the user navigates, stay at the top while loading */
	bool wrap;
	bool details;
	bool modified_only;
	lineedit_t search;

	ui_mode_t mode;

	/* editing */
	int edit_param; /* paramset index */
	lineedit_t edit;
	bool edit_force; /* out-of-range confirmed once */
	char edit_err[160];
	int pick_sel, pick_top;
	uint64_t bits;

	prompt_kind_t prompt_kind;
	lineedit_t prompt;
	confirm_kind_t confirm_kind;
	char confirm_text[200];

	/* geometry from the last render, for mouse hit testing */
	int table_y0, table_y1;
	int nrowpos;
	int row_y[UI_MAX_ROWS], row_h[UI_MAX_ROWS], row_view[UI_MAX_ROWS];
	int col_x[6], col_w[6];
	int popup_y0, popup_x0, popup_h, popup_w, popup_first; /* list popups */
	double last_click;
	int last_click_row;
} ui_t;

bool ui_init(struct app *a);
void ui_shutdown(struct app *a);
int ui_input_fd(struct app *a);
void ui_handle_input(struct app *a);
/* Render if something changed (rate limited). */
void ui_maybe_render(struct app *a, double now);
void ui_invalidate(struct app *a);

/* ui_table.c */
void ui_view_rebuild(struct app *a);
void ui_draw_table(struct app *a);
void ui_ensure_visible(struct app *a);
int ui_table_height(struct app *a);
int ui_row_height(struct app *a, int param_idx);
int ui_selected_param(struct app *a); /* paramset index or -1 */

/* ui_edit.c */
void le_set(lineedit_t *le, const char *s);
bool le_key(lineedit_t *le, uint32_t key); /* true if consumed */
void ui_begin_edit(struct app *a, int param_idx);
void ui_edit_key(struct app *a, uint32_t key);
void ui_pick_key(struct app *a, uint32_t key);
void ui_bits_key(struct app *a, uint32_t key);
void ui_draw_popup(struct app *a);
void ui_popup_click(struct app *a, int y, int x, bool dbl);

/* drawing helpers shared by ui*.c */
void ui_fill(struct ncplane *n, int y, int x, int w);
void ui_put(struct ncplane *n, int y, int x, int w, const char *s);
void ui_putn(struct ncplane *n, int y, int x, int w, const char *s, int len);
void ui_rgb_fg(struct ncplane *n, uint32_t rgb);
void ui_rgb_bg(struct ncplane *n, uint32_t rgb);
void ui_default_colors(struct ncplane *n);
void ui_box(struct ncplane *n, int y, int x, int h, int w, const char *title);

/* palette */
#define C_TITLE_BG 0x1b5e20
#define C_TITLE_FG 0xffffff
#define C_HDR_BG 0x333333
#define C_HDR_FG 0xe8e8e8
#define C_SEL_BG 0x264f78
#define C_GRID 0x5a5a5a
#define C_DIM 0x9e9e9e
#define C_OPT 0xb8b8b8
#define C_PENDING 0xffd54f
#define C_SENT 0x4fc3f7
#define C_FAIL 0xef5350
#define C_OK 0x81c784
#define C_WARN 0xffb74d
#define C_ERR 0xe57373
#define C_POPUP_BG 0x202830
#define C_ACCENT 0x8bc34a
