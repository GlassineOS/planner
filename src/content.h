// SPDX-License-Identifier: GPL-2.0-or-later
/* What the planner says a commit of its surface is (FR-062), for the panel's choice of
 * refresh (glassine_ink_surface_v1.set_content): the answer to a tap on a control is a new
 * page, shown once and exact; a step of a drag is motion, shown fast, and the first commit
 * after the drag that is no step ends it. What the writing changes says nothing.
 * GLASSINE_PLANNER_CONTENT=off: nothing is said at all (to compare the two on a device). */
#ifndef PL_CONTENT_H
#define PL_CONTENT_H

#include <stdbool.h>

/* the values of glassine_ink_surface_v1.content */
enum content_kind { CONTENT_NONE = 0, CONTENT_PAGE = 1, CONTENT_MOTION = 2 };

struct content {
	enum content_kind next; /* what the next commit is */
};

/* a tap on a control was answered and repaints (a view, a period, a template, a popover) */
void content_tap(struct content *c);
/* a drag moved by a step and repaints (an event moved or stretched, the editor's marker) */
void content_step(struct content *c);
/* the drag ended or was cancelled: what is drawn next is its result, no step */
void content_end(struct content *c);
/* a commit is made now (damaged: it has damage, so it is sent): what it is. Forgets it:
 * the commits after it say nothing until told again. A page wins over a step */
enum content_kind content_commit(struct content *c, bool damaged);

#endif
