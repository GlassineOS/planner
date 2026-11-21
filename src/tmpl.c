// SPDX-License-Identifier: GPL-2.0-or-later
/* Templates (tmpl.h). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model.h"
#include "tmpl.h"

static const char *word(const char *s, char *out, int n)
{
	while (*s == ' ' || *s == '\t')
		s++;
	int i = 0;
	bool q = false;
	while (*s && (q || (*s != ' ' && *s != '\t'))) {
		if (*s == '"') {
			q = !q;
			s++;
			continue;
		}
		if (i < n - 1)
			out[i++] = *s;
		s++;
	}
	out[i] = 0;
	return s;
}

bool tmpl_parse(struct tmpl *t, const char *id, const char *text, char *err, int errlen)
{
	memset(t, 0, sizeof *t);
	snprintf(t->id, sizeof t->id, "%s", id);
	t->kind = -1;
	int stack[16], depth[16], sp = 0;
	int lineno = 0;
	const char *p = text;
	while (*p) {
		const char *e = strchr(p, '\n');
		int len = e ? (int)(e - p) : (int)strlen(p);
		char line[256];
		snprintf(line, sizeof line, "%.*s", len, p);
		p += len + (e ? 1 : 0);
		lineno++;
		int ind = 0;
		while (line[ind] == ' ')
			ind++;
		if (line[ind] == 0 || line[ind] == '#')
			continue;
		if (line[ind] == '\t') {
			snprintf(err, errlen, "%s:%d: indent with spaces", id, lineno);
			return false;
		}
		char w[64];
		const char *rest = word(line, w, sizeof w);
		if (ind == 0 && !strcmp(w, "name")) {
			while (*rest == ' ')
				rest++;
			snprintf(t->name, sizeof t->name, "%s", rest);
			continue;
		}
		if (ind == 0 && !strcmp(w, "kind")) {
			char k[16];
			word(rest, k, sizeof k);
			t->kind = pkind_parse(k);
			if (t->kind < 0) {
				snprintf(err, errlen, "%s:%d: kind is day, week or month", id, lineno);
				return false;
			}
			continue;
		}
		if (ind % 2) {
			snprintf(err, errlen, "%s:%d: indent by two spaces", id, lineno);
			return false;
		}
		if (t->nn >= TMPL_MAX_NODES) {
			snprintf(err, errlen, "%s:%d: too many nodes", id, lineno);
			return false;
		}
		struct tnode *n = &t->n[t->nn];
		n->line = lineno;
		snprintf(n->type, sizeof n->type, "%s", w);
		for (;;) {
			char kv[96];
			rest = word(rest, kv, sizeof kv);
			if (!kv[0])
				break;
			if (n->na >= TMPL_MAX_ATTR)
				break;
			char *eq = strchr(kv, '=');
			if (eq) {
				*eq = 0;
				snprintf(n->a[n->na].k, sizeof n->a[0].k, "%s", kv);
				snprintf(n->a[n->na].v, sizeof n->a[0].v, "%s", eq + 1);
			} else {
				snprintf(n->a[n->na].k, sizeof n->a[0].k, "%s", kv);
				strcpy(n->a[n->na].v, "1");
			}
			n->na++;
		}
		while (sp > 0 && depth[sp - 1] >= ind)
			sp--;
		if (t->nn == 0) {
			if (ind != 0) {
				snprintf(err, errlen, "%s:%d: the first node is not indented", id, lineno);
				return false;
			}
		} else {
			if (sp == 0 || ind != depth[sp - 1] + 2) {
				snprintf(err, errlen, "%s:%d: %s", id, lineno,
					 sp == 0 ? "one root node only" : "a child is indented two spaces more than its parent");
				return false;
			}
			struct tnode *par = &t->n[stack[sp - 1]];
			if (par->nkids >= 16 || (strcmp(par->type, "row") && strcmp(par->type, "column"))) {
				snprintf(err, errlen, "%s:%d: only a row or a column has children", id, lineno);
				return false;
			}
			par->kids[par->nkids++] = t->nn;
		}
		stack[sp] = t->nn;
		depth[sp] = ind;
		sp++;
		t->nn++;
	}
	if (t->kind < 0 || !t->nn) {
		snprintf(err, errlen, "%s: needs a kind line and a layout", id);
		return false;
	}
	if (!t->name[0])
		snprintf(t->name, sizeof t->name, "%s", id);
	return true;
}

bool tmpl_load(struct tmpl *t, const char *path, char *err, int errlen)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		snprintf(err, errlen, "%s: can't read", path);
		return false;
	}
	char *text = NULL;
	size_t n = 0;
	FILE *m = open_memstream(&text, &n);
	char b[4096];
	size_t r;
	while ((r = fread(b, 1, sizeof b, f)) > 0)
		fwrite(b, 1, r, m);
	fclose(f);
	fclose(m);
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	char id[48];
	snprintf(id, sizeof id, "%s", base);
	char *dot = strrchr(id, '.');
	if (dot)
		*dot = 0;
	bool ok = tmpl_parse(t, id, text, err, errlen);
	free(text);
	return ok;
}

const char *tnode_get(const struct tnode *n, const char *k)
{
	for (int i = 0; i < n->na; i++)
		if (!strcmp(n->a[i].k, k))
			return n->a[i].v;
	return NULL;
}

double tnode_num(const struct tnode *n, const char *k, double def)
{
	const char *v = tnode_get(n, k);
	return v ? atof(v) : def;
}

bool tnode_flag(const struct tnode *n, const char *k)
{
	const char *v = tnode_get(n, k);
	return v && strcmp(v, "0") && strcmp(v, "no");
}

static int cmp_id(const void *a, const void *b)
{
	return strcmp(((const struct tmpl *)a)->id, ((const struct tmpl *)b)->id);
}

int tmpl_load_dir(struct tmpl *out, int max, const char *dir, char *err, int errlen)
{
	DIR *d = opendir(dir);
	if (!d) {
		snprintf(err, errlen, "%s: no such directory", dir);
		return -1;
	}
	int n = 0;
	struct dirent *de;
	while ((de = readdir(d)) && n < max) {
		size_t l = strlen(de->d_name);
		if (l < 6 || strcmp(de->d_name + l - 5, ".tmpl"))
			continue;
		char p[1024];
		snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
		if (!tmpl_load(&out[n], p, err, errlen)) {
			closedir(d);
			return -1;
		}
		n++;
	}
	closedir(d);
	qsort(out, n, sizeof *out, cmp_id);
	return n;
}
