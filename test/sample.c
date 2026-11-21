// SPDX-License-Identifier: GPL-2.0-or-later
/* The boards' sample month (sample.h). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sample.h"

static unsigned rnd(unsigned *s)
{
	*s = *s * 1103515245u + 12345u;
	return *s >> 16 & 0x7fff;
}

void sample_ink(struct field *f, const char *text, double x, double base, unsigned seed)
{
	unsigned s = seed * 2654435761u + 7;
	uint32_t k = f->ns ? f->s[f->ns - 1].k + 1 : 0;
	const char *p = text;
	while (*p) {
		while (*p == ' ')
			p++, x += 7;
		if (!*p)
			break;
		size_t len = strcspn(p, " ");
		/* one stroke per word: a joined run of loops, a little off the line */
		struct pl_point pt[400];
		int n = 0;
		double dy = ((int)(rnd(&s) % 100) - 50) / 50.0 * 1.6, slant = 0.18 + (rnd(&s) % 100) / 600.0;
		for (size_t i = 0; i < len && n < 380; i++) {
			unsigned char ch = p[i];
			double h = strchr("bdfhklt", ch) ? 15 : strchr("gjpqy", ch) ? -6 : 9;
			double w = strchr("mw", ch) ? 11 : strchr("il", ch) ? 4 : 8;
			for (int j = 0; j <= 8 && n < 380; j++) {
				double t = j / 8.0, a = t * 2 * M_PI;
				double yy = h > 0 ? -h * (0.5 - 0.5 * cos(a)) : -9 * (0.5 - 0.5 * cos(a)) - (j > 4 ? -h * sin(a - M_PI) : 0);
				double xx = x + t * w + yy * -slant;
				pt[n++] = (struct pl_point){ (float)xx, (float)(base + dy + yy), 0.55f, (uint32_t)(n * 8) };
			}
			x += w + 1;
		}
		field_add_stroke(f, k++, pt, n);
		p += len;
	}
}

static void put(struct entry *e, const char *text, double base)
{
	sample_ink(&e->f, text, 2, base, e->id * 31u);
	field_set_value(&e->f, text, NULL, 0);
}

void sample_fill(struct store *s)
{
	int m1 = day_from_ymd(2027, 3, 1);
	static const struct { int day, s, e; const char *t; } EV[] = {
		{ 11, 14, 15, "run" }, { 11, 19, 21, "team call" }, { 11, 28, 30, "dentist" }, { 11, 35, 36, "pick up the parcel" },
		{ 11, -1, 0, "bin day" }, { 8, 36, 38, "yoga" }, { 9, 24, 25, "haircut" }, { 10, 32, 33, "call the landlord" },
		{ 12, 40, 44, "cinema" }, { 13, 18, 22, "market" }, { 14, 25, 28, "lunch at home" }, { 9, 17, 18, "dentist check" },
		{ 2, 20, 22, "plumber" }, { 5, 38, 41, "book club" }, { 17, 12, 17, "flight" }, { 19, 39, 44, "concert" },
		{ 23, 30, 31, "eye test" }, { 26, -1, 0, "visitors" }, { 27, -1, 0, "visitors" }, { 30, -1, 0, "rent" },
		{ 22, 19, 20, "team call" }, { 15, 19, 20, "team call" }, { 3, 36, 38, "yoga" }, { 24, 36, 38, "yoga" },
	};
	long long age = 1000;
	for (size_t i = 0; i < sizeof EV / sizeof *EV; i++) {
		struct entry *e = store_add(s, E_EVENT, (struct period){ P_DAY, m1 + EV[i].day - 1 });
		if (EV[i].s < 0) {
			e->allday = true;
			e->start = 0, e->end = 1;
			put(e, EV[i].t, 15);
		} else {
			e->start = EV[i].s * 30;
			e->end = EV[i].e * 30;
			put(e, EV[i].t, 19);
		}
		e->age = age++;
	}
	struct period d = { P_DAY, m1 + 10 }, w = period_of(P_WEEK, m1 + 10), m = { P_MONTH, m1 };
	static const struct { int per; const char *t; int done; } TD[] = {
		{ 0, "call the bank about the card", 0 }, { 0, "buy stamps", 1 }, { 0, "post the letter", 0 },
		{ 1, "book train tickets", 0 }, { 1, "water the plants", 1 }, { 1, "pay the gas bill", 0 },
		{ 1, "return the library books", 0 }, { 1, "fix the bike light", 1 }, { 1, "order printer ink", 0 },
		{ 1, "sort the photos", 0 }, { 2, "renew the passport", 0 }, { 2, "plan the garden", 0 }, { 2, "clear the attic", 0 },
	};
	int rank[3] = { 0 };
	for (size_t i = 0; i < sizeof TD / sizeof *TD; i++) {
		struct entry *e = store_add(s, E_TODO, TD[i].per == 0 ? d : TD[i].per == 1 ? w : m);
		e->rank = ++rank[TD[i].per];
		e->done = TD[i].done;
		e->age = age++;
		put(e, TD[i].t, 24);
	}
	static const struct { int per; const char *slot, *t; } NT[] = {
		{ 0, "morning", "slept well, a slow start|coffee by the window" },
		{ 0, "evening", "a long walk after dinner|read two chapters|early to bed|call back about the shelf" },
		{ 0, "notes", "ask about the parcel|bring the blue folder" },
		{ 1, "notes", "a quiet week|finish the shelf on Sunday" },
		{ 2, "notes", "more walks, less screen|start the seedlings" },
	};
	for (size_t i = 0; i < sizeof NT / sizeof *NT; i++) {
		struct entry *e = store_add(s, E_NOTE, NT[i].per == 0 ? d : NT[i].per == 1 ? w : m);
		strcpy(e->slot, NT[i].slot);
		char buf[200];
		strcpy(buf, NT[i].t);
		double base = 28;
		char value[200] = "";
		for (char *l = strtok(buf, "|"); l; l = strtok(NULL, "|")) {
			sample_ink(&e->f, l, 2, base, e->id * 17u + (unsigned)base);
			if (*value)
				strcat(value, "\n");
			strcat(value, l);
			base += 34;
		}
		field_set_value(&e->f, value, NULL, 0);
		e->age = age++;
	}
	static const struct { int per; const char *t; } PR[] = {
		{ 0, "finish the report" }, { 0, "call the bank" }, { 1, "finish the report" }, { 1, "tax papers" },
		{ 1, "clean the flat" }, { 2, "tax papers" }, { 2, "spring cleaning" }, { 2, "start running again" },
	};
	int pr[3] = { 0 };
	for (size_t i = 0; i < sizeof PR / sizeof *PR; i++) {
		struct entry *e = store_add(s, E_PRIO, PR[i].per == 0 ? d : PR[i].per == 1 ? w : m);
		e->rank = ++pr[PR[i].per];
		e->age = age++;
		put(e, PR[i].t, 24);
	}
	static const struct { const char *t; int marks[12]; } HB[] = {
		{ "read", { 1, 2, 3, 5, 6, 8, 9, 10, 11 } }, { "walk", { 1, 3, 4, 8, 10 } }, { "water", { 2, 5, 9 } },
		{ "no phone in bed", { 1, 2, 4, 5, 6, 7, 8, 9, 10 } },
	};
	for (size_t i = 0; i < sizeof HB / sizeof *HB; i++) {
		struct entry *h = store_add(s, E_HABIT, (struct period){ P_DAY, m1 });
		h->rank = (int)i + 1;
		h->age = age++;
		sample_ink(&h->f, HB[i].t, 2, 21, h->id * 13u);
		field_set_value(&h->f, HB[i].t, NULL, 0);
		for (int j = 0; j < 12 && HB[i].marks[j]; j++) {
			struct entry *mk = store_add(s, E_MARK, (struct period){ P_DAY, m1 + HB[i].marks[j] - 1 });
			mk->habit = h->id;
			mk->age = age++;
		}
	}
}
