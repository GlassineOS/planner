// SPDX-License-Identifier: GPL-2.0-or-later
/* The store: dates, periods, the iCalendar round trip, template choices, search, and the
 * layout solver's minimums (spec 001). */
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/grid.h"
#include "sample.h"
#include "t.h"

static char *slurp(const char *p)
{
	FILE *f = fopen(p, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	rewind(f);
	char *b = malloc(n + 1);
	b[fread(b, 1, n, f)] = 0;
	fclose(f);
	return b;
}

int main(void)
{
	/* dates */
	int d = day_from_ymd(2027, 3, 11), y, m, dd;
	day_to_ymd(d, &y, &m, &dd);
	CHECK(y == 2027 && m == 3 && dd == 11);
	CHECK_INT(day_weekday(d), 3);
	CHECK_INT(day_weekday(day_from_ymd(2027, 3, 1)), 0);
	CHECK_INT(week_number(d), 10);
	CHECK_INT(week_number(day_from_ymd(2027, 1, 1)), 53);
	CHECK_INT(days_in_month(2028, 2), 29);
	struct period w = period_of(P_WEEK, d);
	CHECK_INT(w.start, day_from_ymd(2027, 3, 8));
	CHECK_INT(period_end(w), day_from_ymd(2027, 3, 14));
	CHECK_INT(period_step(period_of(P_MONTH, d), 1).start, day_from_ymd(2027, 4, 1));

	/* round trip through the files */
	char dir[] = "out/test-store-XXXXXX";
	mkdir("out", 0755);
	CHECK(mkdtemp(dir) != NULL);
	struct store s;
	store_init(&s, dir);
	sample_fill(&s);
	int n0 = s.n;
	CHECK(store_save(&s));
	char *ics = store_ics(&s);
	struct store r;
	store_init(&r, dir);
	CHECK(store_load(&r));
	CHECK_INT(r.n, n0);
	char *ics2 = store_ics(&r);
	CHECK_STR(ics2, ics);
	struct entry *e[64];
	int k = store_list(&r, E_TODO, period_of(P_DAY, d), NULL, e, 64);
	CHECK_INT(k, 3);
	CHECK(k == 3 && e[1]->done);
	CHECK(k && e[0]->f.ns > 0 && e[0]->f.value && !strcmp(e[0]->f.value, "call the bank about the card"));
	k = store_events(&r, d, e, 64);
	CHECK_INT(k, 5);

	/* switching templates leaves the entries byte for byte (FR-028) */
	char path[128];
	snprintf(path, sizeof path, "%s/planner.ics", dir);
	char *before = slurp(path);
	store_choose(&r, period_of(P_DAY, d), d, "day-diary");
	CHECK(store_save_choices(&r));
	CHECK(store_save(&r));
	char *after = slurp(path);
	CHECK(before && after && !strcmp(before, after));
	int how = -1;
	CHECK_STR(store_template(&r, period_of(P_DAY, d), &how), "day-diary");
	CHECK_INT(how, 0);
	store_template(&r, period_of(P_DAY, d + 1), &how);
	CHECK_INT(how, 3);
	store_choose(&r, period_of(P_DAY, d), -1, "day-agenda");
	CHECK_STR(store_template(&r, period_of(P_DAY, d + 40), &how), "day-agenda");
	CHECK_INT(how, 2);
	CHECK(store_save_choices(&r));
	struct store r2;
	store_init(&r2, dir);
	store_load(&r2);
	CHECK_STR(store_template(&r2, period_of(P_DAY, d), NULL), "day-diary");
	CHECK_STR(store_template(&r2, period_of(P_DAY, d + 3), NULL), "day-agenda");

	/* search */
	k = store_search(&r, "BANK", e, 64);
	CHECK(k >= 2);
	k = store_search(&r, "team call", e, 64);
	CHECK_INT(k, 3);

	/* deleting, and the mark of a habit */
	struct entry *x = store_add(&r, E_NOTE, period_of(P_DAY, d));
	uint32_t id = x->id;
	store_delete(&r, id);
	CHECK(store_get(&r, id) == NULL);

	/* the solver gives every widget at least its minimum, upright and turned */
	for (int i = 0; i < pl_ntemplates; i++)
		for (int land = 0; land < 2; land++) {
			struct grid g;
			grid_solve(pl_templates[i], land ? 912 : 678, land ? 578 : 812, land, NULL, NULL, &g);
			CHECK(g.n > 0);
			for (int j = 0; j < g.n; j++)
				CHECK(g.p[j].w >= 80 && g.p[j].h >= 60);
		}

	free(ics), free(ics2), free(before), free(after);
	store_free(&s), store_free(&r), store_free(&r2);
	return t_done("store");
}
