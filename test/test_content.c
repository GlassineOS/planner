// SPDX-License-Identifier: GPL-2.0-or-later
/* What a commit is said to be (FR-062): a tap's answer is a page, a drag's step is motion,
 * each for one commit; the drag's result and what the writing repaints say nothing. The
 * values are the ink protocol's (protocols/glassine-ink-v1.xml). */
#include "../src/content.h"
#include "glassine-ink-v1-client-protocol.h"
#include "t.h"

_Static_assert((int)CONTENT_NONE == GLASSINE_INK_SURFACE_V1_CONTENT_NONE && (int)CONTENT_PAGE == GLASSINE_INK_SURFACE_V1_CONTENT_PAGE &&
		       (int)CONTENT_MOTION == GLASSINE_INK_SURFACE_V1_CONTENT_MOTION,
	       "the kinds are the protocol's values");
_Static_assert(GLASSINE_INK_SURFACE_V1_SET_CONTENT_SINCE_VERSION == 4, "set_content is version 4");

int main(void)
{
	struct content c = { 0 };
	/* a stroke's repaint, the relayout after the pen's pause: nothing said */
	CHECK(content_commit(&c, true) == CONTENT_NONE);

	/* a tap on the bar (the next day, the week view, a template): its commit is a page */
	content_tap(&c);
	CHECK(content_commit(&c, true) == CONTENT_PAGE);
	/* ... that one commit only: the stroke repainted after it says nothing */
	CHECK(content_commit(&c, true) == CONTENT_NONE);

	/* a tap whose frame turns out unchanged: nothing is committed, and the hint is not
	 * carried to the next commit (a stroke's) */
	content_tap(&c);
	CHECK(content_commit(&c, false) == CONTENT_NONE);
	CHECK(content_commit(&c, true) == CONTENT_NONE);

	/* an event dragged: every step's commit is motion */
	for (int i = 0; i < 5; i++) {
		content_step(&c);
		CHECK(content_commit(&c, true) == CONTENT_MOTION);
	}
	/* the finger lifts: the drop's commit says nothing (the panel ends the run with it) */
	content_end(&c);
	CHECK(content_commit(&c, true) == CONTENT_NONE);

	/* a step not yet drawn when the finger lifts: its commit is the result, no step */
	content_step(&c);
	content_end(&c);
	CHECK(content_commit(&c, true) == CONTENT_NONE);

	/* a step and a tap before one commit (a second finger, a key): the page wins */
	content_step(&c);
	content_tap(&c);
	CHECK(content_commit(&c, true) == CONTENT_PAGE);
	content_tap(&c);
	content_step(&c);
	CHECK(content_commit(&c, true) == CONTENT_PAGE);
	/* the end of a drag leaves a page as it is */
	content_tap(&c);
	content_end(&c);
	CHECK(content_commit(&c, true) == CONTENT_PAGE);
	return t_done("content");
}
