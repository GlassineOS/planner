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

	/* two instances on one data directory: neither save loses the other's entries; an entry
	 * deleted in one stays deleted (the device run's empty store after a stroke) */
	{
		char d2[] = "out/test-two-XXXXXX";
		CHECK(mkdtemp(d2) != NULL);
		struct store a, b, c;
		store_init(&a, d2);
		store_init(&b, d2);
		CHECK(store_load(&a) && store_load(&b)); /* both start on an empty directory */
		struct entry *t = store_add(&a, E_TODO, period_of(P_DAY, d));
		sample_ink(&t->f, "buy milk", 2, 27, 5);
		char uid[48];
		snprintf(uid, sizeof uid, "%s", t->uid);
		CHECK(store_save(&a));       /* a's first stroke, kept at once */
		CHECK(store_save(&b));       /* b, with nothing of its own, saves later */
		store_init(&c, d2);
		store_load(&c);
		CHECK_INT(c.n, 1);
		CHECK(c.n == 1 && !strcmp(c.e[0]->uid, uid) && c.e[0]->f.ns == t->f.ns);
		store_free(&c);
		/* b took a's entry in; b's own entry and a's later one both survive */
		CHECK_INT(b.n, 1);
		struct entry *u = store_add(&b, E_NOTE, period_of(P_DAY, d));
		snprintf(u->slot, sizeof u->slot, "notes");
		sample_ink(&u->f, "rain", 2, 27, 6);
		CHECK(store_save(&b));
		struct entry *h = store_add(&a, E_HABIT, period_of(P_DAY, d));
		struct entry *mk = store_add(&a, E_MARK, period_of(P_DAY, d));
		mk->habit = h->id;
		CHECK(store_save(&a));
		CHECK_INT(a.n, 4);
		CHECK(store_save(&b));
		CHECK_INT(b.n, 4);
		struct entry *bm = NULL, *bh = NULL;
		for (int i = 0; i < b.n; i++)
			bm = b.e[i]->kind == E_MARK ? b.e[i] : bm, bh = b.e[i]->kind == E_HABIT ? b.e[i] : bh;
		CHECK(bm && bh && bm->habit == bh->id); /* the mark still names its habit */
		/* a deletes the to-do; a's later saves leave it out even when b's copy is in the file
		 * (b, unaware, may write it again: an entry is never lost by a merge) */
		store_delete(&a, t->id);
		CHECK(store_save(&a));
		CHECK(store_save(&b));
		CHECK(store_save(&a));
		store_init(&c, d2);
		store_load(&c);
		bool back = false;
		for (int i = 0; i < c.n; i++)
			back |= !strcmp(c.e[i]->uid, uid);
		CHECK(!back);
		CHECK_INT(c.n, 3);
		store_free(&c);
		store_free(&a), store_free(&b);
	}

	/* a field's region (the device run's lost stroke): the first stroke makes the row's
	 * entry; a value that takes its only stroke away (read as a strike) empties and deletes
	 * it; the next strokes, before the page settles again, make the entry anew and are kept */
	{
		struct store a;
		store_init(&a, NULL);
		struct fmake mk = { E_TODO, period_of(P_WEEK, d), "", 3 };
		struct pl_point pt[2] = { { 2, 20, 0.5f, 0 }, { 40, 21, 0.5f, 8 } };
		uint32_t ent = 0;
		struct entry *x = field_stroke(&a, &ent, 0, &mk, 1, pt, 2);
		CHECK(x && ent == x->id && x->kind == E_TODO && x->rank == 3 && x->f.ns == 1);
		CHECK(!field_value(&a, &ent, 0, "", NULL, 0, NULL, 0, NULL, 0)); /* a reading not ready yet */
		CHECK_INT(a.n, 1);
		uint32_t g1 = 1;
		CHECK(field_value(&a, &ent, 0, "", NULL, 0, &g1, 1, NULL, 0));
		CHECK_INT(ent, 0);
		CHECK_INT(a.n, 0);
		x = field_stroke(&a, &ent, 0, &mk, 2, pt, 2);
		CHECK(x && ent && a.n == 1 && x->f.ns == 1 && x->per.kind == P_WEEK);
		CHECK(!field_value(&a, &ent, 0, "milk", NULL, 0, NULL, 0, "st", 2));
		CHECK(x->f.value && !strcmp(x->f.value, "milk") && x->f.state_len == 2);
		/* an event keeps its time when its title is struck out */
		struct entry *ev = store_add(&a, E_EVENT, period_of(P_DAY, d));
		uint32_t eid = ev->id;
		field_stroke(&a, &eid, 0, NULL, 1, pt, 2);
		CHECK(!field_value(&a, &eid, 0, "", NULL, 0, &g1, 1, NULL, 0) && store_get(&a, eid));
		/* a title region makes nothing when its event is gone */
		store_delete(&a, eid);
		struct fmake none = { E_NKINDS };
		CHECK(field_stroke(&a, &eid, 0, &none, 3, pt, 2) == NULL);
		store_free(&a);
	}

	/* a text-mode edit: the value's layout keeps the unchanged words' ink and sets the
	 * edited one in Autograph; it survives the file, with neat strokes and the opt-ins */
	{
		struct store a;
		store_init(&a, dir);
		struct entry *t = store_add(&a, E_TODO, period_of(P_DAY, d));
		sample_ink(&t->f, "call the bonk", 2, 27, 9);
		uint32_t ent = t->id, k0 = t->f.s[0].k, k1 = t->f.s[1].k, k2 = t->f.s[2].k;
		float path[] = { 0, 60, 20, 1, 90, 20, 1, 90, 30, 4 };
		struct pl_word w[3] = {
			{ 0, 4, false, &k0, 1, 0, 0, { 0 }, NULL, 0 },
			{ 5, 3, false, &k1, 1, 0, 0, { 0 }, NULL, 0 },
			{ 9, 4, true, NULL, 0, 0, 0, { 60, 12, 30, 18 }, path, 10 },
		};
		CHECK(!field_value(&a, &ent, 0, "call the bank", w, 3, &k2, 1, "s", 1));
		CHECK_INT(t->f.ns, 2);
		CHECK_INT(t->f.nw, 3);
		CHECK_INT(field_word_of(&t->f, k1), 1);
		CHECK_INT(field_word_of(&t->f, k2), -1);
		struct pl_point np[2] = { { 3, 20, 0.5f, 0 }, { 30, 20, 0.5f, 4 } };
		CHECK(field_set_neat(&t->f, k0, np, 2));
		/* the region telling it all again (a restored field): nothing changes, its ink's
		 * version neither */
		uint32_t gen = t->f.gen;
		CHECK(gen != 0);
		CHECK(!field_set_neat(&t->f, k0, np, 2));
		CHECK(field_value_same(&a, ent, 0, "call the bank", w, 3, 0, "s", 1));
		CHECK(!field_value_same(&a, ent, 0, "call the bank", w, 3, 1, "s", 1)); /* a stroke gone */
		CHECK(!field_value_same(&a, ent, 0, "call the bonk", w, 3, 0, "s", 1));
		CHECK(!field_value_same(&a, ent, 0, "call the bank", w, 2, 0, "s", 1));
		CHECK(!field_value_same(&a, ent, 0, "call the bank", w, 3, 0, "t", 1));
		CHECK_INT(t->f.gen, gen);
		np[1].y = 21;
		CHECK(field_set_neat(&t->f, k0, np, 2));
		CHECK(t->f.gen != gen);
		np[1].y = 20;
		field_set_neat(&t->f, k0, np, 2);
		t->f.neaten = SHOW_ON;
		a.neaten = false;
		CHECK(field_neaten(&a, &t->f) && !field_autograph(&a, &t->f));
		char *x1 = store_ics(&a);
		struct store b;
		store_init(&b, NULL);
		CHECK(store_parse_ics(&b, x1));
		char *x2 = store_ics(&b);
		CHECK_STR(x2, x1);
		CHECK(b.n == 1 && b.e[0]->f.nw == 3 && b.e[0]->f.w[2].set && b.e[0]->f.w[2].npath == 10 &&
		      b.e[0]->f.w[2].box[0] == 60 && b.e[0]->f.nneat == 1 && b.e[0]->f.neaten == SHOW_ON);
		free(x1), free(x2);
		/* text typed into an empty row makes its entry */
		struct fmake mk = { E_NOTE, period_of(P_DAY, d), "evening", 0 };
		uint32_t ne = 0;
		CHECK(field_typed(&b, &ne, 0, &mk, "") == NULL);
		struct entry *y = field_typed(&b, &ne, 0, &mk, "rain all day");
		CHECK(y && ne == y->id && !strcmp(y->slot, "evening") && !strcmp(y->f.value, "rain all day"));
		/* the settings */
		a.neaten = true, a.strength = 0.35f, a.autograph = true, a.min_lane = 200;
		CHECK(store_save_settings(&a));
		struct store c;
		store_init(&c, dir);
		store_load(&c);
		CHECK(c.neaten && c.autograph && c.strength > 0.34f && c.strength < 0.36f && c.min_lane == 200);
		store_free(&a), store_free(&b), store_free(&c);
	}

	/* the journal (FR-024): each stroke kept at once without writing planner.ics, the file
	 * written later; nothing another instance journalled is lost */
	{
		char jd[] = "out/test-journal-XXXXXX";
		CHECK(mkdtemp(jd) != NULL);
		const int DEAD = 0x3ffffff0; /* an instance that ended without saving */
		struct pl_point pt[2] = { { 3, 20, 0.5f, 0 }, { 30, 22, 0.5f, 8 } };
		struct fmake mk = { E_TODO, period_of(P_DAY, d), "", 1 };
		/* a stroke noted, then a crash: the next start has it, the latest note winning */
		struct store a;
		store_init(&a, jd);
		a.jid = DEAD;
		uint32_t ea = 0;
		CHECK(field_stroke(&a, &ea, 0, &mk, 0, pt, 2) != NULL);
		CHECK(store_note(&a, ea));
		CHECK(field_stroke(&a, &ea, 0, &mk, 1, pt, 2) != NULL);
		CHECK(store_note(&a, ea));
		char jp[256];
		snprintf(jp, sizeof jp, "%s/planner.ics", jd);
		CHECK(access(jp, F_OK) != 0); /* planner.ics not written per stroke */
		struct store b;
		store_init(&b, jd);
		b.jid = (int)getppid(); /* another instance, running */
		store_load(&b);
		CHECK_INT(b.n, 1);
		CHECK(b.n == 1 && b.e[0]->f.ns == 2 && !strcmp(b.e[0]->uid, store_get(&a, ea)->uid));
		/* b's save folds the ended instance's journal into planner.ics and removes it */
		CHECK(store_save(&b));
		snprintf(jp, sizeof jp, "%s/planner.journal.%d", jd, DEAD);
		CHECK(access(jp, F_OK) != 0);
		struct store c;
		store_init(&c, jd);
		store_load(&c);
		CHECK(c.n == 1 && c.e[0]->f.ns == 2);
		store_free(&c);
		/* a running instance's stroke on an entry another one knows: that one's save keeps
		 * the journal, so the stroke survives the running one crashing later */
		struct store r;
		store_init(&r, jd);
		r.jid = (int)getpid();
		store_load(&r);
		uint32_t er = r.e[0]->id;
		CHECK(field_stroke(&r, &er, 0, NULL, 7, pt, 2) != NULL);
		CHECK(store_note(&r, er));
		CHECK(store_save(&b)); /* b knows the entry: its two strokes */
		snprintf(jp, sizeof jp, "%s/planner.journal.%d", jd, (int)getpid());
		CHECK(access(jp, F_OK) == 0);
		store_init(&c, jd);
		store_load(&c);
		CHECK(c.n == 1 && c.e[0]->f.ns == 3);
		store_free(&c);
		/* its own save folds it in and removes its journal */
		CHECK(store_save(&r));
		CHECK(access(jp, F_OK) != 0);
		/* a struck-out row: the emptied entry is deleted and saved whole (a delete is never
		 * journalled), so a restart doesn't bring it back */
		uint32_t gone[3] = { 0, 1, 7 };
		CHECK(field_value(&r, &er, 0, "", NULL, 0, gone, 3, NULL, 0));
		CHECK(er == 0);
		CHECK(store_save(&r));
		store_init(&c, jd);
		store_load(&c);
		CHECK_INT(c.n, 0);
		store_free(&c);
		store_free(&r);
		store_free(&a);
		store_free(&b);
	}

	/* a choice removed: its period falls back to the range, then the default; an entry moved
	 * to another day keeps its ink */
	{
		struct store a;
		store_init(&a, NULL);
		struct period w = period_of(P_WEEK, d);
		store_choose(&a, period_of(P_WEEK, d - 14), period_end(period_of(P_WEEK, d + 14)), "week-spread");
		store_choose(&a, w, w.start, "week-lists");
		int how;
		CHECK_STR(store_template(&a, w, &how), "week-lists");
		store_unchoose(&a, 1);
		CHECK_STR(store_template(&a, w, &how), "week-spread");
		CHECK_INT(how, 1);
		store_unchoose(&a, 0);
		store_template(&a, w, &how);
		CHECK_INT(how, 3);
		struct entry *t = store_add(&a, E_EVENT, period_of(P_DAY, d));
		t->start = 600, t->end = 660;
		sample_ink(&t->f, "dentist", 2, 19, 1);
		CHECK(store_move(&a, t->id, d + 3));
		CHECK(t->per.start == d + 3 && t->start == 600 && t->f.ns == 1);
		CHECK(!store_move(&a, t->id, d + 3)); /* the same day: nothing to move */
		struct entry *mt = store_add(&a, E_TODO, period_of(P_MONTH, d));
		CHECK(store_move(&a, mt->id, d + 40) && mt->per.kind == P_MONTH && mt->per.start == period_of(P_MONTH, d + 40).start);
		store_free(&a);
	}

	/* an event's page (FR-018, board event.open): its place, notes and participants written,
	 * its repeat and its reminder chosen, all kept through planner.ics and the journal; a
	 * page field never makes an entry */
	{
		struct store a;
		store_init(&a, NULL);
		struct entry *t = store_add(&a, E_EVENT, period_of(P_DAY, d));
		CHECK_INT(t->remind, -1);
		t->start = 14 * 60, t->end = 15 * 60, t->repeat = REP_WEEKLY, t->remind = 15;
		sample_ink(&t->f, "dentist", 2, 19, 1);
		sample_ink(entry_sub(t, EF_WHERE), "by the station", 2, 25, 2);
		sample_ink(entry_sub(t, EF_NOTES), "bring the card", 2, 25, 3);
		uint32_t id = t->id;
		CHECK(!field_value(&a, &id, EF_WITH, "the dentist", NULL, 0, NULL, 0, NULL, 0) && id == t->id);
		CHECK(entry_sub(t, EF_TITLE) == &t->f && entry_sub(t, 4) == NULL);
		struct entry *to = store_add(&a, E_TODO, period_of(P_DAY, d));
		CHECK(entry_sub(to, EF_WHERE) == NULL); /* only an event has a page */
		char *x1 = store_ics(&a);
		CHECK(strstr(x1, "RRULE:FREQ=WEEKLY") && strstr(x1, "TRIGGER:-PT15M") && strstr(x1, "X-GLASSINE-WITH:the dentist"));
		struct store b;
		store_init(&b, NULL);
		CHECK(store_parse_ics(&b, x1));
		char *x2 = store_ics(&b);
		CHECK_STR(x2, x1);
		struct entry *u = NULL;
		for (int i = 0; i < b.n; i++)
			if (b.e[i]->kind == E_EVENT)
				u = b.e[i];
		CHECK(u && u->repeat == REP_WEEKLY && u->remind == 15 && u->f.ns == t->f.ns);
		CHECK(u && entry_sub(u, EF_WHERE)->ns == entry_sub(t, EF_WHERE)->ns && entry_sub(u, EF_NOTES)->ns > 0);
		CHECK(u && entry_sub(u, EF_WITH)->value && !strcmp(entry_sub(u, EF_WITH)->value, "the dentist"));
		/* the reminders' and repeats' other choices */
		static const int RM[] = { -1, 0, 60, 1440 };
		for (int k = 0; k < 4; k++) {
			t->remind = RM[k], t->repeat = k;
			char *x3 = store_ics(&a);
			struct store c;
			store_init(&c, NULL);
			CHECK(store_parse_ics(&c, x3));
			for (int i = 0; i < c.n; i++)
				if (c.e[i]->kind == E_EVENT)
					CHECK(c.e[i]->remind == RM[k] && c.e[i]->repeat == k);
			store_free(&c), free(x3);
		}
		/* a page field with no entry: nothing made */
		uint32_t none = 0;
		struct pl_point pt[2] = { { 3, 20, 0.5f, 0 }, { 30, 22, 0.5f, 8 } };
		struct fmake mk = { E_EVENT, period_of(P_DAY, d), "", 0 };
		CHECK(field_stroke(&a, &none, EF_NOTES, &mk, 0, pt, 2) == NULL && none == 0);
		CHECK(field_typed(&a, &none, EF_WHERE, &mk, "x") == NULL && none == 0);
		/* journalled: a stroke on its notes survives a crash */
		char jd[] = "out/test-evpage-XXXXXX";
		CHECK(mkdtemp(jd) != NULL);
		struct store j;
		store_init(&j, jd);
		j.jid = 0x3ffffff1;
		struct entry *je = store_add(&j, E_EVENT, period_of(P_DAY, d));
		je->start = 600, je->end = 660;
		uint32_t jid = je->id;
		CHECK(field_stroke(&j, &jid, EF_NOTES, NULL, 4, pt, 2) != NULL && jid == je->id);
		CHECK(store_note(&j, jid));
		struct store k2;
		store_init(&k2, jd);
		k2.jid = (int)getpid();
		store_load(&k2);
		CHECK(k2.n == 1 && entry_sub(k2.e[0], EF_NOTES)->ns == 1 && k2.e[0]->f.ns == 0);
		store_free(&k2), store_free(&j);
		free(x1), free(x2);
		store_free(&a), store_free(&b);
	}

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
