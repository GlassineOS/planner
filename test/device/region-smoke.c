/* SPDX-License-Identifier: GPL-2.0-or-later
 * region-smoke: the planner's check of writing regions on a running compositor, before the
 * planner is built on them. One window, one writing region ("smoke") in the middle third of
 * the window, reading on. Prints a line per event:
 *   "caps C"                      the manager's capabilities (bit 16: regions are read)
 *   "size W H"                    the window's size, then "region X Y W H"
 *   "pointer X Y"                 where a press landed (surface coordinates; the pen as a
 *                                 pointer, for placing strokes)
 *   "stroke K N X0,Y0 XN,YN"      a raw stroke routed to the region (region coordinates)
 *   "value SERIAL STATE_BYTES TEXT"  the region's value from the handwriting service
 * and exits after SECONDS (default 30). Needs a compositor with glassine_handwriting_v1
 * version 2 (exit 2 without regions). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "glassine-handwriting.h"
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_seat *seat;
static struct wl_surface *surface;
static struct ghw_pen *pen;
static struct ghw_region *region;
static int width = 400, height = 400, configured, dirty = 1;
static double px, py;

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t serial) { xdg_wm_base_pong(w, serial); }
static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	px = wl_fixed_to_double(x), py = wl_fixed_to_double(y);
}
static void ptr_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf) {}
static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	px = wl_fixed_to_double(x), py = wl_fixed_to_double(y);
}
static void ptr_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st)
{
	if (st == WL_POINTER_BUTTON_STATE_PRESSED) printf("pointer %.1f %.1f\n", px, py), fflush(stdout);
}
static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static void ptr_frame(void *d, struct wl_pointer *p) {}
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s) {}
static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {}
static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}
static const struct wl_pointer_listener ptr_listener = { ptr_enter, ptr_leave, ptr_motion, ptr_button, ptr_axis,
	ptr_frame, ptr_axis_source, ptr_axis_stop, ptr_axis_discrete };

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	static struct wl_pointer *p;
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !p) {
		p = wl_seat_get_pointer(s);
		wl_pointer_add_listener(p, &ptr_listener, NULL);
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v)
{
	if (!strcmp(iface, wl_compositor_interface.name)) compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	else if (!strcmp(iface, wl_shm_interface.name)) shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, xdg_wm_base_interface.name)) {
		wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wm, &wm_listener, NULL);
	} else if (!strcmp(iface, wl_seat_interface.name)) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, v < 5 ? v : 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	}
}
static void global_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = { global, global_remove };

static void rect(void)
{
	double x = width / 3.0, y = height / 3.0, w = width / 3.0, h = height / 3.0;
	ghw_region_set_rect(region, x, y, w, h);
	printf("size %d %d\nregion %.1f %.1f %.1f %.1f\n", width, height, x, y, w, h);
	fflush(stdout);
}

/* white, the region outlined */
static void draw(void)
{
	int stride = width * 4, size = stride * height;
	int fd = memfd_create("region-smoke", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, size) < 0) exit(1);
	uint32_t *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	for (int i = 0; i < width * height; i++) px[i] = 0xffffffff;
	int x0 = width / 3, y0 = height / 3, x1 = 2 * width / 3, y1 = 2 * height / 3;
	for (int x = x0; x <= x1; x++) px[y0 * width + x] = px[y1 * width + x] = 0xff000000;
	for (int y = y0; y <= y1; y++) px[y * width + x0] = px[y * width + x1] = 0xff000000;
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	munmap(px, size);
	close(fd);
	wl_surface_attach(surface, b, 0, 0);
	wl_surface_damage(surface, 0, 0, width, height);
	wl_surface_commit(surface);
}

static void xs_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{
	xdg_surface_ack_configure(xs, serial);
	configured = 1;
	if (dirty) {
		draw();
		rect();
		dirty = 0;
	}
}
static const struct xdg_surface_listener xs_listener = { xs_configure };
static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	if (w > 0 && h > 0 && (w != width || h != height)) width = w, height = h, dirty = 1;
}
static void tl_close(void *d, struct xdg_toplevel *t) { exit(0); }
static const struct xdg_toplevel_listener tl_listener = { tl_configure, tl_close };

static void on_stroke(void *d, struct ghw_region *r, uint32_t k, const struct ghw_point *p, size_t n)
{
	printf("stroke %u %zu", k, n);
	if (n) printf(" %.1f,%.1f %.1f,%.1f", p[0].x, p[0].y, p[n - 1].x, p[n - 1].y);
	printf("\n");
	fflush(stdout);
}
static void on_value(void *d, struct ghw_region *r, uint32_t serial, const char *text, const struct ghw_word *w,
		     size_t nw, const uint32_t *gone, size_t ngone, const void *state, size_t state_len)
{
	printf("value %u %zu %s\n", serial, state_len, text);
	fflush(stdout);
}
static const struct ghw_region_listener region_listener = { .stroke = on_stroke, .value = on_value };

int main(int argc, char **argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 30;
	struct wl_display *dpy = wl_display_connect(NULL);
	if (!dpy) { fprintf(stderr, "region-smoke: no Wayland display\n"); return 1; }
	struct wl_registry *reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(dpy);
	if (!compositor || !shm || !wm) { fprintf(stderr, "region-smoke: missing globals\n"); return 1; }
	surface = wl_compositor_create_surface(compositor);
	pen = ghw_pen_new(dpy, surface, NULL, NULL);
	if (!pen) { fprintf(stderr, "region-smoke: no glassine_handwriting_manager_v1\n"); return 2; }
	printf("caps %u\n", ghw_pen_capabilities(pen));
	region = ghw_region_new(pen, "smoke", &region_listener, NULL);
	if (!region) { fprintf(stderr, "region-smoke: the compositor has no writing regions\n"); return 2; }
	ghw_region_set_reading(region, 1);
	struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, surface);
	xdg_surface_add_listener(xs, &xs_listener, NULL);
	struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
	xdg_toplevel_add_listener(tl, &tl_listener, NULL);
	xdg_toplevel_set_app_id(tl, "glassine-region-smoke");
	xdg_toplevel_set_title(tl, "region smoke");
	wl_surface_commit(surface);
	fflush(stdout);
	time_t end = time(NULL) + seconds;
	while (time(NULL) < end) {
		wl_display_flush(dpy);
		struct pollfd pfd = { wl_display_get_fd(dpy), POLLIN, 0 };
		if (poll(&pfd, 1, 200) > 0 && wl_display_dispatch(dpy) < 0) break;
		else wl_display_dispatch_pending(dpy);
	}
	ghw_region_destroy(region);
	ghw_pen_destroy(pen);
	wl_display_disconnect(dpy);
	return 0;
}
