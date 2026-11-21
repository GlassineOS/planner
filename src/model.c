// SPDX-License-Identifier: GPL-2.0-or-later
/* The planner's data (model.h). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model.h"

/* ---- dates ------------------------------------------------------------------------- */

/* days from civil (proleptic Gregorian), after H. Hinnant's public-domain algorithm */
int day_from_ymd(int y, int m, int d)
{
	y -= m <= 2;
	int era = (y >= 0 ? y : y - 399) / 400;
	int yoe = y - era * 400;
	int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

void day_to_ymd(int z, int *y, int *m, int *d)
{
	z += 719468;
	int era = (z >= 0 ? z : z - 146096) / 146097;
	int doe = z - era * 146097;
	int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int yy = yoe + era * 400;
	int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp + (mp < 10 ? 3 : -9);
	*y = yy + (*m <= 2);
}

int day_weekday(int day)
{
	int w = (day + 3) % 7; /* 1970-01-01 was a Thursday */
	return w < 0 ? w + 7 : w;
}

int days_in_month(int y, int m)
{
	static const int n[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
		return 29;
	return n[m - 1];
}

int day_parse(const char *s)
{
	int y, m, d;
	if (!s || sscanf(s, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > days_in_month(y, m))
		return -1;
	return day_from_ymd(y, m, d);
}

void day_format(int day, char *buf, size_t n)
{
	int y, m, d;
	day_to_ymd(day, &y, &m, &d);
	snprintf(buf, n, "%04d-%02d-%02d", y, m, d);
}

struct period period_of(enum pkind k, int day)
{
	struct period p = { k, day };
	if (k == P_WEEK)
		p.start = day - day_weekday(day);
	else if (k == P_MONTH) {
		int y, m, d;
		day_to_ymd(day, &y, &m, &d);
		p.start = day_from_ymd(y, m, 1);
	}
	return p;
}

int period_end(struct period p)
{
	if (p.kind == P_DAY)
		return p.start;
	if (p.kind == P_WEEK)
		return p.start + 6;
	int y, m, d;
	day_to_ymd(p.start, &y, &m, &d);
	return p.start + days_in_month(y, m) - 1;
}

bool period_contains(struct period o, struct period i)
{
	return i.start >= o.start && period_end(i) <= period_end(o) && i.kind <= o.kind;
}

const char *pkind_name(enum pkind k)
{
	return k == P_DAY ? "day" : k == P_WEEK ? "week" : "month";
}

int pkind_parse(const char *s)
{
	if (!strcmp(s, "day"))
		return P_DAY;
	if (!strcmp(s, "week"))
		return P_WEEK;
	if (!strcmp(s, "month"))
		return P_MONTH;
	return -1;
}

/* ---- ink --------------------------------------------------------------------------- */

void field_add_stroke(struct field *f, const struct stroke *s)
{
	if (s->n < 1)
		return;
	if (f->ns == f->cap) {
		f->cap = f->cap ? f->cap * 2 : 8;
		f->s = realloc(f->s, sizeof *f->s * f->cap);
	}
	struct stroke *d = &f->s[f->ns++];
	d->n = d->cap = s->n;
	d->pt = malloc(sizeof *d->pt * s->n);
	memcpy(d->pt, s->pt, sizeof *d->pt * s->n);
}

void field_extent(const struct field *f, double *right, double *bottom)
{
	double r = 0, b = 0;
	for (int i = 0; i < f->ns; i++)
		for (int k = 0; k < f->s[i].n; k++) {
			if (f->s[i].pt[k].x > r)
				r = f->s[i].pt[k].x;
			if (f->s[i].pt[k].y > b)
				b = f->s[i].pt[k].y;
		}
	if (right)
		*right = r;
	if (bottom)
		*bottom = b;
}

const char *field_text(const struct field *f)
{
	return f->edited ? f->text : f->reading;
}

bool field_empty(const struct field *f)
{
	return f->ns == 0 && !(f->edited && f->text[0]);
}

struct sbox { double x0, x1, y0, y1; int line; };

static int sbox_cmp(const void *a, const void *b)
{
	const struct sbox *p = a, *q = b;
	if (p->line != q->line)
		return p->line - q->line;
	return p->x0 < q->x0 ? -1 : p->x0 > q->x0;
}

/*
 * The stand-in for the handwriting service (spec 005 doesn't exist yet): strokes are
 * grouped into words by their horizontal gaps on each line, and every word becomes a
 * placeholder picked by the field and the word's place. Only the data flow is real:
 * ink in, a value out, stored with the entry.
 */
void field_read_stub(struct field *f)
{
	static const char *const vocab[] = { "call", "bank", "dentist", "team", "plan", "milk", "report", "train",
					     "letter", "garden", "review", "lunch", "books", "tickets", "draft", "notes" };
	f->reading[0] = 0;
	f->read_strokes = f->ns;
	if (!f->ns)
		return;
	struct sbox *b = calloc(f->ns, sizeof *b);
	for (int i = 0; i < f->ns; i++) {
		b[i].x0 = b[i].y0 = 1e9;
		b[i].x1 = b[i].y1 = -1e9;
		for (int k = 0; k < f->s[i].n; k++) {
			struct pt p = f->s[i].pt[k];
			b[i].x0 = fmin(b[i].x0, p.x);
			b[i].x1 = fmax(b[i].x1, p.x);
			b[i].y0 = fmin(b[i].y0, p.y);
			b[i].y1 = fmax(b[i].y1, p.y);
		}
		b[i].line = (int)floor((b[i].y0 + b[i].y1) / 2 / 34); /* 34 px lines */
	}
	qsort(b, f->ns, sizeof *b, sbox_cmp);
	size_t len = 0;
	int word = 0;
	double right = -1e9;
	int line = b[0].line;
	for (int i = 0; i <= f->ns; i++) {
		bool brk = i == f->ns || b[i].line != line || b[i].x0 - right > 7;
		if (brk && i > 0) {
			unsigned h = (unsigned)f->id * 2654435761u + (unsigned)word * 40503u;
			const char *w = vocab[(h >> 7) % (sizeof vocab / sizeof *vocab)];
			int n = snprintf(f->reading + len, sizeof f->reading - len, "%s%s", len ? " " : "", w);
			if (n > 0 && (size_t)n < sizeof f->reading - len)
				len += n;
			word++;
		}
		if (i == f->ns)
			break;
		if (b[i].line != line) {
			line = b[i].line;
			right = -1e9;
		}
		right = fmax(right, b[i].x1);
	}
	free(b);
}

/* ---- entries ----------------------------------------------------------------------- */

const char *ekind_name(enum ekind k, bool plural)
{
	static const char *const one[] = { "event", "to-do", "note" }, *const many[] = { "events", "to-dos", "notes" };
	return plural ? many[k] : one[k];
}

void store_init(struct store *s)
{
	memset(s, 0, sizeof *s);
	s->next_id = 1;
	strcpy(s->def[P_DAY], "day-full");
	strcpy(s->def[P_WEEK], "week-columns");
	strcpy(s->def[P_MONTH], "month");
}

static void field_free(struct field *f)
{
	for (int i = 0; i < f->ns; i++)
		free(f->s[i].pt);
	free(f->s);
	f->s = NULL;
	f->ns = f->cap = 0;
}

void store_free(struct store *s)
{
	for (int i = 0; i < s->n; i++)
		field_free(&s->e[i].f);
	s->n = 0;
}

struct entry *store_add(struct store *s, enum ekind k, struct period per)
{
	if (s->n >= STORE_MAX)
		return NULL;
	struct entry *e = &s->e[s->n++];
	memset(e, 0, sizeof *e);
	e->id = s->next_id++;
	e->kind = k;
	e->per = per;
	e->f.id = e->id;
	if (k == E_TODO) {
		int o = 0;
		for (int i = 0; i < s->n - 1; i++)
			if (s->e[i].kind == E_TODO && s->e[i].per.kind == per.kind && s->e[i].per.start == per.start &&
			    s->e[i].order >= o)
				o = s->e[i].order + 1;
		e->order = o;
	}
	return e;
}

struct entry *store_find(struct store *s, int id)
{
	for (int i = 0; i < s->n; i++)
		if (s->e[i].id == id)
			return &s->e[i];
	return NULL;
}

const char *store_choice(const struct store *s, struct period p)
{
	for (int i = 0; i < s->nchoice; i++)
		if (s->choice[i].per.kind == p.kind && s->choice[i].per.start == p.start)
			return s->choice[i].tmpl;
	return s->def[p.kind];
}

void store_choose(struct store *s, struct period p, const char *tmpl)
{
	for (int i = 0; i < s->nchoice; i++)
		if (s->choice[i].per.kind == p.kind && s->choice[i].per.start == p.start) {
			snprintf(s->choice[i].tmpl, sizeof s->choice[i].tmpl, "%s", tmpl);
			return;
		}
	if (s->nchoice < (int)(sizeof s->choice / sizeof *s->choice)) {
		s->choice[s->nchoice].per = p;
		snprintf(s->choice[s->nchoice].tmpl, sizeof s->choice[0].tmpl, "%s", tmpl);
		s->nchoice++;
	}
}

/* ---- files ------------------------------------------------------------------------- */

struct buf { char *p; size_t n, cap; };
static void bput(struct buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void bput(struct buf *b, const char *fmt, ...)
{
	va_list ap;
	for (;;) {
		va_start(ap, fmt);
		int n = vsnprintf(b->p ? b->p + b->n : NULL, b->p ? b->cap - b->n : 0, fmt, ap);
		va_end(ap);
		if (n < 0)
			return;
		if (b->p && b->n + n < b->cap) {
			b->n += n;
			return;
		}
		b->cap = (b->cap + n + 1) * 2;
		b->p = realloc(b->p, b->cap);
	}
}

/* a text on one line: newlines become spaces */
static void bline(struct buf *b, const char *key, const char *t)
{
	bput(b, " %s ", key);
	for (const char *c = t; *c; c++)
		bput(b, "%c", *c == '\n' ? ' ' : *c);
	bput(b, "\n");
}

char *store_entries_text(const struct store *s)
{
	struct buf b = { 0 };
	bput(&b, "# glassine-planner spike: entries (FR-020: each belongs to a date)\n");
	for (int i = 0; i < s->n; i++) {
		const struct entry *e = &s->e[i];
		if (e->deleted)
			continue;
		char d[16];
		day_format(e->per.start, d, sizeof d);
		bput(&b, "entry %d %s %s %s", e->id, ekind_name(e->kind, false), pkind_name(e->per.kind), d);
		if (e->kind == E_EVENT)
			bput(&b, " start=%d end=%d", e->start, e->end);
		if (e->kind == E_TODO)
			bput(&b, " done=%d order=%d", e->done, e->order);
		bput(&b, "\n");
		if (e->f.reading[0])
			bline(&b, "reading", e->f.reading);
		if (e->f.edited)
			bline(&b, "text", e->f.text);
		for (int k = 0; k < e->f.ns; k++) {
			bput(&b, " s");
			for (int j = 0; j < e->f.s[k].n; j++)
				bput(&b, " %.1f,%.1f,%.2f", e->f.s[k].pt[j].x, e->f.s[k].pt[j].y, e->f.s[k].pt[j].p);
			bput(&b, "\n");
		}
	}
	if (!b.p)
		bput(&b, "%s", "");
	return b.p;
}

static bool write_file(const char *path, const char *text)
{
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return false;
	bool ok = fputs(text, f) >= 0;
	ok = fclose(f) == 0 && ok;
	return ok && rename(tmp, path) == 0;
}

bool store_save_entries(const struct store *s, const char *path)
{
	char *t = store_entries_text(s);
	bool ok = write_file(path, t);
	free(t);
	return ok;
}

bool store_save_choices(const struct store *s, const char *path)
{
	struct buf b = { 0 };
	bput(&b, "# glassine-planner spike: template choices\n");
	for (int k = 0; k < 3; k++)
		bput(&b, "default %s %s\n", pkind_name(k), s->def[k]);
	for (int i = 0; i < s->nchoice; i++) {
		char d[16];
		day_format(s->choice[i].per.start, d, sizeof d);
		bput(&b, "choose %s %s %s\n", pkind_name(s->choice[i].per.kind), d, s->choice[i].tmpl);
	}
	bool ok = write_file(path, b.p);
	free(b.p);
	return ok;
}

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;
	struct buf b = { 0 };
	char tmp[4096];
	size_t n;
	while ((n = fread(tmp, 1, sizeof tmp, f)) > 0)
		bput(&b, "%.*s", (int)n, tmp);
	fclose(f);
	if (!b.p)
		bput(&b, "%s", "");
	return b.p;
}

bool store_load(struct store *s, const char *entries, const char *choices)
{
	store_free(s);
	store_init(s);
	char *t = entries ? slurp(entries) : NULL;
	struct entry *e = NULL;
	for (char *line = t ? strtok(t, "\n") : NULL; line; line = strtok(NULL, "\n")) {
		char kind[16], pk[16], date[16];
		int id;
		if (sscanf(line, "entry %d %15s %15s %15s", &id, kind, pk, date) == 4) {
			int k = !strcmp(kind, "event") ? E_EVENT : !strcmp(kind, "to-do") ? E_TODO : E_NOTE;
			int p = pkind_parse(pk), d = day_parse(date);
			if (p < 0 || d < 0 || s->n >= STORE_MAX) {
				e = NULL;
				continue;
			}
			e = &s->e[s->n++];
			memset(e, 0, sizeof *e);
			e->id = e->f.id = id;
			e->kind = k;
			e->per = (struct period){ p, d };
			const char *a;
			if ((a = strstr(line, " start=")))
				e->start = atoi(a + 7);
			if ((a = strstr(line, " end=")))
				e->end = atoi(a + 5);
			if ((a = strstr(line, " done=")))
				e->done = atoi(a + 6);
			if ((a = strstr(line, " order=")))
				e->order = atoi(a + 7);
			if (id >= s->next_id)
				s->next_id = id + 1;
		} else if (e && !strncmp(line, " reading ", 9)) {
			snprintf(e->f.reading, sizeof e->f.reading, "%s", line + 9);
		} else if (e && !strncmp(line, " text ", 6)) {
			snprintf(e->f.text, sizeof e->f.text, "%s", line + 6);
			e->f.edited = true;
		} else if (e && !strncmp(line, " text", 5) && !line[5]) {
			e->f.text[0] = 0;
			e->f.edited = true;
		} else if (e && !strncmp(line, " s ", 3)) {
			struct stroke st = { 0 };
			int cap = 64;
			st.pt = malloc(sizeof *st.pt * cap);
			char *c = line + 3;
			float x, y, p;
			int used;
			while (sscanf(c, " %f,%f,%f%n", &x, &y, &p, &used) == 3) {
				if (st.n == cap)
					st.pt = realloc(st.pt, sizeof *st.pt * (cap *= 2));
				st.pt[st.n++] = (struct pt){ x, y, p };
				c += used;
			}
			field_add_stroke(&e->f, &st);
			free(st.pt);
		}
	}
	free(t);
	for (int i = 0; i < s->n; i++)
		s->e[i].f.read_strokes = s->e[i].f.ns;
	t = choices ? slurp(choices) : NULL;
	for (char *line = t ? strtok(t, "\n") : NULL; line; line = strtok(NULL, "\n")) {
		char a[16], b[16], c[48];
		if (sscanf(line, "default %15s %47s", a, c) == 2 && pkind_parse(a) >= 0)
			snprintf(s->def[pkind_parse(a)], sizeof s->def[0], "%s", c);
		else if (sscanf(line, "choose %15s %15s %47s", a, b, c) == 3 && pkind_parse(a) >= 0 && day_parse(b) >= 0)
			store_choose(s, (struct period){ pkind_parse(a), day_parse(b) }, c);
	}
	free(t);
	return true;
}

/* ---- synthetic ink ------------------------------------------------------------------ */

static unsigned rnd(unsigned *s)
{
	*s = *s * 1103515245u + 12345u;
	return (*s >> 16) & 0x7fff;
}

void synth_words(struct field *f, double x, double y, double xh, int words, unsigned seed)
{
	unsigned r = seed * 7919u + 17u;
	struct pt pts[400];
	for (int w = 0; w < words; w++) {
		int letters = 2 + (int)(rnd(&r) % 6);
		int n = 0;
		double cx = x;
		for (int l = 0; l < letters && n < 380; l++) {
			double tall = (rnd(&r) % 4 == 0) ? 1.9 : 1.0; /* an ascender now and then */
			double lw = xh * (0.55 + (rnd(&r) % 40) / 100.0);
			for (int k = 0; k < 9; k++) {
				double t = k / 8.0;
				double px = cx + lw * t + sin(t * 2 * M_PI) * lw * 0.18;
				double py = y - xh * tall * (0.5 - 0.5 * cos(t * 2 * M_PI)) + (rnd(&r) % 10) / 10.0;
				pts[n++] = (struct pt){ (float)px, (float)py, 0.5f + (rnd(&r) % 30) / 100.0f };
			}
			cx += lw;
		}
		struct stroke s = { pts, n, n };
		field_add_stroke(f, &s);
		x = cx + xh * (1.3 + (rnd(&r) % 30) / 100.0);
	}
}
