// SPDX-License-Identifier: GPL-2.0-or-later
/* When the page is laid out again (FR-046): a relayout the writing asks for waits until the
 * pen is up and the person paused; one a tap on a control asks for is done at once, and
 * leaves a relayout the writing asked for before it waiting. */
#ifndef PL_SETTLE_H
#define PL_SETTLE_H

#include <stdbool.h>

struct settle {
	bool asked; /* a relayout is wanted */
	bool held;  /* during a tap: the one the writing asked for before it */
};

/* a tap begins: what it asks for is told apart from what was asked before */
void settle_tap_begin(struct settle *s);
/* the tap's end: true when the tap asked for a relayout: lay out now (everything, so then
 * settle_done). Else what the writing asked for before the tap is still asked */
bool settle_tap_end(struct settle *s);
/* laid out: nothing is asked any more */
void settle_done(struct settle *s);
/* the writing's relayout is due: asked, the pen up and the pause (ms) past since it lifted;
 * true clears it (lay out now) */
bool settle_due(struct settle *s, bool pen_down, long long now, long long pen_up, long long pause);

#endif
