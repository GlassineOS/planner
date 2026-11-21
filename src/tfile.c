// SPDX-License-Identifier: GPL-2.0-or-later
/* The person's templates, one file each (tfile.h). */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tfile.h"

static const char *const WK[W_NWIDGETS] = { "agenda", "cagenda", "todo", "notes", "habits", "minimonth", "weekov", "priorities" };

const char *wkind_name(enum wkind k)
{
	return k < W_NWIDGETS ? WK[k] : "?";
}

int wkind_parse(const char *s)
{
	for (int i = 0; i < W_NWIDGETS; i++)
		if (!strcmp(s, WK[i]))
			return i;
	return -1;
}

/* ---- heap templates ---- */

void tmpl_set_str(const char **dst, const char *s)
{
	free((char *)*dst);
	*dst = s ? strdup(s) : NULL;
}

struct tnode *tnode_new(enum nkind k)
{
	struct tnode *n = calloc(1, sizeof *n);
	if (n) {
		n->k = k;
		n->land = k == N_WIDGET ? N_WIDGET : k;
		n->period = -1;
	}
	return n;
}

void tnode_free(struct tnode *n)
{
	if (!n)
		return;
	for (int i = 0; i < n->nc; i++)
		tnode_free(n->c[i]);
	free((char *)n->slot);
	free((char *)n->title);
	free(n);
}

static struct tnode *node_clone(const struct tnode *n)
{
	struct tnode *c = malloc(sizeof *c);
	if (!c)
		return NULL;
	*c = *n;
	c->slot = n->slot ? strdup(n->slot) : NULL;
	c->title = n->title ? strdup(n->title) : NULL;
	c->nc = 0;
	for (int i = 0; i < n->nc; i++) {
		struct tnode *k = node_clone(n->c[i]);
		if (k)
			c->c[c->nc++] = k;
	}
	return c;
}

struct tmpl *tmpl_clone(const struct tmpl *t)
{
	struct tmpl *c = calloc(1, sizeof *c);
	if (!c)
		return NULL;
	c->kind = t->kind;
	c->id = t->id ? strdup(t->id) : NULL;
	c->name = t->name ? strdup(t->name) : NULL;
	c->note = t->note ? strdup(t->note) : NULL;
	c->root = t->root ? node_clone(t->root) : tnode_new(N_COL);
	return c;
}

void tmpl_free(struct tmpl *t)
{
	if (!t)
		return;
	free((char *)t->id);
	free((char *)t->name);
	free((char *)t->note);
	tnode_free(t->root);
	free(t);
}

/* ---- text ---- */

struct buf {
	char *p;
	size_t n, cap;
};

static void put(struct buf *b, const char *s)
{
	size_t n = strlen(s);
	if (b->n + n + 1 > b->cap) {
		size_t c = b->cap ? b->cap * 2 : 1024;
		while (c < b->n + n + 1)
			c *= 2;
		char *p = realloc(b->p, c);
		if (!p)
			return;
		b->p = p;
		b->cap = c;
	}
	memcpy(b->p + b->n, s, n + 1);
	b->n += n;
}

static void put_q(struct buf *b, const char *s)
{
	put(b, "\"");
	for (; *s; s++) {
		char c[3] = { *s, 0, 0 };
		if (*s == '"' || *s == '\\')
			c[0] = '\\', c[1] = *s;
		if (*s == '\n')
			c[0] = ' ';
		put(b, c);
	}
	put(b, "\"");
}

static const char *nk_name(enum nkind k)
{
	return k == N_ROW ? "row" : "col";
}

static void node_text(struct buf *b, const struct tnode *n, int depth)
{
	char l[96];
	for (int i = 0; i < depth; i++)
		put(b, " ");
	put(b, n->k == N_WIDGET ? wkind_name(n->wg) : nk_name(n->k));
	if (n->k != N_WIDGET && n->land != n->k && n->land != N_WIDGET) {
		put(b, " land=");
		put(b, nk_name(n->land));
	}
	if (n->w > 0) {
		snprintf(l, sizeof l, " w=%g", n->w);
		put(b, l);
	}
	if (n->grow)
		put(b, " grow");
	if (n->k == N_WIDGET) {
		if (n->period >= 0) {
			put(b, " period=");
			put(b, pkind_name(n->period));
		}
		if (n->slot) {
			put(b, " slot=");
			put_q(b, n->slot);
		}
		if (n->title) {
			put(b, " title=");
			put_q(b, n->title);
		}
		if (n->wg == W_AGENDA || n->wg == W_CAGENDA) {
			snprintf(l, sizeof l, " from=%d to=%d", n->from, n->to);
			put(b, l);
		}
		if (n->wg == W_CAGENDA) {
			snprintf(l, sizeof l, " day=%d", n->day);
			put(b, l);
		}
		if (n->large)
			put(b, " large");
	}
	put(b, "\n");
	for (int i = 0; i < n->nc; i++)
		node_text(b, n->c[i], depth + 1);
}

char *tmpl_to_text(const struct tmpl *t)
{
	struct buf b = { 0 };
	put(&b, "# glassine-planner template 1\nid ");
	put(&b, t->id ? t->id : "");
	put(&b, "\nkind ");
	put(&b, pkind_name(t->kind));
	put(&b, "\nname ");
	put_q(&b, t->name ? t->name : "");
	put(&b, "\n");
	if (t->note && *t->note) {
		put(&b, "note ");
		put_q(&b, t->note);
		put(&b, "\n");
	}
	if (t->root)
		node_text(&b, t->root, 0);
	return b.p;
}

/* the next token of a line: a word, KEY=VALUE or KEY="quoted"; key and value unquoted */
static const char *token(const char *s, char *key, int nk, char *val, int nv)
{
	while (*s == ' ' || *s == '\t')
		s++;
	if (!*s)
		return NULL;
	int i = 0;
	while (*s && *s != ' ' && *s != '\t' && *s != '=') {
		if (i < nk - 1)
			key[i++] = *s;
		s++;
	}
	key[i] = 0;
	val[0] = 0;
	if (*s != '=')
		return s;
	s++;
	i = 0;
	if (*s == '"') {
		s++;
		while (*s && *s != '"') {
			if (*s == '\\' && s[1])
				s++;
			if (i < nv - 1)
				val[i++] = *s;
			s++;
		}
		if (*s == '"')
			s++;
	} else
		while (*s && *s != ' ' && *s != '\t') {
			if (i < nv - 1)
				val[i++] = *s;
			s++;
		}
	val[i] = 0;
	return s;
}

static bool valid_id(const char *s)
{
	if (!*s || strlen(s) >= TMPL_ID)
		return false;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_'))
			return false;
	return true;
}

/* a container with no children is dropped; one with a single child stays (it may be the root) */
static bool tree_ok(const struct tnode *n)
{
	if (n->k == N_WIDGET)
		return true;
	for (int i = 0; i < n->nc; i++)
		if (!tree_ok(n->c[i]))
			return false;
	return true;
}

struct tmpl *tmpl_from_text(const char *text)
{
	struct tmpl *t = calloc(1, sizeof *t);
	if (!t)
		return NULL;
	int kind = -1, widgets = 0;
	struct tnode *stack[16];
	int depth_of[16], sp = 0;
	bool bad = false;
	const char *l = text;
	while (l && *l && !bad) {
		const char *end = strchr(l, '\n');
		size_t len = end ? (size_t)(end - l) : strlen(l);
		char line[512];
		if (len >= sizeof line)
			len = sizeof line - 1;
		memcpy(line, l, len);
		line[len] = 0;
		l = end ? end + 1 : NULL;
		int depth = 0;
		while (line[depth] == ' ')
			depth++;
		char key[32], val[256];
		const char *p = token(line, key, sizeof key, val, sizeof val);
		if (!p || key[0] == '#')
			continue;
		if (!t->root && depth == 0) {
			char rest[256];
			if (!strcmp(key, "id") || !strcmp(key, "kind") || !strcmp(key, "name") || !strcmp(key, "note")) {
				/* the value is the rest of the line, quoted or not */
				const char *r = p;
				while (*r == ' ')
					r++;
				if (*r == '"') {
					char k2[4];
					char tmp[300];
					snprintf(tmp, sizeof tmp, "x=%s", r);
					token(tmp, k2, sizeof k2, rest, sizeof rest);
				} else
					snprintf(rest, sizeof rest, "%s", r);
				if (!strcmp(key, "id"))
					tmpl_set_str(&t->id, valid_id(rest) ? rest : NULL);
				else if (!strcmp(key, "kind"))
					kind = pkind_parse(rest);
				else if (!strcmp(key, "name"))
					tmpl_set_str(&t->name, rest);
				else
					tmpl_set_str(&t->note, rest);
				continue;
			}
		}
		enum nkind nk;
		int wg = -1;
		if (!strcmp(key, "row"))
			nk = N_ROW;
		else if (!strcmp(key, "col"))
			nk = N_COL;
		else if ((wg = wkind_parse(key)) >= 0)
			nk = N_WIDGET;
		else
			continue;
		struct tnode *n = tnode_new(nk);
		if (!n)
			break;
		if (wg >= 0) {
			n->wg = wg;
			if (wg == W_AGENDA || wg == W_CAGENDA)
				n->from = 7, n->to = 21;
		}
		while ((p = token(p, key, sizeof key, val, sizeof val))) {
			if (!strcmp(key, "w"))
				n->w = atof(val);
			else if (!strcmp(key, "grow"))
				n->grow = true;
			else if (!strcmp(key, "large"))
				n->large = true;
			else if (!strcmp(key, "land") && nk != N_WIDGET)
				n->land = !strcmp(val, "row") ? N_ROW : N_COL;
			else if (!strcmp(key, "period"))
				n->period = pkind_parse(val);
			else if (!strcmp(key, "slot") && *val)
				tmpl_set_str(&n->slot, val);
			else if (!strcmp(key, "title") && *val)
				tmpl_set_str(&n->title, val);
			else if (!strcmp(key, "from"))
				n->from = atoi(val);
			else if (!strcmp(key, "to"))
				n->to = atoi(val);
			else if (!strcmp(key, "day"))
				n->day = atoi(val);
		}
		if (n->w < 0 || n->w > 100)
			n->w = 0;
		if (n->from < 0 || n->from > 23 || n->to <= n->from || n->to > 24)
			n->from = 7, n->to = 21;
		if (n->day < 0 || n->day > 6)
			n->day = 0;
		while (sp > 0 && depth_of[sp - 1] >= depth)
			sp--;
		if (!t->root) {
			if (depth != 0 || nk == N_WIDGET) { /* the root is a row or a column */
				struct tnode *r = tnode_new(N_COL);
				if (!r) {
					tnode_free(n);
					break;
				}
				t->root = r;
				stack[0] = r, depth_of[0] = -1, sp = 1;
			} else
				t->root = n;
		} else if (sp == 0) { /* a second root: the file is broken */
			tnode_free(n);
			bad = true;
			break;
		}
		if (n != t->root) {
			struct tnode *parent = stack[sp - 1];
			if (parent->k == N_WIDGET || parent->nc >= TN_KIDS || widgets >= GRID_MAX) {
				tnode_free(n);
				continue;
			}
			parent->c[parent->nc++] = n;
		}
		widgets += nk == N_WIDGET;
		if (nk != N_WIDGET && sp < 16)
			stack[sp] = n, depth_of[sp] = depth, sp++;
	}
	if (bad || kind < 0 || !t->id || !t->root || !tree_ok(t->root)) {
		tmpl_free(t);
		return NULL;
	}
	t->kind = kind;
	if (!t->name)
		tmpl_set_str(&t->name, t->id);
	return t;
}

/* ---- the person's templates ---- */

#define USER_MAX 64
static struct tmpl *user[USER_MAX];
static int nuser;

void tmpl_user_clear(void)
{
	for (int i = 0; i < nuser; i++)
		tmpl_free(user[i]);
	nuser = 0;
}

const struct tmpl *tmpl_user_find(const char *id)
{
	for (int i = 0; id && i < nuser; i++)
		if (!strcmp(user[i]->id, id))
			return user[i];
	return NULL;
}

bool tmpl_is_shipped(const char *id)
{
	for (int i = 0; id && i < pl_ntemplates; i++)
		if (!strcmp(pl_templates[i]->id, id))
			return true;
	return false;
}

static int by_name(const struct dirent **a, const struct dirent **b)
{
	return strverscmp((*a)->d_name, (*b)->d_name);
}

static void keep(struct tmpl *t)
{
	for (int i = 0; i < nuser; i++)
		if (!strcmp(user[i]->id, t->id)) {
			tmpl_free(user[i]);
			user[i] = t;
			return;
		}
	if (nuser < USER_MAX)
		user[nuser++] = t;
	else
		tmpl_free(t);
}

void tmpl_user_load(const char *dir)
{
	tmpl_user_clear();
	if (!dir)
		return;
	char p[4096];
	snprintf(p, sizeof p, "%s/templates", dir);
	struct dirent **ls;
	int n = scandir(p, &ls, NULL, by_name);
	for (int i = 0; i < n; i++) {
		const char *nm = ls[i]->d_name;
		size_t len = strlen(nm);
		if (len > 5 && !strcmp(nm + len - 5, ".tmpl")) {
			char f[4400];
			snprintf(f, sizeof f, "%s/%s", p, nm);
			FILE *fp = fopen(f, "re");
			if (fp) {
				char *text = calloc(1, 65537);
				if (text) {
					size_t got = fread(text, 1, 65536, fp);
					text[got] = 0;
					struct tmpl *t = tmpl_from_text(text);
					/* the file's name is its id */
					if (t && strlen(t->id) == len - 5 && !strncmp(t->id, nm, len - 5))
						keep(t);
					else
						tmpl_free(t);
					free(text);
				}
				fclose(fp);
			}
		}
		free(ls[i]);
	}
	if (n >= 0)
		free(ls);
}

static bool write_file(const char *dir, const char *id, const char *data)
{
	char d[4096], p[4200], t[4300];
	snprintf(d, sizeof d, "%s/templates", dir);
	mkdir(dir, 0700);
	mkdir(d, 0700);
	snprintf(p, sizeof p, "%s/%s.tmpl", d, id);
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
			unlink(t);
			return false;
		}
		o += (size_t)w;
	}
	if (fsync(fd) < 0 || close(fd) < 0) {
		unlink(t);
		return false;
	}
	return rename(t, p) == 0;
}

bool tmpl_user_put(const char *dir, struct tmpl *t)
{
	if (!t || !t->id || !valid_id(t->id)) {
		tmpl_free(t);
		return false;
	}
	bool ok = true;
	if (dir) {
		char *text = tmpl_to_text(t);
		ok = text && write_file(dir, t->id, text);
		free(text);
	}
	keep(t);
	return ok;
}

bool tmpl_user_remove(const char *dir, const char *id)
{
	int i = 0;
	while (i < nuser && strcmp(user[i]->id, id))
		i++;
	if (i == nuser)
		return false;
	tmpl_free(user[i]);
	memmove(&user[i], &user[i + 1], (nuser - i - 1) * sizeof *user);
	nuser--;
	if (dir && valid_id(id)) {
		char p[4200];
		snprintf(p, sizeof p, "%s/templates/%s.tmpl", dir, id);
		if (unlink(p) < 0 && errno != ENOENT)
			return false;
	}
	return true;
}

void tmpl_new_id(char *out, int len)
{
	int max = 0;
	for (int i = 0; i < nuser; i++)
		if (!strncmp(user[i]->id, "own-", 4)) {
			int k = atoi(user[i]->id + 4);
			if (k > max)
				max = k;
		}
	snprintf(out, len, "own-%d", max + 1);
}

int tmpl_list(enum pkind k, const struct tmpl **out, int max)
{
	int n = 0;
	for (int i = 0; i < pl_ntemplates && n < max; i++)
		if (pl_templates[i]->kind == k) {
			const struct tmpl *u = tmpl_user_find(pl_templates[i]->id);
			out[n++] = u && u->kind == k ? u : pl_templates[i];
		}
	for (int i = 0; i < nuser && n < max; i++)
		if (user[i]->kind == k && !tmpl_is_shipped(user[i]->id))
			out[n++] = user[i];
	return n;
}
