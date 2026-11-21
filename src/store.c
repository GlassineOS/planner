// SPDX-License-Identifier: GPL-2.0-or-later
/* The planner's data (store.h). */
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "store.h"

/* ---- dates (H. Hinnant's public-domain civil-day algorithms) ---- */

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

int day_today(void)
{
	const char *e = getenv("GLASSINE_PLANNER_TODAY"); /* tests and renders: YYYY-MM-DD */
	int y, m, d;
	if (e && sscanf(e, "%d-%d-%d", &y, &m, &d) == 3)
		return day_from_ymd(y, m, d);
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return day_from_ymd(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

int week_number(int day)
{
	int thu = day - day_weekday(day) + 3, y, m, d;
	day_to_ymd(thu, &y, &m, &d);
	return (thu - day_from_ymd(y, 1, 1)) / 7 + 1;
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

struct period period_step(struct period p, int n)
{
	if (p.kind == P_DAY)
		return (struct period){ P_DAY, p.start + n };
	if (p.kind == P_WEEK)
		return (struct period){ P_WEEK, p.start + 7 * n };
	int y, m, d;
	day_to_ymd(p.start, &y, &m, &d);
	int mm = (y * 12 + m - 1) + n;
	return (struct period){ P_MONTH, day_from_ymd(mm / 12, mm % 12 + 1, 1) };
}

bool period_eq(struct period a, struct period b)
{
	return a.kind == b.kind && a.start == b.start;
}

const char *pkind_name(enum pkind k)
{
	return k == P_DAY ? "day" : k == P_WEEK ? "week" : "month";
}

int pkind_parse(const char *s)
{
	return !strcmp(s, "day") ? P_DAY : !strcmp(s, "week") ? P_WEEK : !strcmp(s, "month") ? P_MONTH : -1;
}

/* ---- fields ---- */

static void words_free(struct field *f)
{
	for (int i = 0; i < f->nw; i++) {
		free(f->w[i].ks);
		free(f->w[i].path);
	}
	free(f->w);
	f->w = NULL;
	f->nw = 0;
}

void field_free(struct field *f)
{
	for (int i = 0; i < f->ns; i++)
		free(f->s[i].pt);
	free(f->s);
	for (int i = 0; i < f->nneat; i++)
		free(f->neat[i].pt);
	free(f->neat);
	words_free(f);
	free(f->value);
	free(f->state);
	memset(f, 0, sizeof *f);
}

static void strokes_remove(struct pl_stroke *s, int *n, uint32_t k)
{
	for (int i = 0; i < *n; i++)
		if (s[i].k == k) {
			free(s[i].pt);
			memmove(&s[i], &s[i + 1], (*n - i - 1) * sizeof *s);
			(*n)--;
			return;
		}
}

void field_set_neat(struct field *f, uint32_t k, const struct pl_point *p, int n)
{
	strokes_remove(f->neat, &f->nneat, k);
	struct pl_stroke *s = realloc(f->neat, (f->nneat + 1) * sizeof *s);
	if (!s)
		return;
	f->neat = s;
	struct pl_point *c = malloc((n ? n : 1) * sizeof *c);
	if (!c)
		return;
	memcpy(c, p, n * sizeof *c);
	f->neat[f->nneat++] = (struct pl_stroke){ k, c, n };
}

const struct pl_stroke *field_neat(const struct field *f, uint32_t k)
{
	for (int i = 0; i < f->nneat; i++)
		if (f->neat[i].k == k)
			return &f->neat[i];
	return NULL;
}

void field_set_words(struct field *f, const struct pl_word *w, int n)
{
	words_free(f);
	if (n <= 0)
		return;
	f->w = calloc(n, sizeof *f->w);
	if (!f->w)
		return;
	for (int i = 0; i < n; i++) {
		struct pl_word *d = &f->w[f->nw++];
		*d = w[i];
		d->ks = w[i].nks ? malloc(w[i].nks * sizeof *d->ks) : NULL;
		d->path = w[i].npath ? malloc(w[i].npath * sizeof *d->path) : NULL;
		if (d->ks)
			memcpy(d->ks, w[i].ks, w[i].nks * sizeof *d->ks);
		else
			d->nks = 0;
		if (d->path)
			memcpy(d->path, w[i].path, w[i].npath * sizeof *d->path);
		else
			d->npath = 0;
	}
}

int field_word_of(const struct field *f, uint32_t k)
{
	for (int i = 0; i < f->nw; i++)
		for (int j = 0; j < f->w[i].nks; j++)
			if (f->w[i].ks[j] == k)
				return i;
	return -1;
}

bool field_neaten(const struct store *s, const struct field *f)
{
	return f->neaten == SHOW_FOLLOW ? s->neaten : f->neaten == SHOW_ON;
}

bool field_autograph(const struct store *s, const struct field *f)
{
	return f->autograph == SHOW_FOLLOW ? s->autograph : f->autograph == SHOW_ON;
}

void field_remove_stroke(struct field *f, uint32_t k)
{
	strokes_remove(f->neat, &f->nneat, k);
	for (int i = 0; i < f->ns; i++)
		if (f->s[i].k == k) {
			free(f->s[i].pt);
			memmove(&f->s[i], &f->s[i + 1], (f->ns - i - 1) * sizeof *f->s);
			f->ns--;
			return;
		}
}

void field_add_stroke(struct field *f, uint32_t k, const struct pl_point *p, int n)
{
	field_remove_stroke(f, k);
	struct pl_stroke *s = realloc(f->s, (f->ns + 1) * sizeof *s);
	if (!s)
		return;
	f->s = s;
	struct pl_point *c = malloc((n ? n : 1) * sizeof *c);
	if (!c)
		return;
	memcpy(c, p, n * sizeof *c);
	f->s[f->ns++] = (struct pl_stroke){ k, c, n };
}

void field_set_value(struct field *f, const char *text, const void *state, size_t len)
{
	free(f->value);
	f->value = text && *text ? strdup(text) : NULL;
	free(f->state);
	f->state = NULL;
	f->state_len = 0;
	if (state && len) {
		f->state = malloc(len);
		if (f->state) {
			memcpy(f->state, state, len);
			f->state_len = len;
		}
	}
}

bool field_empty(const struct field *f)
{
	return f->ns == 0 && !(f->value && *f->value);
}

void field_extent(const struct field *f, double *right, double *bottom)
{
	double r = 0, b = 0;
	for (int i = 0; i < f->ns; i++) {
		int wi = field_word_of(f, f->s[i].k);
		double dx = wi >= 0 ? f->w[wi].dx : 0, dy = wi >= 0 ? f->w[wi].dy : 0;
		for (int j = 0; j < f->s[i].n; j++) {
			if (f->s[i].pt[j].x + dx > r)
				r = f->s[i].pt[j].x + dx;
			if (f->s[i].pt[j].y + dy > b)
				b = f->s[i].pt[j].y + dy;
		}
	}
	for (int i = 0; i < f->nw; i++)
		if (f->w[i].set) {
			r = fmax(r, f->w[i].box[0] + f->w[i].box[2]);
			b = fmax(b, f->w[i].box[1] + f->w[i].box[3]);
		}
	*right = r;
	*bottom = b;
}

/* ---- entries ---- */

void store_init(struct store *s, const char *dir)
{
	memset(s, 0, sizeof *s);
	s->next_id = 1;
	s->strength = 0.65f;
	s->dir = dir ? strdup(dir) : NULL;
}

void store_free(struct store *s)
{
	for (int i = 0; i < s->n; i++) {
		field_free(&s->e[i]->f);
		free(s->e[i]);
	}
	free(s->e);
	free(s->c);
	free(s->dir);
	free(s->deleted);
	free(s->replayed);
	memset(s, 0, sizeof *s);
}

static struct entry *store_put(struct store *s)
{
	if (s->n == s->cap) {
		int c = s->cap ? s->cap * 2 : 64;
		struct entry **e = realloc(s->e, c * sizeof *e);
		if (!e)
			return NULL;
		s->e = e;
		s->cap = c;
	}
	struct entry *x = calloc(1, sizeof *x);
	if (!x)
		return NULL;
	x->id = s->next_id++;
	s->e[s->n++] = x;
	return x;
}

struct entry *store_add(struct store *s, enum ekind k, struct period per)
{
	struct entry *x = store_put(s);
	if (!x)
		return NULL;
	x->kind = k;
	x->per = per;
	x->age = (long long)time(NULL);
	static unsigned salt;
	snprintf(x->uid, sizeof x->uid, "%llx-%x-%x@planner.glassine", (long long)time(NULL), (unsigned)getpid() & 0xffff,
		 ++salt * 2654435761u ^ x->id);
	return x;
}

struct entry *store_get(struct store *s, uint32_t id)
{
	for (int i = 0; i < s->n; i++)
		if (s->e[i]->id == id)
			return s->e[i];
	return NULL;
}

void store_delete(struct store *s, uint32_t id)
{
	for (int i = 0; i < s->n; i++)
		if (s->e[i]->id == id) {
			char (*d)[48] = realloc(s->deleted, (s->ndel + 1) * sizeof *d);
			if (d) {
				s->deleted = d;
				snprintf(d[s->ndel++], 48, "%s", s->e[i]->uid);
			}
			field_free(&s->e[i]->f);
			free(s->e[i]);
			memmove(&s->e[i], &s->e[i + 1], (s->n - i - 1) * sizeof *s->e);
			s->n--;
			return;
		}
}

static int by_rank(const void *a, const void *b)
{
	const struct entry *x = *(struct entry *const *)a, *y = *(struct entry *const *)b;
	if (x->rank != y->rank)
		return x->rank < y->rank ? -1 : 1;
	if (x->age != y->age)
		return x->age < y->age ? -1 : 1;
	return x->id < y->id ? -1 : x->id > y->id;
}

int store_list(struct store *s, enum ekind k, struct period p, const char *slot, struct entry **out, int max)
{
	int n = 0;
	for (int i = 0; i < s->n && n < max; i++) {
		struct entry *x = s->e[i];
		if (x->kind != k)
			continue;
		if (k == E_HABIT)
			; /* habits belong to no period */
		else if (k == E_MARK) {
			if (x->per.start < p.start || x->per.start > period_end(p))
				continue;
		} else if (!period_eq(x->per, p))
			continue;
		if (slot && strcmp(x->slot, slot))
			continue;
		out[n++] = x;
	}
	qsort(out, n, sizeof *out, by_rank);
	return n;
}

static int by_start(const void *a, const void *b)
{
	const struct entry *x = *(struct entry *const *)a, *y = *(struct entry *const *)b;
	if (x->allday != y->allday)
		return x->allday ? -1 : 1;
	long ax = x->per.start * 1440L + x->start, ay = y->per.start * 1440L + y->start;
	return ax < ay ? -1 : ax > ay ? 1 : (x->id < y->id ? -1 : x->id > y->id);
}

int store_events(struct store *s, int day, struct entry **out, int max)
{
	int n = 0;
	for (int i = 0; i < s->n && n < max; i++) {
		struct entry *x = s->e[i];
		if (x->kind != E_EVENT)
			continue;
		bool in;
		if (x->allday)
			in = day >= x->per.start && day < x->per.start + (x->end > x->start ? x->end : 1);
		else {
			int last = x->per.start + (x->end - 1) / 1440;
			in = day >= x->per.start && day <= (last < x->per.start ? x->per.start : last);
		}
		if (in)
			out[n++] = x;
	}
	qsort(out, n, sizeof *out, by_start);
	return n;
}

/* ---- a field's region ---- */

struct entry *field_stroke(struct store *s, uint32_t *entry, const struct fmake *mk, uint32_t k,
			   const struct pl_point *p, int n)
{
	struct entry *e = *entry ? store_get(s, *entry) : NULL;
	if (!e) {
		if (!mk || mk->kind >= E_NKINDS)
			return NULL;
		e = store_add(s, mk->kind, mk->per);
		if (!e)
			return NULL;
		e->rank = mk->rank;
		snprintf(e->slot, sizeof e->slot, "%s", mk->slot);
		*entry = e->id;
	}
	field_add_stroke(&e->f, k, p, n);
	return e;
}

bool field_value(struct store *s, uint32_t *entry, const char *text, const struct pl_word *w, int nw,
		 const uint32_t *gone, size_t ngone, const void *state, size_t len)
{
	struct entry *e = *entry ? store_get(s, *entry) : NULL;
	if (!e)
		return false;
	for (size_t i = 0; i < ngone; i++)
		field_remove_stroke(&e->f, gone[i]);
	field_set_value(&e->f, text, state, len);
	field_set_words(&e->f, w, nw);
	if (e->kind != E_EVENT && field_empty(&e->f)) {
		/* all its ink went (struck out): the row is empty again, not an entry */
		store_delete(s, e->id);
		*entry = 0;
		return true;
	}
	return false;
}

struct entry *field_typed(struct store *s, uint32_t *entry, const struct fmake *mk, const char *text)
{
	struct entry *e = *entry ? store_get(s, *entry) : NULL;
	if (!e) {
		if (!mk || mk->kind >= E_NKINDS || !text || !*text)
			return NULL;
		e = store_add(s, mk->kind, mk->per);
		if (!e)
			return NULL;
		e->rank = mk->rank;
		snprintf(e->slot, sizeof e->slot, "%s", mk->slot);
		*entry = e->id;
	}
	free(e->f.value);
	e->f.value = *text ? strdup(text) : NULL;
	return e;
}

/* ---- template choices ---- */

const char *store_template(const struct store *s, struct period p, int *how)
{
	const struct choice *range = NULL, *onwards = NULL;
	for (int i = s->nc - 1; i >= 0; i--) {
		const struct choice *c = &s->c[i];
		if (c->from.kind != p.kind)
			continue;
		if (c->to == p.start && c->from.start == p.start) {
			if (how)
				*how = 0;
			return c->tmpl;
		}
		if (c->to >= 0 && p.start >= c->from.start && p.start <= c->to && !range)
			range = c;
		if (c->to < 0 && p.start >= c->from.start && (!onwards || c->from.start > onwards->from.start))
			onwards = c;
	}
	if (range) {
		if (how)
			*how = 1;
		return range->tmpl;
	}
	if (onwards) {
		if (how)
			*how = 2;
		return onwards->tmpl;
	}
	if (how)
		*how = 3;
	return s->def[p.kind];
}

void store_choose(struct store *s, struct period from, int to, const char *tmpl)
{
	for (int i = 0; i < s->nc; i++)
		if (period_eq(s->c[i].from, from) && s->c[i].to == to) {
			snprintf(s->c[i].tmpl, sizeof s->c[i].tmpl, "%s", tmpl);
			return;
		}
	struct choice *c = realloc(s->c, (s->nc + 1) * sizeof *c);
	if (!c)
		return;
	s->c = c;
	c[s->nc] = (struct choice){ from, to, "" };
	snprintf(c[s->nc].tmpl, sizeof c[s->nc].tmpl, "%s", tmpl);
	s->nc++;
}

/* ---- iCalendar ---- */

struct buf {
	char *p;
	size_t n, cap;
};

static void bput(struct buf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		size_t c = b->cap ? b->cap : 4096;
		while (c < b->n + n + 1)
			c *= 2;
		char *p = realloc(b->p, c);
		if (!p)
			return;
		b->p = p;
		b->cap = c;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

/* one content line, folded at 75 octets (RFC 5545 3.1), never inside a UTF-8 sequence */
static void line(struct buf *b, const char *name, const char *value)
{
	struct buf l = { 0 };
	bput(&l, name, strlen(name));
	bput(&l, ":", 1);
	bput(&l, value, strlen(value));
	size_t i = 0, col = 0;
	while (i < l.n) {
		size_t k = 1;
		while (i + k < l.n && ((unsigned char)l.p[i + k] & 0xc0) == 0x80)
			k++;
		if (col + k > 75) {
			bput(b, "\r\n ", 3);
			col = 1;
		}
		bput(b, l.p + i, k);
		col += k;
		i += k;
	}
	bput(b, "\r\n", 2);
	free(l.p);
}

static char *esc_text(const char *s)
{
	struct buf b = { 0 };
	bput(&b, "", 0);
	for (; s && *s; s++) {
		if (*s == '\\' || *s == ';' || *s == ',') {
			bput(&b, "\\", 1);
			bput(&b, s, 1);
		} else if (*s == '\n')
			bput(&b, "\\n", 2);
		else
			bput(&b, s, 1);
	}
	return b.p;
}

static char *unesc_text(const char *s)
{
	char *o = malloc(strlen(s) + 1), *q = o;
	if (!o)
		return NULL;
	for (; *s; s++) {
		if (*s == '\\' && s[1]) {
			s++;
			*q++ = (*s == 'n' || *s == 'N') ? '\n' : *s;
		} else
			*q++ = *s;
	}
	*q = 0;
	return o;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *b64(const uint8_t *d, size_t n)
{
	char *o = malloc(4 * ((n + 2) / 3) + 1), *q = o;
	if (!o)
		return NULL;
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
		*q++ = B64[v >> 18 & 63];
		*q++ = B64[v >> 12 & 63];
		*q++ = i + 1 < n ? B64[v >> 6 & 63] : '=';
		*q++ = i + 2 < n ? B64[v & 63] : '=';
	}
	*q = 0;
	return o;
}

static uint8_t *unb64(const char *s, size_t *len)
{
	size_t n = strlen(s);
	uint8_t *o = malloc(n / 4 * 3 + 3);
	size_t k = 0;
	uint32_t v = 0;
	int bits = 0;
	if (!o)
		return NULL;
	for (; *s; s++) {
		const char *p = strchr(B64, *s);
		if (!p || !*s)
			continue;
		v = v << 6 | (uint32_t)(p - B64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			o[k++] = v >> bits & 0xff;
		}
	}
	*len = k;
	return o;
}

/* the ink: per stroke its number and point count, then x, y (1/64 px), pressure (0..255)
 * and time (ms since the stroke's first point), little-endian */
static void put32(struct buf *b, uint32_t v)
{
	uint8_t c[4] = { v, v >> 8, v >> 16, v >> 24 };
	bput(b, (char *)c, 4);
}

static char *ink_b64(const struct pl_stroke *ss, int ns)
{
	struct buf b = { 0 };
	put32(&b, 1); /* format */
	put32(&b, ns);
	for (int i = 0; i < ns; i++) {
		put32(&b, ss[i].k);
		put32(&b, ss[i].n);
		uint32_t t0 = ss[i].n ? ss[i].pt[0].t : 0;
		for (int j = 0; j < ss[i].n; j++) {
			const struct pl_point *p = &ss[i].pt[j];
			put32(&b, (uint32_t)(int32_t)(p->x * 64));
			put32(&b, (uint32_t)(int32_t)(p->y * 64));
			put32(&b, (uint32_t)(p->p * 255 + 0.5) | (p->t - t0) << 8);
		}
	}
	char *s = b64((uint8_t *)b.p, b.n);
	free(b.p);
	return s;
}

static uint32_t get32(const uint8_t *d, size_t len, size_t *o)
{
	if (*o + 4 > len) {
		*o = len + 1;
		return 0;
	}
	uint32_t v = d[*o] | d[*o + 1] << 8 | d[*o + 2] << 16 | (uint32_t)d[*o + 3] << 24;
	*o += 4;
	return v;
}

static void ink_parse(struct field *f, const char *s, bool neat)
{
	size_t len, o = 0;
	uint8_t *d = unb64(s, &len);
	if (!d)
		return;
	if (get32(d, len, &o) == 1) {
		uint32_t ns = get32(d, len, &o);
		for (uint32_t i = 0; i < ns && o <= len; i++) {
			uint32_t k = get32(d, len, &o), n = get32(d, len, &o);
			if (o > len || n > (len - o) / 12)
				break;
			struct pl_point *p = malloc((n ? n : 1) * sizeof *p);
			if (!p)
				break;
			for (uint32_t j = 0; j < n; j++) {
				p[j].x = (int32_t)get32(d, len, &o) / 64.0f;
				p[j].y = (int32_t)get32(d, len, &o) / 64.0f;
				uint32_t pt = get32(d, len, &o);
				p[j].p = (pt & 0xff) / 255.0f;
				p[j].t = pt >> 8;
			}
			if (neat)
				field_set_neat(f, k, p, n);
			else
				field_add_stroke(f, k, p, n);
			free(p);
		}
	}
	free(d);
}

/* the word layout: per word start, length, set, its strokes, dx, dy, box (1/64 px) and path
 * (numbers x 64), little-endian */
static char *words_b64(const struct field *f)
{
	struct buf b = { 0 };
	put32(&b, 1);
	put32(&b, f->nw);
	for (int i = 0; i < f->nw; i++) {
		const struct pl_word *w = &f->w[i];
		put32(&b, w->start);
		put32(&b, w->len);
		put32(&b, w->set);
		put32(&b, w->nks);
		for (int j = 0; j < w->nks; j++)
			put32(&b, w->ks[j]);
		put32(&b, (uint32_t)(int32_t)lroundf(w->dx * 64));
		put32(&b, (uint32_t)(int32_t)lroundf(w->dy * 64));
		for (int j = 0; j < 4; j++)
			put32(&b, (uint32_t)(int32_t)lroundf(w->box[j] * 64));
		put32(&b, w->npath);
		for (int j = 0; j < w->npath; j++)
			put32(&b, (uint32_t)(int32_t)lroundf(w->path[j] * 64));
	}
	char *s = b64((uint8_t *)b.p, b.n);
	free(b.p);
	return s;
}

static void words_parse(struct field *f, const char *s)
{
	size_t len, o = 0;
	uint8_t *d = unb64(s, &len);
	if (!d)
		return;
	if (get32(d, len, &o) == 1) {
		uint32_t n = get32(d, len, &o);
		struct pl_word *w = n && n < len ? calloc(n, sizeof *w) : NULL;
		uint32_t got = 0;
		for (; w && got < n && o <= len; got++) {
			struct pl_word *x = &w[got];
			x->start = get32(d, len, &o);
			x->len = get32(d, len, &o);
			x->set = get32(d, len, &o) != 0;
			uint32_t nk = get32(d, len, &o);
			if (o > len || nk > (len - o) / 4)
				break;
			x->ks = malloc((nk ? nk : 1) * sizeof *x->ks);
			x->nks = nk;
			for (uint32_t j = 0; j < nk; j++)
				x->ks[j] = get32(d, len, &o);
			x->dx = (int32_t)get32(d, len, &o) / 64.0f;
			x->dy = (int32_t)get32(d, len, &o) / 64.0f;
			for (int j = 0; j < 4; j++)
				x->box[j] = (int32_t)get32(d, len, &o) / 64.0f;
			uint32_t np = get32(d, len, &o);
			if (o > len || np > (len - o) / 4) {
				free(x->ks);
				break;
			}
			x->path = malloc((np ? np : 1) * sizeof *x->path);
			x->npath = np;
			for (uint32_t j = 0; j < np; j++)
				x->path[j] = (int32_t)get32(d, len, &o) / 64.0f;
		}
		if (w && got == n)
			field_set_words(f, w, n);
		for (uint32_t i = 0; w && i < got; i++) {
			free(w[i].ks);
			free(w[i].path);
		}
		free(w);
	}
	free(d);
}

static void fmt_date(char *o, size_t n, int day)
{
	int y, m, d;
	day_to_ymd(day, &y, &m, &d);
	snprintf(o, n, "%04d%02d%02d", y, m, d);
}

static void fmt_time(char *o, size_t n, int day, int min)
{
	day += min >= 0 ? min / 1440 : -((-min + 1439) / 1440);
	min -= (min >= 0 ? min / 1440 : -((-min + 1439) / 1440)) * 1440;
	int y, m, d;
	day_to_ymd(day, &y, &m, &d);
	snprintf(o, n, "%04d%02d%02dT%02d%02d00", y, m, d, min / 60, min % 60);
}

static const char *KIND[] = { "event", "todo", "note", "priority", "habit", "habit-mark" };

static void entry_ics(struct buf *b, const struct entry *x)
{
	const char *comp = x->kind == E_EVENT ? "VEVENT" : x->kind == E_TODO || x->kind == E_PRIO ? "VTODO" : "VJOURNAL";
	char v[64], w[64];
	line(b, "BEGIN", comp);
	line(b, "UID", x->uid);
	snprintf(v, sizeof v, "%lld", x->age);
	line(b, "X-GLASSINE-CREATED", v);
	line(b, "X-GLASSINE-KIND", KIND[x->kind]);
	if (x->kind == E_EVENT) {
		if (x->allday) {
			fmt_date(v, sizeof v, x->per.start);
			line(b, "DTSTART;VALUE=DATE", v);
			fmt_date(w, sizeof w, x->per.start + (x->end > x->start ? x->end : 1));
			line(b, "DTEND;VALUE=DATE", w);
		} else {
			fmt_time(v, sizeof v, x->per.start, x->start);
			line(b, "DTSTART", v);
			fmt_time(w, sizeof w, x->per.start, x->end);
			line(b, "DTEND", w);
		}
	} else if (x->kind != E_HABIT) {
		fmt_date(v, sizeof v, x->per.start);
		line(b, "DTSTART;VALUE=DATE", v);
		if (x->kind != E_MARK)
			line(b, "X-GLASSINE-PERIOD", pkind_name(x->per.kind));
	}
	if (x->kind == E_TODO || x->kind == E_PRIO) {
		line(b, "STATUS", x->done ? "COMPLETED" : "NEEDS-ACTION");
		snprintf(v, sizeof v, "%d", x->rank);
		line(b, "X-GLASSINE-RANK", v);
	}
	if (x->kind == E_NOTE)
		line(b, "X-GLASSINE-SLOT", x->slot);
	if (x->kind == E_MARK) {
		snprintf(v, sizeof v, "%u", x->habit);
		line(b, "X-GLASSINE-HABIT", v);
	}
	if (x->f.value) {
		char *t = esc_text(x->f.value);
		line(b, x->kind == E_NOTE ? "DESCRIPTION" : "SUMMARY", t);
		free(t);
	}
	if (x->f.ns) {
		char *t = ink_b64(x->f.s, x->f.ns);
		line(b, "X-GLASSINE-INK", t);
		free(t);
	}
	if (x->f.nneat) {
		char *t = ink_b64(x->f.neat, x->f.nneat);
		line(b, "X-GLASSINE-NEAT", t);
		free(t);
	}
	if (x->f.nw) {
		char *t = words_b64(&x->f);
		line(b, "X-GLASSINE-WORDS", t);
		free(t);
	}
	if (x->f.neaten || x->f.autograph) {
		static const char *SH[] = { "follow", "off", "on" };
		snprintf(v, sizeof v, "neaten=%s;autograph=%s", SH[x->f.neaten % 3], SH[x->f.autograph % 3]);
		line(b, "X-GLASSINE-SHOW", v);
	}
	if (x->f.state_len) {
		char *t = b64(x->f.state, x->f.state_len);
		line(b, "X-GLASSINE-STATE", t);
		free(t);
	}
	line(b, "END", comp);
}

char *store_ics(const struct store *s)
{
	struct buf b = { 0 };
	line(&b, "BEGIN", "VCALENDAR");
	line(&b, "VERSION", "2.0");
	line(&b, "PRODID", "-//GlassineOS//Planner//EN");
	for (int i = 0; i < s->n; i++) {
		const struct entry *x = s->e[i];
		if (x->kind == E_MARK) /* marks name their habit by its local id: written after it */
			continue;
		entry_ics(&b, x);
	}
	for (int i = 0; i < s->n; i++)
		if (s->e[i]->kind == E_MARK) {
			const struct entry *x = s->e[i];
			struct entry y = *x;
			struct entry *h = store_get((struct store *)s, x->habit);
			/* the mark names its habit by uid in the file */
			char v[64];
			(void)v;
			y.habit = 0;
			struct buf t = { 0 };
			entry_ics(&t, &y);
			if (h) {
				/* insert RELATED-TO before END */
				char *end = strstr(t.p, "END:VJOURNAL");
				if (end) {
					struct buf u = { 0 };
					bput(&u, t.p, end - t.p);
					line(&u, "RELATED-TO", h->uid);
					bput(&u, end, strlen(end));
					bput(&b, u.p, u.n);
					free(u.p);
				}
			} else
				bput(&b, t.p, t.n);
			free(t.p);
		}
	line(&b, "END", "VCALENDAR");
	return b.p;
}

static int parse_dt(const char *v, int *day, int *min)
{
	int y, m, d, hh = 0, mm = 0;
	if (sscanf(v, "%4d%2d%2d", &y, &m, &d) != 3)
		return -1;
	*day = day_from_ymd(y, m, d);
	if (v[8] == 'T' && sscanf(v + 9, "%2d%2d", &hh, &mm) == 2) {
		*min = hh * 60 + mm;
		return 1;
	}
	*min = 0;
	return 0;
}

bool store_parse_ics(struct store *s, const char *text)
{
	/* unfold */
	struct buf u = { 0 };
	for (const char *p = text; *p; p++) {
		if (*p == '\r')
			continue;
		if (*p == '\n' && (p[1] == ' ' || p[1] == '\t')) {
			p++;
			continue;
		}
		bput(&u, p, 1);
	}
	if (!u.p)
		return true;
	struct entry *x = NULL;
	char rel[48] = "";
	struct mark { struct entry *e; char uid[48]; } *marks = NULL;
	int nm = 0;
	int sday = 0, smin = 0, stimed = 0, eday = 0, emin = 0, etimed = -1;
	for (char *l = strtok(u.p, "\n"); l; l = strtok(NULL, "\n")) {
		char *c = strchr(l, ':');
		if (!c)
			continue;
		*c = 0;
		char *name = l, *val = c + 1, *semi = strchr(name, ';');
		if (semi)
			*semi = 0;
		if (!strcmp(name, "BEGIN") && (!strcmp(val, "VEVENT") || !strcmp(val, "VTODO") || !strcmp(val, "VJOURNAL"))) {
			x = store_put(s);
			if (!x)
				break;
			x->kind = !strcmp(val, "VEVENT") ? E_EVENT : !strcmp(val, "VTODO") ? E_TODO : E_NOTE;
			x->per.kind = P_DAY;
			rel[0] = 0;
			etimed = -1;
			continue;
		}
		if (!x)
			continue;
		if (!strcmp(name, "END")) {
			if (x->kind == E_EVENT) {
				x->per = (struct period){ P_DAY, sday };
				if (!stimed) {
					x->allday = true;
					x->start = 0;
					x->end = etimed >= 0 ? eday - sday : 1;
				} else {
					x->start = smin;
					x->end = etimed > 0 ? (eday - sday) * 1440 + emin : smin + 60;
				}
			} else if (x->kind != E_HABIT)
				x->per.start = sday;
			if (x->kind == E_MARK && rel[0]) {
				struct mark *m = realloc(marks, (nm + 1) * sizeof *m);
				if (m) {
					marks = m;
					marks[nm].e = x;
					snprintf(marks[nm].uid, sizeof marks[nm].uid, "%s", rel);
					nm++;
				}
			}
			x = NULL;
			continue;
		}
		if (!strcmp(name, "UID"))
			snprintf(x->uid, sizeof x->uid, "%s", val);
		else if (!strcmp(name, "X-GLASSINE-KIND")) {
			for (int k = 0; k < E_NKINDS; k++)
				if (!strcmp(val, KIND[k]))
					x->kind = k;
		} else if (!strcmp(name, "X-GLASSINE-CREATED"))
			x->age = atoll(val);
		else if (!strcmp(name, "DTSTART"))
			stimed = parse_dt(val, &sday, &smin) == 1;
		else if (!strcmp(name, "DTEND"))
			etimed = parse_dt(val, &eday, &emin);
		else if (!strcmp(name, "X-GLASSINE-PERIOD")) {
			int k = pkind_parse(val);
			if (k >= 0)
				x->per.kind = k;
		} else if (!strcmp(name, "STATUS"))
			x->done = !strcmp(val, "COMPLETED");
		else if (!strcmp(name, "X-GLASSINE-RANK"))
			x->rank = atoi(val);
		else if (!strcmp(name, "X-GLASSINE-SLOT"))
			snprintf(x->slot, sizeof x->slot, "%s", val);
		else if (!strcmp(name, "RELATED-TO"))
			snprintf(rel, sizeof rel, "%s", val);
		else if (!strcmp(name, "SUMMARY") || !strcmp(name, "DESCRIPTION")) {
			free(x->f.value);
			x->f.value = unesc_text(val);
		} else if (!strcmp(name, "X-GLASSINE-INK"))
			ink_parse(&x->f, val, false);
		else if (!strcmp(name, "X-GLASSINE-NEAT"))
			ink_parse(&x->f, val, true);
		else if (!strcmp(name, "X-GLASSINE-WORDS"))
			words_parse(&x->f, val);
		else if (!strcmp(name, "X-GLASSINE-SHOW")) {
			const char *a = strstr(val, "neaten="), *g = strstr(val, "autograph=");
			x->f.neaten = a ? (!strncmp(a + 7, "on", 2) ? SHOW_ON : !strncmp(a + 7, "off", 3) ? SHOW_OFF : SHOW_FOLLOW) : SHOW_FOLLOW;
			x->f.autograph = g ? (!strncmp(g + 10, "on", 2) ? SHOW_ON : !strncmp(g + 10, "off", 3) ? SHOW_OFF : SHOW_FOLLOW) : SHOW_FOLLOW;
		}
		else if (!strcmp(name, "X-GLASSINE-STATE")) {
			free(x->f.state);
			x->f.state = unb64(val, &x->f.state_len);
		}
	}
	for (int i = 0; i < nm; i++)
		for (int j = 0; j < s->n; j++)
			if (s->e[j]->kind == E_HABIT && !strcmp(s->e[j]->uid, marks[i].uid))
				marks[i].e->habit = s->e[j]->id;
	free(marks);
	free(u.p);
	return true;
}

/* ---- files ---- */

static bool write_atomic(const char *dir, const char *name, const char *data)
{
	char p[4096], t[4200];
	snprintf(p, sizeof p, "%s/%s", dir, name);
	snprintf(t, sizeof t, "%s.new", p);
	int fd = open(t, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return false;
	size_t n = strlen(data), o = 0;
	while (o < n) {
		ssize_t w = write(fd, data + o, n - o);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return false;
		}
		o += w;
	}
	bool ok = fsync(fd) == 0;
	close(fd);
	if (!ok || rename(t, p) < 0)
		return false;
	int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dfd >= 0) {
		fsync(dfd);
		close(dfd);
	}
	return true;
}

static char *read_all(const char *dir, const char *name)
{
	char p[4096];
	snprintf(p, sizeof p, "%s/%s", dir, name);
	FILE *f = fopen(p, "re");
	if (!f)
		return NULL;
	struct buf b = { 0 };
	char tmp[8192];
	size_t n;
	while ((n = fread(tmp, 1, sizeof tmp, f)) > 0)
		bput(&b, tmp, n);
	fclose(f);
	if (!b.p)
		bput(&b, "", 0);
	return b.p;
}

static void mkdirs(const char *dir)
{
	char p[4096];
	snprintf(p, sizeof p, "%s", dir);
	for (char *q = p + 1; *q; q++)
		if (*q == '/') {
			*q = 0;
			mkdir(p, 0700);
			*q = '/';
		}
	mkdir(p, 0700);
}

static struct entry *by_uid(struct store *s, const char *uid)
{
	for (int i = 0; i < s->n; i++)
		if (!strcmp(s->e[i]->uid, uid))
			return s->e[i];
	return NULL;
}

static bool has_uid(char (*l)[48], int n, const char *uid)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(l[i], uid))
			return true;
	return false;
}

/* the entries of o that s doesn't know and didn't delete: moved into s (marks after the
 * habits they name, by uid) */
static void take_foreign(struct store *s, struct store *o)
{
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < o->n; i++) {
			struct entry *x = o->e[i];
			if ((x->kind == E_MARK) != (pass == 1) || by_uid(s, x->uid) || has_uid(s->deleted, s->ndel, x->uid))
				continue;
			uint32_t habit = 0;
			if (x->kind == E_MARK) {
				struct entry *h = store_get(o, x->habit), *mine = h ? by_uid(s, h->uid) : NULL;
				if (!mine)
					continue; /* its habit was deleted here */
				habit = mine->id;
			}
			struct entry *y = store_put(s);
			if (!y)
				return;
			uint32_t id = y->id;
			*y = *x;
			y->id = id;
			y->habit = habit;
			memset(&x->f, 0, sizeof x->f); /* the field now belongs to y */
		}
}

static void seen_set(struct store *s, const struct stat *st)
{
	s->seen = true;
	s->seen_ino = (long long)st->st_ino;
	s->seen_size = (long long)st->st_size;
	s->seen_mtime = (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
}

static bool seen_same(const struct store *s, const struct stat *st)
{
	return s->seen && s->seen_ino == (long long)st->st_ino && s->seen_size == (long long)st->st_size &&
	       s->seen_mtime == (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
}

/* the journal over what planner.ics held: each entry as last noted */
static void replay(struct store *s, const char *text)
{
	struct store o;
	store_init(&o, NULL);
	store_parse_ics(&o, text);
	for (int i = 0; i < o.n; i++) {
		struct entry *x = o.e[i], *y = by_uid(s, x->uid);
		if (x->kind == E_MARK || has_uid(s->deleted, s->ndel, x->uid))
			continue;
		if (!y && !(y = store_put(s)))
			break;
		uint32_t id = y->id;
		field_free(&y->f);
		*y = *x;
		y->id = id;
		y->habit = 0;
		memset(&x->f, 0, sizeof x->f); /* the field now belongs to y */
	}
	store_free(&o);
}

static int jid_of(struct store *s)
{
	if (!s->jid)
		s->jid = (int)getpid();
	return s->jid;
}

static bool has_name(char (*l)[64], int n, const char *name)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(l[i], name))
			return true;
	return false;
}

struct journal {
	char name[64];
	long long mtime;
};

static int by_mtime(const void *a, const void *b)
{
	long long x = ((const struct journal *)a)->mtime, y = ((const struct journal *)b)->mtime;
	return x < y ? -1 : x > y;
}

/* the journals in the data directory, oldest first */
static int journals(struct store *s, struct journal **out)
{
	struct journal *l = NULL;
	int n = 0;
	DIR *d = opendir(s->dir);
	for (struct dirent *e; d && (e = readdir(d));) {
		if (strncmp(e->d_name, "planner.journal.", 16) || strlen(e->d_name) >= 64)
			continue;
		char p[4096];
		struct stat st;
		snprintf(p, sizeof p, "%s/%s", s->dir, e->d_name);
		if (stat(p, &st) < 0)
			continue;
		struct journal *m = realloc(l, (n + 1) * sizeof *l);
		if (!m)
			break;
		l = m;
		snprintf(l[n].name, 64, "%s", e->d_name);
		l[n++].mtime = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
	}
	if (d)
		closedir(d);
	if (n > 1)
		qsort(l, n, sizeof *l, by_mtime);
	*out = l;
	return n;
}

bool store_save(struct store *s)
{
	if (!s->dir)
		return true;
	mkdirs(s->dir);
	/* one writer at a time: read, merge and write under the lock */
	char p[4096];
	snprintf(p, sizeof p, "%s/.lock", s->dir);
	int lk = open(p, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (lk >= 0)
		while (flock(lk, LOCK_EX) < 0 && errno == EINTR)
			;
	snprintf(p, sizeof p, "%s/planner.ics", s->dir);
	struct stat st;
	if (stat(p, &st) == 0 && !seen_same(s, &st)) {
		char *t = read_all(s->dir, "planner.ics");
		if (t) {
			struct store o;
			store_init(&o, NULL);
			store_parse_ics(&o, t);
			take_foreign(s, &o);
			store_free(&o);
			free(t);
		}
	}
	/* the other instances' journals: a running one's entries this one doesn't know; an
	 * ended one's whole, unless replayed at load already */
	char (*drop)[64] = NULL;
	int ndrop = 0;
	struct journal *jl = NULL;
	int nj = journals(s, &jl);
	for (int i = 0; i < nj; i++) {
		int pid = atoi(jl[i].name + sizeof "planner.journal");
		if (pid == jid_of(s))
			continue;
		bool alive = pid > 0 && (kill(pid, 0) == 0 || errno == EPERM);
		bool seen = has_name(s->replayed, s->nrep, jl[i].name);
		if (!alive && !seen) {
			char *j = read_all(s->dir, jl[i].name);
			if (j)
				replay(s, j);
			free(j);
		} else if (alive) {
			char *j = read_all(s->dir, jl[i].name);
			if (j) {
				struct store o;
				store_init(&o, NULL);
				store_parse_ics(&o, j);
				take_foreign(s, &o);
				store_free(&o);
			}
			free(j);
		}
		if (!alive) {
			char (*d)[64] = realloc(drop, (ndrop + 1) * sizeof *drop);
			if (d) {
				drop = d;
				snprintf(drop[ndrop++], 64, "%s", jl[i].name);
			}
		}
	}
	free(jl);
	char *t = store_ics(s);
	bool ok = t && write_atomic(s->dir, "planner.ics", t);
	free(t);
	if (ok && stat(p, &st) == 0)
		seen_set(s, &st);
	if (ok) { /* planner.ics holds them now */
		snprintf(p, sizeof p, "%s/planner.journal.%d", s->dir, jid_of(s));
		unlink(p);
		for (int i = 0; i < ndrop; i++) {
			snprintf(p, sizeof p, "%s/%s", s->dir, drop[i]);
			unlink(p);
		}
		free(s->replayed);
		s->replayed = NULL;
		s->nrep = 0;
	}
	free(drop);
	if (lk >= 0)
		close(lk);
	return ok;
}

bool store_note(struct store *s, uint32_t id)
{
	struct entry *x = store_get(s, id);
	if (!s->dir || !x)
		return true;
	if (x->kind == E_MARK)
		return store_save(s);
	mkdirs(s->dir);
	struct buf b = { 0 };
	entry_ics(&b, x);
	char p[4096];
	snprintf(p, sizeof p, "%s/.lock", s->dir);
	int lk = open(p, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (lk >= 0)
		while (flock(lk, LOCK_EX) < 0 && errno == EINTR)
			;
	snprintf(p, sizeof p, "%s/planner.journal.%d", s->dir, jid_of(s));
	int fd = open(p, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
	bool ok = fd >= 0;
	for (size_t o = 0; ok && o < b.n;) {
		ssize_t w = write(fd, b.p + o, b.n - o);
		if (w < 0 && errno == EINTR)
			continue;
		ok = w > 0;
		o += w > 0 ? (size_t)w : 0;
	}
	if (fd >= 0) {
		ok = fdatasync(fd) == 0 && ok;
		close(fd);
	}
	if (lk >= 0)
		close(lk);
	free(b.p);
	return ok;
}

bool store_save_choices(struct store *s)
{
	if (!s->dir)
		return true;
	mkdirs(s->dir);
	struct buf b = { 0 };
	static const char hd[] = "# the template each period uses: KIND FIRST-DAY LAST-DAY|onwards TEMPLATE; default KIND TEMPLATE\n";
	bput(&b, hd, sizeof hd - 1);
	char l[200];
	for (int k = 0; k < 3; k++)
		if (s->def[k][0]) {
			snprintf(l, sizeof l, "default %s %s\n", pkind_name(k), s->def[k]);
			bput(&b, l, strlen(l));
		}
	for (int i = 0; i < s->nc; i++) {
		char a[16], z[16];
		fmt_date(a, sizeof a, s->c[i].from.start);
		if (s->c[i].to >= 0)
			fmt_date(z, sizeof z, s->c[i].to);
		else
			snprintf(z, sizeof z, "onwards");
		snprintf(l, sizeof l, "choose %s %s %s %s\n", pkind_name(s->c[i].from.kind), a, z, s->c[i].tmpl);
		bput(&b, l, strlen(l));
	}
	bool ok = write_atomic(s->dir, "templates.conf", b.p);
	free(b.p);
	return ok;
}

bool store_save_settings(struct store *s)
{
	if (!s->dir)
		return true;
	mkdirs(s->dir);
	char b[256];
	snprintf(b, sizeof b, "# the planner's settings\nneaten %d\nstrength %.2f\nautograph %d\n", s->neaten, s->strength,
		 s->autograph);
	return write_atomic(s->dir, "settings.conf", b);
}

bool store_load(struct store *s)
{
	if (!s->dir)
		return true;
	char p[4096];
	struct stat st;
	snprintf(p, sizeof p, "%s/planner.ics", s->dir);
	bool have = stat(p, &st) == 0;
	char *t = read_all(s->dir, "planner.ics");
	if (t) {
		store_parse_ics(s, t);
		free(t);
		if (have)
			seen_set(s, &st);
	}
	struct journal *jl = NULL;
	int nj = journals(s, &jl);
	for (int i = 0; i < nj; i++) {
		t = read_all(s->dir, jl[i].name);
		if (t)
			replay(s, t);
		free(t);
		char (*r)[64] = realloc(s->replayed, (s->nrep + 1) * sizeof *r);
		if (r) {
			s->replayed = r;
			snprintf(s->replayed[s->nrep++], 64, "%s", jl[i].name);
		}
	}
	free(jl);
	t = read_all(s->dir, "settings.conf");
	if (t) {
		for (char *l = strtok(t, "\n"); l; l = strtok(NULL, "\n")) {
			int v;
			float f;
			if (sscanf(l, "neaten %d", &v) == 1)
				s->neaten = v != 0;
			else if (sscanf(l, "autograph %d", &v) == 1)
				s->autograph = v != 0;
			else if (sscanf(l, "strength %f", &f) == 1 && f >= 0 && f <= 1)
				s->strength = f;
		}
		free(t);
	}
	t = read_all(s->dir, "templates.conf");
	if (t) {
		for (char *l = strtok(t, "\n"); l; l = strtok(NULL, "\n")) {
			char k[16], a[16], z[16], name[40];
			int kind;
			if (sscanf(l, "default %15s %39s", k, name) == 2 && (kind = pkind_parse(k)) >= 0)
				snprintf(s->def[kind], sizeof s->def[kind], "%s", name);
			else if (sscanf(l, "choose %15s %15s %15s %39s", k, a, z, name) == 4 && (kind = pkind_parse(k)) >= 0) {
				int d0, d1, m;
				if (parse_dt(a, &d0, &m) < 0)
					continue;
				int to = strcmp(z, "onwards") ? (parse_dt(z, &d1, &m) < 0 ? d0 : d1) : -1;
				store_choose(s, (struct period){ kind, d0 }, to, name);
			}
		}
		free(t);
	}
	return true;
}

/* ---- search ---- */

static bool contains_ci(const char *h, const char *n, size_t len)
{
	for (; *h; h++) {
		size_t i = 0;
		while (i < len && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)n[i]))
			i++;
		if (i == len)
			return true;
	}
	return false;
}

int store_search(struct store *s, const char *q, struct entry **out, int max)
{
	int n = 0;
	for (int i = s->n - 1; i >= 0 && n < max; i--) {
		const char *v = s->e[i]->f.value;
		if (!v || !*q)
			continue;
		bool all = true;
		const char *w = q;
		while (*w) {
			while (*w == ' ')
				w++;
			size_t len = strcspn(w, " ");
			if (len && !contains_ci(v, w, len)) {
				all = false;
				break;
			}
			w += len;
		}
		if (all)
			out[n++] = s->e[i];
	}
	return n;
}
