/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The planner's data (spike): entries that belong to dates (FR-020), their
 * handwriting fields with ink, a stand-in reading and text-mode edits, and the
 * template chosen per period. Entries and template choices are kept in two files,
 * so "switching templates changes no entry" is a byte comparison of one of them.
 * No cairo, no Wayland.
 */
#ifndef PL_MODEL_H
#define PL_MODEL_H

#include <stdbool.h>
#include <stddef.h>

/* ---- dates: days since 1970-01-01 ------------------------------------------------- */
int day_from_ymd(int y, int m, int d);
void day_to_ymd(int day, int *y, int *m, int *d);
int day_weekday(int day); /* 0 = Monday (FR-003: the system's first day; the spike fixes Monday) */
int day_parse(const char *s); /* YYYY-MM-DD; -1 on error */
void day_format(int day, char *buf, size_t n);
int days_in_month(int y, int m);

enum pkind { P_DAY, P_WEEK, P_MONTH };
struct period { enum pkind kind; int start; }; /* start: the first day */
struct period period_of(enum pkind k, int day);
int period_end(struct period p); /* the last day */
bool period_contains(struct period outer, struct period inner);
const char *pkind_name(enum pkind k);
int pkind_parse(const char *s);

/* ---- ink ---------------------------------------------------------------------------- */
struct pt { float x, y, p; };
struct stroke { struct pt *pt; int n, cap; };

/*
 * A handwriting field: its ink in the field's own coordinates (logical px, never
 * scaled: FR-047), the stand-in reading, the text-mode text when edited.
 */
#define FIELD_TEXT 256
struct field {
	int id;
	struct stroke *s;
	int ns, cap;
	char reading[FIELD_TEXT]; /* stand-in for the handwriting service */
	char text[FIELD_TEXT];    /* the text-mode edit ("" + !edited: the reading) */
	bool edited;
	bool textmode;            /* this field's own text-mode toggle */
	int read_strokes;         /* strokes the reading covers (a stale reading: < ns) */
	long long ink_ms;         /* when its ink last changed (the reading waits for a pause) */
};
void field_add_stroke(struct field *f, const struct stroke *s);
/* ink extent in field coordinates: bottom and right edge (0 when empty) */
void field_extent(const struct field *f, double *right, double *bottom);
const char *field_text(const struct field *f); /* the current text (FR-013) */
bool field_empty(const struct field *f);
/* the stand-in reading: words from the strokes' horizontal gaps, each a placeholder */
void field_read_stub(struct field *f);

/* ---- entries ---------------------------------------------------------------------- */
enum ekind { E_EVENT, E_TODO, E_NOTE, E_NKINDS };
const char *ekind_name(enum ekind k, bool plural);

struct entry {
	int id;
	enum ekind kind;
	struct period per; /* to-do, note: their period; event: its day (P_DAY) */
	int start, end;    /* event: minutes from the day's start */
	bool done;         /* to-do */
	int order;         /* to-do: its place in its list */
	bool deleted;
	struct field f;
};

#define STORE_MAX 512
struct store {
	struct entry e[STORE_MAX];
	int n;
	int next_id;
	/* template choices: per period, else the kind's default */
	struct { struct period per; char tmpl[48]; } choice[256];
	int nchoice;
	char def[3][48];
};

void store_init(struct store *s);
void store_free(struct store *s);
struct entry *store_add(struct store *s, enum ekind k, struct period per);
struct entry *store_find(struct store *s, int id);
const char *store_choice(const struct store *s, struct period p);
void store_choose(struct store *s, struct period p, const char *tmpl);

/* the files: entries (FR-051: written after every stroke) and template choices */
bool store_save_entries(const struct store *s, const char *path);
bool store_save_choices(const struct store *s, const char *path);
bool store_load(struct store *s, const char *entries, const char *choices);
/* the entries' file as text (tests compare it before and after) */
char *store_entries_text(const struct store *s);

/* ---- synthetic ink (tests, renders: never a person's) -------------------------------- */
/* n words of loops on a line at baseline y from x, x-height xh; seeded */
void synth_words(struct field *f, double x, double y, double xh, int words, unsigned seed);

#endif
