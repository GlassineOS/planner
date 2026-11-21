// SPDX-License-Identifier: GPL-2.0-or-later
/* Dates, templates, the store and its files, the stand-in reading (src/model.h, src/tmpl.h). */
#include <stdlib.h>
#include <unistd.h>

#include "../src/model.h"
#include "../src/tmpl.h"
#include "t.h"

static void dates(void)
{
	int d = day_from_ymd(2031, 3, 12);
	int y, m, dd;
	day_to_ymd(d, &y, &m, &dd);
	CHECK(y == 2031 && m == 3 && dd == 12);
	CHECK_INT(day_weekday(day_from_ymd(1970, 1, 1)), 3); /* a Thursday */
	CHECK_INT(day_weekday(d), 2);                       /* a Wednesday */
	struct period w = period_of(P_WEEK, d);
	CHECK_INT(w.start, d - 2);
	CHECK_INT(period_end(w), d + 4);
	struct period mo = period_of(P_MONTH, d);
	CHECK_INT(period_end(mo) - mo.start, 30);
	CHECK(period_contains(w, (struct period){ P_DAY, d }));
	CHECK(!period_contains((struct period){ P_DAY, d }, w));
	CHECK_INT(day_parse("2031-02-29"), -1);
	CHECK_INT(days_in_month(2032, 2), 29);
	char b[16];
	day_format(d, b, sizeof b);
	CHECK_STR(b, "2031-03-12");
}

static void templates(void)
{
	struct tmpl t[16];
	char err[200] = "";
	int n = tmpl_load_dir(t, 16, "templates", err, sizeof err);
	CHECK(n >= 4);
	if (n < 0)
		fprintf(stderr, "%s\n", err);
	int days = 0, weeks = 0;
	for (int i = 0; i < n; i++) {
		days += t[i].kind == P_DAY;
		weeks += t[i].kind == P_WEEK;
	}
	CHECK(days >= 2 && weeks >= 2);
	struct tmpl x;
	const char *ok = "name  Test\nkind day\n\ncolumn\n  row weight=2\n    agenda grow from=8 to=18\n    todo title=\"Two words\"\n  notes\n";
	CHECK(tmpl_parse(&x, "t", ok, err, sizeof err));
	CHECK_INT(x.nn, 5);
	CHECK_INT(x.n[0].nkids, 2);
	CHECK_STR(x.n[1].type, "row");
	CHECK_STR(tnode_get(&x.n[3], "title"), "Two words");
	CHECK(tnode_flag(&x.n[2], "grow"));
	CHECK(tnode_num(&x.n[1], "weight", 1) == 2);
	CHECK(!tmpl_parse(&x, "t", "kind day\ncolumn\n   todo\n", err, sizeof err));
	CHECK(strstr(err, "t:3") != NULL);
	CHECK(!tmpl_parse(&x, "t", "kind day\ntodo\n  notes\n", err, sizeof err));
	CHECK(!tmpl_parse(&x, "t", "kind year\ncolumn\n", err, sizeof err));
	CHECK(!tmpl_parse(&x, "t", "kind day\ncolumn\ncolumn\n", err, sizeof err));
}

static void store_files(void)
{
	char dir[] = "out/test-store-XXXXXX";
	CHECK(mkdtemp(dir) != NULL);
	char ep[200], cp[200];
	snprintf(ep, sizeof ep, "%s/entries.txt", dir);
	snprintf(cp, sizeof cp, "%s/choices.txt", dir);
	struct store s;
	store_init(&s);
	int d = day_from_ymd(2031, 3, 12);
	struct entry *e = store_add(&s, E_EVENT, (struct period){ P_DAY, d });
	e->start = 840;
	e->end = 900;
	synth_words(&e->f, 4, 14, 6, 2, 1);
	field_read_stub(&e->f);
	struct entry *t1 = store_add(&s, E_TODO, period_of(P_WEEK, d));
	struct entry *t2 = store_add(&s, E_TODO, period_of(P_WEEK, d));
	CHECK_INT(t1->order, 0);
	CHECK_INT(t2->order, 1);
	t2->done = true;
	snprintf(t2->f.text, sizeof t2->f.text, "call the bank");
	t2->f.edited = true;
	store_choose(&s, period_of(P_WEEK, d), "week-focus");
	CHECK(store_save_entries(&s, ep));
	CHECK(store_save_choices(&s, cp));
	char *a = store_entries_text(&s);
	struct store r;
	store_init(&r);
	CHECK(store_load(&r, ep, cp));
	char *b = store_entries_text(&r);
	CHECK_STR(b, a);
	CHECK_STR(store_choice(&r, period_of(P_WEEK, d + 1)), "week-focus");
	CHECK_STR(store_choice(&r, (struct period){ P_DAY, d }), "day-full");
	CHECK_STR(field_text(&store_find(&r, t2->id)->f), "call the bank");
	struct entry *n = store_add(&r, E_NOTE, (struct period){ P_DAY, d });
	CHECK(n->id > t2->id); /* ids go on after the loaded ones */
	free(a);
	free(b);
	store_free(&s);
	store_free(&r);
}

static void reading(void)
{
	struct field f = { .id = 7 };
	CHECK(field_empty(&f));
	synth_words(&f, 4, 20, 8, 3, 9);
	field_read_stub(&f);
	int words = 1;
	for (const char *c = f.reading; *c; c++)
		words += *c == ' ';
	CHECK_INT(words, 3);
	CHECK_INT(f.read_strokes, f.ns);
	/* a second line: its words come after the first line's */
	synth_words(&f, 4, 54, 8, 2, 10);
	field_read_stub(&f);
	words = 1;
	for (const char *c = f.reading; *c; c++)
		words += *c == ' ';
	CHECK_INT(words, 5);
	double r, b;
	field_extent(&f, &r, &b);
	CHECK(b > 40 && b < 60);
}

int main(void)
{
	dates();
	templates();
	store_files();
	reading();
	return t_done("model");
}
