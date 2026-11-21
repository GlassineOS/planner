/* SPDX-License-Identifier: GPL-2.0-or-later
 * The person's templates (spec 001 FR-030 to FR-034, FR-045, FR-049): templates made, copied
 * or edited on the device, one file each, `templates/ID.tmpl` beside the entries. A file whose
 * id is a shipped template's replaces it (an edit of a shipped template); any other id is a
 * template of the person's own. The files hold layouts only: nothing here reads or writes an
 * entry. No cairo, no Wayland.
 *
 * The format, one line per node, its depth given by leading spaces (one per level):
 *
 *     # glassine-planner template 1
 *     id own-1
 *     kind day
 *     name "Workdays"
 *     note "A copy of Agenda and lists"
 *     col land=row
 *      row w=3
 *       agenda w=3 grow from=7 to=21
 *       todo period=day title="Today" grow
 *
 * Nodes: row, col, and the widgets agenda, cagenda, todo, notes, habits, minimonth, weekov,
 * priorities. Keys: w (weight), grow, land=row|col (its kind when turned), period=day|week|month,
 * slot, title, from, to (hours), day (a compact agenda's day of the week), large. Quoted values
 * may hold \" and \\. Unknown keys and lines are skipped; a file that gives no valid tree is
 * ignored, so a period whose template is unreadable falls back to its kind's default. */
#ifndef PL_TFILE_H
#define PL_TFILE_H
#include "grid.h"

#define TMPL_ID 40 /* ids fit store's choice (char tmpl[40]); no spaces */

/* a template on the heap: every string and node its own (free with tmpl_free) */
struct tmpl *tmpl_clone(const struct tmpl *t);
void tmpl_free(struct tmpl *t);
struct tnode *tnode_new(enum nkind k);
void tnode_free(struct tnode *n);
/* replace a heap string of a heap template or node (NULL clears) */
void tmpl_set_str(const char **dst, const char *s);

/* text <-> template; NULL on a file without a valid tree */
char *tmpl_to_text(const struct tmpl *t);
struct tmpl *tmpl_from_text(const char *text);

/* the person's templates: loaded from DIR/templates, NULL dir = in memory only */
void tmpl_user_load(const char *dir);
void tmpl_user_clear(void);
const struct tmpl *tmpl_user_find(const char *id);
/* keep t (taken over) as the person's template of its id, written to its file; false when
 * the file couldn't be written (the template is kept in memory all the same) */
bool tmpl_user_put(const char *dir, struct tmpl *t);
/* the person's version of id goes (its file too): a shipped template is as shipped again */
bool tmpl_user_remove(const char *dir, const char *id);
/* a fresh id for a new template or a copy */
void tmpl_new_id(char *out, int len);
/* every template of kind k, shipped ones first (replaced by the person's edit), then the
 * person's own, in the order they were made */
int tmpl_list(enum pkind k, const struct tmpl **out, int max);
bool tmpl_is_shipped(const char *id);

const char *wkind_name(enum wkind k);
int wkind_parse(const char *s);

#endif
