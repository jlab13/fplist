/* Localisation via GNU gettext.
 *   _("text")      translate now
 *   N_("text")     mark for extraction only (static tables), translate on use
 *   ngettext(s, p, n) plural forms
 * Built without NLS (-DGNDTUI_NLS=OFF) these collapse to the English text. */
#pragma once

#include <stddef.h>

#ifdef GNDTUI_NLS
#include <libintl.h>
#define _(s) gettext(s)
#else
#define _(s) (s)
#define ngettext(s, p, n) ((n) == 1 ? (s) : (p))
#endif
#define N_(s) (s)

/* Bind the "gndtui" text domain. Looks for catalogs in $GNDTUI_LOCALEDIR,
 * then next to the executable (build tree: <exe dir>/locale), then in the
 * install prefix. Call after setlocale(). */
void i18n_init(void);

/* Language for parameter metadata translations, as "ll_CC" (PX4 file naming),
 * derived from $LANGUAGE / LC_MESSAGES. Empty for C/POSIX/English. */
void i18n_meta_lang(char *out, size_t n);
