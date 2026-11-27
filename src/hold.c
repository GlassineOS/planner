// SPDX-License-Identifier: GPL-2.0-or-later
#include <math.h>

#include "hold.h"

void hold_begin(struct hold *h, uint32_t id, double x, double y, long long now)
{
	*h = (struct hold){ id, x, y, now, false };
}

bool hold_move(struct hold *h, double x, double y)
{
	if (!h->id || h->fired)
		return h->id != 0;
	if (fabs(x - h->x) > HOLD_SLOP || fabs(y - h->y) > HOLD_SLOP) {
		h->id = 0;
		return false;
	}
	return true;
}

long long hold_left(const struct hold *h, long long now)
{
	if (!h->id || h->fired)
		return -1;
	long long left = h->t + HOLD_MS - now;
	return left > 0 ? left : 0;
}

bool hold_fire(struct hold *h, long long now)
{
	if (hold_left(h, now) != 0)
		return false;
	h->fired = true;
	return true;
}

void hold_end(struct hold *h)
{
	*h = (struct hold){ 0 };
}
