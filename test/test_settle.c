// SPDX-License-Identifier: GPL-2.0-or-later
/* When the page is laid out again (FR-046): a pen tap on a control (the bar, a date, a
 * popover) at once, also right after writing; the writing's own relayout only after the
 * pen's pause, and a tap that asks for nothing doesn't bring it forward. */
#include "../src/settle.h"
#include "t.h"

#define PAUSE 1200

int main(void)
{
	struct settle s = { 0 };
	/* nothing asked: nothing due */
	CHECK(!settle_due(&s, false, 5000, 0, PAUSE));

	/* writing asks (a new row): not while the pen is down, nor within the pause */
	s.asked = true;
	CHECK(!settle_due(&s, true, 1000, 0, PAUSE));
	CHECK(!settle_due(&s, false, 1100, 1000, PAUSE));
	CHECK(s.asked);
	/* after the pause: due once */
	CHECK(settle_due(&s, false, 2200, 1000, PAUSE));
	CHECK(!s.asked);
	CHECK(!settle_due(&s, false, 2300, 1000, PAUSE));

	/* a pen tap on the bar (next day) 100 ms after the pen lifted from a word: at once */
	settle_tap_begin(&s);
	s.asked = true; /* the tap's action: relayout_later() */
	CHECK(settle_tap_end(&s));
	settle_done(&s);
	CHECK(!s.asked);
	CHECK(!settle_due(&s, false, 5000, 1000, PAUSE)); /* and no second relayout later */

	/* writing asked, then a tap on a control: the tap lays out everything, at once */
	s.asked = true;
	settle_tap_begin(&s);
	CHECK(!s.asked);
	s.asked = true;
	CHECK(settle_tap_end(&s));
	settle_done(&s);
	CHECK(!s.asked);

	/* writing asked, then a tap that asks for nothing (a dot beside a field): the writing's
	 * relayout keeps its pause */
	s.asked = true;
	settle_tap_begin(&s);
	CHECK(!settle_tap_end(&s));
	CHECK(s.asked);
	CHECK(!settle_due(&s, false, 1500, 1000, PAUSE));
	CHECK(settle_due(&s, false, 2300, 1000, PAUSE));

	/* a tap that asks for nothing, nothing asked before: nothing asked after */
	settle_tap_begin(&s);
	CHECK(!settle_tap_end(&s));
	CHECK(!s.asked);
	return t_done("settle");
}
