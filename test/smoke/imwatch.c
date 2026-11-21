// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * imwatch: an input method that only watches (as GlassineOS/shell's glassine-swayd does for
 * the keyboard's auto-show): it logs, one line each, when sway activates it for a focused
 * text field (text-input-v3 enable), the field's content purpose and text, and when it goes.
 * It never types. The smoke run reads the log: "activate" = the system keyboard would show.
 *   imwatch LOG
 */
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>
#include "im.h"

static FILE *L;
static struct zwp_input_method_manager_v2 *imm;
static struct wl_seat *seat;
static int act, purpose;
static char text[512];

static void a(void *d, struct zwp_input_method_v2 *m) { act = 1; }
static void da(void *d, struct zwp_input_method_v2 *m) { act = 0; }
static void st(void *d, struct zwp_input_method_v2 *m, const char *t, uint32_t c, uint32_t an)
{
	snprintf(text, sizeof text, "%s", t ? t : "");
}
static void cause(void *d, struct zwp_input_method_v2 *m, uint32_t c) {}
static void ct(void *d, struct zwp_input_method_v2 *m, uint32_t hint, uint32_t p) { purpose = (int)p; }
static void done(void *d, struct zwp_input_method_v2 *m)
{
	if (act)
		fprintf(L, "activate purpose=%d text=%s\n", purpose, text);
	else
		fprintf(L, "deactivate\n");
	fflush(L);
}
static void un(void *d, struct zwp_input_method_v2 *m)
{
	fprintf(L, "unavailable\n");
	fflush(L);
}
static const struct zwp_input_method_v2_listener iml = { a, da, st, cause, ct, done, un };

static void g(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	if (!strcmp(iface, zwp_input_method_manager_v2_interface.name))
		imm = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
	else if (!strcmp(iface, wl_seat_interface.name) && !seat)
		seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
}
static void gr(void *data, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener rl = { g, gr };

int main(int argc, char **argv)
{
	if (argc < 2 || !(L = fopen(argv[1], "a")))
		return 1;
	struct wl_display *d = wl_display_connect(NULL);
	if (!d)
		return 1;
	struct wl_registry *r = wl_display_get_registry(d);
	wl_registry_add_listener(r, &rl, NULL);
	wl_display_roundtrip(d);
	if (!imm || !seat) {
		fprintf(L, "no input-method-v2\n");
		return 1;
	}
	struct zwp_input_method_v2 *im = zwp_input_method_manager_v2_get_input_method(imm, seat);
	zwp_input_method_v2_add_listener(im, &iml, NULL);
	fprintf(L, "ready\n");
	fflush(L);
	while (wl_display_dispatch(d) >= 0)
		;
	return 0;
}
