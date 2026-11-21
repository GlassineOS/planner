/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Templates (spike): an indented text file, one node per line.
 *
 *   # a comment
 *   name  Day · agenda and lists
 *   kind  day
 *
 *   column
 *     row weight=3
 *       agenda grow weight=3 from=7 to=21
 *       column weight=2
 *         todo period=day title=Today
 *         todo period=week title="This week"
 *     notes period=day grow
 *
 * A node is `row`, `column` or a widget's name; its words are key=value settings
 * (quoted values may hold spaces) or bare flags (grow). Children are indented by two
 * spaces more than their parent. Layout keys any node may carry: weight, min_w, min_h,
 * pref_h, grow; everything else is the widget's own setting (FR-042).
 */
#ifndef PL_TMPL_H
#define PL_TMPL_H

#include <stdbool.h>

#define TMPL_MAX_ATTR 12
#define TMPL_MAX_NODES 48

struct tnode {
	char type[24];
	struct { char k[24], v[64]; } a[TMPL_MAX_ATTR];
	int na;
	int kids[16];
	int nkids;
	int line;
};

struct tmpl {
	char id[48];   /* the file's name without .tmpl */
	char name[80];
	int kind;      /* enum pkind */
	struct tnode n[TMPL_MAX_NODES];
	int nn;        /* n[0] is the root */
};

/* parses text; on error returns false with a message naming the line */
bool tmpl_parse(struct tmpl *t, const char *id, const char *text, char *err, int errlen);
bool tmpl_load(struct tmpl *t, const char *path, char *err, int errlen);
const char *tnode_get(const struct tnode *n, const char *k);
double tnode_num(const struct tnode *n, const char *k, double def);
bool tnode_flag(const struct tnode *n, const char *k);

/* every *.tmpl in dir, sorted by id */
int tmpl_load_dir(struct tmpl *out, int max, const char *dir, char *err, int errlen);

#endif
