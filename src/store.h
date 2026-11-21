/* SPDX-License-Identifier: GPL-2.0-or-later
 * The planner's data (spec 001 FR-020 to FR-028): entries keyed by their date or period,
 * their kind and, for free text, their slot. Each entry's handwriting field holds its ink
 * (strokes in the field's own coordinates, numbered as the writing region numbered them),
 * its value (the reading, the person's text-mode edits applied) and the region's saved state.
 * Entries are kept as iCalendar objects (VEVENT, VTODO, VJOURNAL) in one file; the template
 * choices in another, so no template operation can touch an entry. No cairo, no Wayland. */
#ifndef PL_STORE_H
#define PL_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- dates: days since 1970-01-01, local wall-clock ---- */
int day_from_ymd(int y, int m, int d);
void day_to_ymd(int day, int *y, int *m, int *d);
int day_weekday(int day); /* 0 = Monday */
int days_in_month(int y, int m);
int day_today(void);
int week_number(int day); /* ISO 8601 */

enum pkind { P_DAY, P_WEEK, P_MONTH };
struct period {
	enum pkind kind;
	int start; /* its first day */
};
struct period period_of(enum pkind k, int day);
int period_end(struct period p); /* its last day */
struct period period_step(struct period p, int n);
bool period_eq(struct period a, struct period b);
const char *pkind_name(enum pkind k);
int pkind_parse(const char *s);

/* ---- a handwriting field ---- */
struct pl_point {
	float x, y, p; /* field coordinates, pressure 0..1 */
	uint32_t t;
};
struct pl_stroke {
	uint32_t k; /* the region's number for it */
	struct pl_point *pt;
	int n;
};
/* a word of the value, as the region laid it out: an ink word is its strokes moved by dx, dy;
 * a set word (set in Autograph, the person's font: edited or added in text mode) is an outline,
 * path (op codes 0 move, 1 line, 2 quad, 3 cubic, 4 close, each followed by its coordinates),
 * in box. Field coordinates. */
struct pl_word {
	uint32_t start, len; /* bytes of the value */
	bool set;
	uint32_t *ks;
	int nks;
	float dx, dy;
	float box[4];        /* x, y, width, height */
	float *path;
	int npath;
};
/* the person's display opt-ins (FR-011): a field's own choice, else the settings' */
enum { SHOW_FOLLOW, SHOW_OFF, SHOW_ON };
struct field {
	struct pl_stroke *s;
	int ns;
	char *value;  /* the field's current text (the reading with edits): what search uses */
	uint8_t *state; /* the region's saved state, opaque */
	size_t state_len;
	struct pl_word *w; /* the value's word layout (none: the strokes as written) */
	int nw;
	struct pl_stroke *neat; /* the neatened strokes, by the raw stroke's number */
	int nneat;
	uint8_t neaten, autograph; /* SHOW_* */
};
void field_free(struct field *f);
void field_add_stroke(struct field *f, uint32_t k, const struct pl_point *p, int n);
void field_remove_stroke(struct field *f, uint32_t k);
void field_set_value(struct field *f, const char *text, const void *state, size_t len);
/* the word layout (copied); n 0 clears it */
void field_set_words(struct field *f, const struct pl_word *w, int n);
void field_set_neat(struct field *f, uint32_t k, const struct pl_point *p, int n);
const struct pl_stroke *field_neat(const struct field *f, uint32_t k);
/* the word an ink stroke belongs to, or -1 (ink that is no word: a drawing) */
int field_word_of(const struct field *f, uint32_t k);
bool field_empty(const struct field *f);
/* the ink's extent (right, bottom) in field coordinates; 0 when empty */
void field_extent(const struct field *f, double *right, double *bottom);

/* ---- entries ---- */
enum ekind { E_EVENT, E_TODO, E_NOTE, E_PRIO, E_HABIT, E_MARK, E_NKINDS };

struct entry {
	uint32_t id;     /* local; the file keeps uid */
	char uid[48];
	enum ekind kind;
	struct period per; /* to-do, note, priority: their period; event, mark: their day (P_DAY) */
	int start, end;    /* timed event: minutes from per.start's midnight, end may pass 24:00 */
	bool allday;       /* event over whole days: start, end in days from per.start (end exclusive) */
	bool done;         /* to-do */
	int rank;          /* priority: 1..; to-do: its order */
	long long age;     /* when it was made (unix seconds): the oldest open gives way first */
	char slot[32];     /* note: its slot */
	uint32_t habit;    /* mark: the habit's id */
	struct field f;
};

struct choice {
	struct period from; /* a single period: from == to */
	int to;             /* the last day it covers, or -1: onwards */
	char tmpl[40];
};

struct store {
	struct entry **e;
	int n, cap;
	uint32_t next_id;
	struct choice *c;
	int nc;
	char def[3][40];   /* each kind's default template ("" = the built-in default) */
	char *dir;         /* NULL: in memory only (tests, renders) */
	/* the settings (FR-053): the display opt-ins for every field (FR-011) */
	bool neaten, autograph;
	float strength;    /* neatening, 0..1 */
	/* the entries' file as this store last read or wrote it: a save finds another writer's
	 * entries (another instance) by a change of it, and keeps them */
	bool seen;
	long long seen_ino, seen_size, seen_mtime;
	char (*deleted)[48]; /* uids deleted here: another writer's copy of them stays deleted */
	int ndel;
	/* the journal (store_note): this instance's is planner.journal.<jid> (jid: the process
	 * id unless set); the journals of instances that ended without saving, replayed at load */
	int jid;
	char (*replayed)[64];
	int nrep;
};

void store_init(struct store *s, const char *dir);
void store_free(struct store *s);
struct entry *store_add(struct store *s, enum ekind k, struct period per);
struct entry *store_get(struct store *s, uint32_t id);
void store_delete(struct store *s, uint32_t id);
/* entries of kind k whose period is exactly p (slot for notes, NULL any), in order */
int store_list(struct store *s, enum ekind k, struct period p, const char *slot, struct entry **out, int max);
/* timed and all-day events touching day */
int store_events(struct store *s, int day, struct entry **out, int max);

/* ---- a field's writing region applied to the store (wl.c; tests drive it without Wayland) ---- */

/* what a field makes when a stroke comes and it has no entry: an empty row's entry, or the
 * same again when its entry was emptied (struck out) and deleted. kind E_NKINDS: nothing */
struct fmake {
	enum ekind kind;
	struct period per;
	char slot[32];
	int rank;
};
/* a stroke for the field whose entry is *entry (0: none yet): made when there is none, the
 * stroke kept with it; *entry set. NULL when nothing is to be made */
struct entry *field_stroke(struct store *s, uint32_t *entry, const struct fmake *mk, uint32_t k,
			   const struct pl_point *p, int n);
/* the region's value: strokes that left (gone), the text and the saved state. An entry other
 * than an event whose ink and text are all gone is deleted, *entry = 0 (true) */
bool field_value(struct store *s, uint32_t *entry, const char *text, const struct pl_word *w, int nw,
		 const uint32_t *gone, size_t ngone, const void *state, size_t len);
/* text typed in text mode into a field with no entry yet (an empty row): its entry, made
 * with the text as its value until the region's value comes. NULL when nothing is made */
struct entry *field_typed(struct store *s, uint32_t *entry, const struct fmake *mk, const char *text);
/* a field's opt-in, its own choice or else the settings' */
bool field_neaten(const struct store *s, const struct field *f);
bool field_autograph(const struct store *s, const struct field *f);

/* the template a period uses, and how it got it: 0 its own, 1 a range, 2 onwards, 3 default */
const char *store_template(const struct store *s, struct period p, int *how);
void store_choose(struct store *s, struct period from, int to, const char *tmpl);
/* the choice i gone (its periods use the next choice that covers them, else the default) */
void store_unchoose(struct store *s, int i);
/* entry id moved to the period of its kind that holds day (an event keeps its times) */
bool store_move(struct store *s, uint32_t id, int day);

/* the files (FR-024: written at once, atomically) */
bool store_load(struct store *s);
/* entries; when the file changed since this store read or wrote it, the entries only the
 * file has (written by another instance) are taken in first, except those deleted here */
bool store_save(struct store *s);
/* entry id as it is now, kept at once (FR-024) without writing the whole file: appended to
 * this instance's journal and synced. Marks and deletes are not journalled (save them).
 * store_load replays every journal after planner.ics, the latest of each entry winning.
 * store_save writes planner.ics with what a running instance only journalled (entries it
 * doesn't know) and what an ended one left, then removes its own journal and the ended
 * ones'; a running instance's journal stays until that instance saves */
bool store_note(struct store *s, uint32_t id);
bool store_save_choices(struct store *s);
bool store_save_settings(struct store *s);
/* the entries' file as text (tests: a template switch must leave it byte for byte) */
char *store_ics(const struct store *s);
bool store_parse_ics(struct store *s, const char *text);

/* search (FR-050): entries whose current text contains every word of q, newest first */
int store_search(struct store *s, const char *q, struct entry **out, int max);

#endif
