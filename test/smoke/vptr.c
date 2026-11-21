// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vptr: a virtual pointer for the smoke run (after GlassineOS/shell's test/shell/vptr.c).
 * Stays connected and reads commands from a FIFO, one per line, positions in layout px of
 * the output (W x H, argv). The left button is the planner's pen, the right one a finger.
 *   move X Y | tap X Y | rtap X Y | stroke X Y X Y ... (down, moves 8 ms apart, up)
 *   drag X0 Y0 X1 Y1 N (the left button, N steps)
 * Every command ends with "done" on stdout once the compositor has it.
 *   vptr FIFO W H
 */

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "vp.h"

static struct zwlr_virtual_pointer_manager_v1 *mgr;
static struct zwlr_virtual_pointer_v1 *vp;
static struct wl_display *d;
static int W = 702, H = 936;

static void g(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
		mgr = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
}
static void gr(void *data, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener rl = { g, gr };

static uint32_t ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void sync_(void) { wl_display_roundtrip(d); }
static void move(double x, double y)
{
	zwlr_virtual_pointer_v1_motion_absolute(vp, ms(), (uint32_t)(x * 16), (uint32_t)(y * 16), W * 16, H * 16);
	zwlr_virtual_pointer_v1_frame(vp);
}
static void btn(int b, int down)
{
	zwlr_virtual_pointer_v1_button(vp, ms(), b, down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
	zwlr_virtual_pointer_v1_frame(vp);
}
static void click(int b, double x, double y)
{
	move(x, y); sync_();
	btn(b, 1); sync_();
	usleep(60000);
	btn(b, 0);
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: vptr FIFO W H\n");
		return 1;
	}
	W = atoi(argv[2]);
	H = atoi(argv[3]);
	d = wl_display_connect(NULL);
	if (!d)
		return 1;
	struct wl_registry *r = wl_display_get_registry(d);
	wl_registry_add_listener(r, &rl, NULL);
	wl_display_roundtrip(d);
	if (!mgr) {
		fprintf(stderr, "vptr: no virtual pointer manager\n");
		return 1;
	}
	vp = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, NULL);
	sync_();
	setvbuf(stdout, NULL, _IOLBF, 0);
	for (;;) {
		FILE *f = fopen(argv[1], "r");
		if (!f)
			return 1;
		char l[4096];
		while (fgets(l, sizeof l, f)) {
			double x, y, x1, y1;
			int n, used;
			if (!strncmp(l, "stroke ", 7)) {
				char *c = l + 7;
				int k = 0;
				while (sscanf(c, " %lf %lf%n", &x, &y, &used) == 2) {
					move(x, y);
					sync_();
					if (k++ == 0) {
						btn(BTN_LEFT, 1);
						sync_();
					}
					usleep(8000);
					c += used;
				}
				btn(BTN_LEFT, 0);
			} else if (sscanf(l, "drag %lf %lf %lf %lf %d", &x, &y, &x1, &y1, &n) == 5) {
				move(x, y); sync_();
				btn(BTN_LEFT, 1); sync_();
				for (int i = 1; i <= n; i++) {
					move(x + (x1 - x) * i / n, y + (y1 - y) * i / n);
					sync_();
					usleep(15000);
				}
				btn(BTN_LEFT, 0);
			} else if (sscanf(l, "move %lf %lf", &x, &y) == 2) {
				move(x, y);
			} else if (sscanf(l, "rtap %lf %lf", &x, &y) == 2) {
				click(BTN_RIGHT, x, y);
			} else if (sscanf(l, "tap %lf %lf", &x, &y) == 2) {
				click(BTN_LEFT, x, y);
			}
			sync_();
			printf("done\n");
		}
		fclose(f);
	}
}
