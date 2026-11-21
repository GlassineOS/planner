// SPDX-License-Identifier: GPL-2.0-or-later
#include "settle.h"

void settle_tap_begin(struct settle *s)
{
	s->held = s->asked;
	s->asked = false;
}

bool settle_tap_end(struct settle *s)
{
	bool now = s->asked;
	s->asked = s->asked || s->held;
	s->held = false;
	return now;
}

bool settle_due(struct settle *s, bool pen_down, long long now, long long pen_up, long long pause)
{
	if (!s->asked || pen_down || now - pen_up < pause)
		return false;
	s->asked = false;
	return true;
}

void settle_done(struct settle *s)
{
	s->asked = false;
}
