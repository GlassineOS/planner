// SPDX-License-Identifier: GPL-2.0-or-later
#include "content.h"

void content_tap(struct content *c)
{
	c->next = CONTENT_PAGE;
}

void content_step(struct content *c)
{
	if (c->next != CONTENT_PAGE)
		c->next = CONTENT_MOTION;
}

void content_end(struct content *c)
{
	if (c->next == CONTENT_MOTION)
		c->next = CONTENT_NONE;
}

enum content_kind content_commit(struct content *c, bool damaged)
{
	enum content_kind k = damaged ? c->next : CONTENT_NONE;
	c->next = CONTENT_NONE;
	return k;
}
