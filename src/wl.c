// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * glassine-planner (spike) on Wayland: one xdg_toplevel (app_id glassine-planner),
 * shared-memory buffers (frames.c, as GlassineOS/home), and the input:
 *
 *   the pen       tablet-v2 tools: the tip writes, the eraser end erases
 *   the pointer   the host's stand-in: the left button is the pen, the right one a finger
 *   touch         a finger: taps, drags events, opens a field's text mode
 *   keys          wl_keyboard (the on-screen keyboard types through it); text-input-v3
 *                 is enabled on a field in text mode, so the system shows its keyboard
 *
 * Environment:
 *   PLANNER_TEMPLATES  the templates' directory [./templates]
 *   PLANNER_DATA       where entries.txt and choices.txt live [$XDG_DATA_HOME/glassine-planner-spike]
 *   PLANNER_VOICE      white | editorial | stationery [~/.config/glassine-shell/theme]
 *   PLANNER_VIEW       day | week [day];  PLANNER_TEMPLATE  a template id for the view
 *   PLANNER_TODAY      YYYY-MM-DD [the clock];  PLANNER_DEMO=1  synthetic demo entries
 *   PLANNER_DEBUG=1    the debug overlay;  PLANNER_DUMP  file: hits and fields after each paint
 *   PLANNER_LEDGER=1   every action, commit and layout on stderr
 *   PLANNER_SCALE      device px per design px [the panel's short side / 702]
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "frames.h"
#include "plan.h"
#include "tablet-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct buffer {
	struct wl_buffer *b;
	void *data;
	int w, h, stride;
	size_t size;
};

static struct {
	struct wl_display *d;
	struct wl_compositor *comp;
	struct wl_shm *shm;
	struct xdg_wm_base *wm;
	struct wl_seat *seat;
	struct wl_output *out;
	struct zwp_tablet_manager_v2 *tabm;
	struct zwp_text_input_manager_v3 *tim;
	struct zwp_text_input_v3 *ti;
	bool ti_entered, ti_on;
	char ti_commit[256];
	int ti_delete;
	struct wl_surface *surf;
	struct xdg_surface *xs;
	struct xdg_toplevel *top;
	struct wl_pointer *ptr;
	struct wl_touch *touch;
	struct wl_keyboard *kbd;
	struct xkb_context *xkb;
	struct xkb_keymap *km;
	struct xkb_state *ks;
	int out_scale, mode_w, mode_h;
	int cw, ch;
	bool configured, running;
	double px, py;
	int btn;                 /* the pointer button held: 0, BTN_LEFT, BTN_RIGHT */
	int touch_id;
	/* a tablet tool's frame */
	struct { bool down, was_down, prox; double x, y, p; bool eraser; } pen;
	struct buffer buf[FRAME_BUFFERS];
	struct frames f;
	struct plan *p;
	const char *dump;
} W = { .out_scale = 1, .touch_id = -1, .running = true };

static long long mono_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

static void now(void)
{
	W.p->now = mono_ms();
}

/* ---- buffers (as GlassineOS/home's wl.c) ------------------------------------------- */

static void buf_release(void *data, struct wl_buffer *b)
{
	frames_released(&W.f, (int)((struct buffer *)data - W.buf));
}
static const struct wl_buffer_listener buf_listener = { buf_release };

static void wlbuf_free(struct buffer *b)
{
	if (b->b)
		wl_buffer_destroy(b->b);
	if (b->data)
		munmap(b->data, b->size);
	memset(b, 0, sizeof *b);
}

static bool buf_make(struct buffer *b, int w, int h)
{
	wlbuf_free(b);
	b->w = w;
	b->h = h;
	b->stride = w * 4;
	b->size = (size_t)b->stride * h;
	int fd = memfd_create("glassine-planner", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)b->size)) {
		if (fd >= 0)
			close(fd);
		return false;
	}
	b->data = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (b->data == MAP_FAILED) {
		b->data = NULL;
		close(fd);
		return false;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(W.shm, fd, (int)b->size);
	b->b = wl_shm_pool_create_buffer(pool, 0, w, h, b->stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->b, &buf_listener, b);
	return true;
}

static void copy_rect(struct buffer *b, const unsigned char *src, int sstride, const struct rect *r)
{
	int x0, y0, x1, y1;
	if (!frames_clip(r, b->w, b->h, &x0, &y0, &x1, &y1))
		return;
	for (int y = y0; y < y1; y++)
		memcpy((char *)b->data + (size_t)y * b->stride + x0 * 4, src + (size_t)y * sstride + x0 * 4, (size_t)(x1 - x0) * 4);
}

static void present(void)
{
	struct plan *p = W.p;
	if (!W.configured || !p->cur_canvas)
		return;
	int dw = cairo_image_surface_get_width(p->cur_canvas), dh = cairo_image_surface_get_height(p->cur_canvas);
	frames_canvas(&W.f, dw, dh);
	int i = frames_pick(&W.f);
	if (i < 0)
		return;
	struct buffer *b = &W.buf[i];
	if (b->w != dw || b->h != dh) {
		if (!buf_make(b, dw, dh))
			return;
		W.f.b[i].all = true;
	}
	cairo_surface_flush(p->cur_canvas);
	const unsigned char *src = cairo_image_surface_get_data(p->cur_canvas);
	int sstride = cairo_image_surface_get_stride(p->cur_canvas);
	struct rect full = { 0, 0, dw, dh };
	if (W.f.b[i].all)
		copy_rect(b, src, sstride, &full);
	else
		for (int k = 0; k < W.f.b[i].ndmg; k++)
			copy_rect(b, src, sstride, &W.f.b[i].dmg[k]);
	wl_surface_attach(W.surf, b->b, 0, 0);
	const struct rect *sd = W.f.sall ? &full : W.f.sdmg;
	int nsd = W.f.sall ? 1 : W.f.nsdmg;
	for (int k = 0; k < nsd; k++)
		wl_surface_damage_buffer(W.surf, (int)sd[k].x, (int)sd[k].y, (int)ceil(sd[k].w), (int)ceil(sd[k].h));
	frames_shown(&W.f, i);
	wl_surface_commit(W.surf);
}

static void commit(void *ud, const struct rect *r, int n)
{
	if (!W.configured || !W.p->cur_canvas)
		return;
	frames_damage(&W.f, r, n);
	present();
	if (W.dump)
		plan_dump(W.p, W.dump);
}

/* ---- text-input v3: the system keyboard for a field in text mode ---------------------- */

static void ti_apply(void)
{
	if (!W.ti || !W.ti_entered)
		return;
	if (W.ti_on) {
		zwp_text_input_v3_enable(W.ti);
		zwp_text_input_v3_set_content_type(W.ti, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
						   ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
	} else {
		zwp_text_input_v3_disable(W.ti);
	}
	zwp_text_input_v3_commit(W.ti);
}

static void text_input(void *ud, bool on, const struct rect *r, const char *text)
{
	bool was = W.ti_on;
	W.ti_on = on;
	if (!W.ti || !W.ti_entered) {
		if (on != was)
			fprintf(stderr, "planner: text field %s (no text-input-v3 focus: keys only)\n", on ? "on" : "off");
		return;
	}
	if (on != was)
		ti_apply();
	if (on) {
		size_t n = strlen(text ? text : "");
		zwp_text_input_v3_set_surrounding_text(W.ti, text ? text : "", (int)n, (int)n);
		if (r)
			zwp_text_input_v3_set_cursor_rectangle(W.ti, (int)r->x, (int)r->y, (int)r->w, (int)r->h);
		zwp_text_input_v3_commit(W.ti);
	}
	fprintf(stderr, "planner: text-input %s\n", on ? "enabled" : "disabled");
}

static void ti_enter(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s)
{
	W.ti_entered = true;
	if (W.ti_on)
		ti_apply();
}
static void ti_leave(void *d, struct zwp_text_input_v3 *t, struct wl_surface *s)
{
	W.ti_entered = false;
}
static void ti_preedit(void *d, struct zwp_text_input_v3 *t, const char *s, int32_t b, int32_t e) {}
static void ti_commit_string(void *d, struct zwp_text_input_v3 *t, const char *s)
{
	snprintf(W.ti_commit, sizeof W.ti_commit, "%s", s ? s : "");
}
static void ti_delete_surrounding(void *d, struct zwp_text_input_v3 *t, uint32_t before, uint32_t after)
{
	W.ti_delete = (int)before;
}
static void ti_done(void *d, struct zwp_text_input_v3 *t, uint32_t serial)
{
	now();
	for (; W.ti_delete > 0; W.ti_delete--)
		plan_backspace(W.p);
	if (W.ti_commit[0])
		plan_text(W.p, W.ti_commit);
	W.ti_commit[0] = 0;
}
static const struct zwp_text_input_v3_listener ti_listener = { .enter = ti_enter, .leave = ti_leave, .preedit_string = ti_preedit, .commit_string = ti_commit_string,
							       .delete_surrounding_text = ti_delete_surrounding, .done = ti_done };

/* ---- size -------------------------------------------------------------------------------- */

static int scale(void)
{
	const char *e = getenv("PLANNER_SCALE");
	if (e && atoi(e) > 0)
		return atoi(e);
	if (W.mode_w > 0 && W.mode_h > 0) {
		int sh = W.mode_w < W.mode_h ? W.mode_w : W.mode_h;
		int s = (int)lround(sh / 702.0);
		return s < 1 ? 1 : s;
	}
	return W.out_scale;
}

static void apply_size(void)
{
	int cs = W.out_scale < 1 ? 1 : W.out_scale;
	int bw = (W.cw ? W.cw : 702) * cs, bh = (W.ch ? W.ch : 936) * cs;
	int S = scale();
	wl_surface_set_buffer_scale(W.surf, cs);
	plan_resize(W.p, bw / S, bh / S, S);
}

static double lx(double v)
{
	return v * (W.out_scale < 1 ? 1 : W.out_scale) / W.p->S;
}

/* ---- xdg --------------------------------------------------------------------------------- */

static void wm_ping(void *d, struct xdg_wm_base *wm, uint32_t serial)
{
	xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wm_listener = { wm_ping };

static void xs_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{
	xdg_surface_ack_configure(xs, serial);
	W.configured = true;
	apply_size();
	W.p->dirty = true;
}
static const struct xdg_surface_listener xs_listener = { xs_configure };

static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states)
{
	if (w > 0)
		W.cw = w;
	if (h > 0)
		W.ch = h;
}
static void top_close(void *d, struct xdg_toplevel *t)
{
	W.running = false;
}
static void top_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h) {}
static void top_caps(void *d, struct xdg_toplevel *t, struct wl_array *c) {}
static const struct xdg_toplevel_listener top_listener = { top_configure, top_close, top_bounds, top_caps };

/* ---- the pointer: the host's pen (left) and finger (right) ------------------------------ */

static void p_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
	W.px = lx(wl_fixed_to_double(x));
	W.py = lx(wl_fixed_to_double(y));
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
	if (W.btn) {
		now();
		plan_up(W.p); /* a stroke that leaves the window ends there */
		W.btn = 0;
	}
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	W.px = lx(wl_fixed_to_double(x));
	W.py = lx(wl_fixed_to_double(y));
	if (W.btn) {
		now();
		plan_motion(W.p, W.px, W.py, 0.5);
	}
}
static void p_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st)
{
	if (getenv("PLANNER_LEDGER"))
		fprintf(stderr, "planner: pointer button %u %s at %.0f,%.0f (held %d)\n", b, st ? "down" : "up", W.px, W.py, W.btn);
	if (b != BTN_LEFT && b != BTN_RIGHT)
		return;
	now();
	if (st == WL_POINTER_BUTTON_STATE_PRESSED && !W.btn) {
		W.btn = (int)b;
		plan_down(W.p, b == BTN_LEFT ? T_PEN : T_TOUCH, W.px, W.py, 0.5);
	} else if (st == WL_POINTER_BUTTON_STATE_RELEASED && W.btn == (int)b) {
		W.btn = 0;
		plan_up(W.p);
	}
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static void p_frame(void *d, struct wl_pointer *p) {}
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) {}
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {}
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}
static const struct wl_pointer_listener ptr_listener = {
	.enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button, .axis = p_axis,
	.frame = p_frame, .axis_source = p_axis_source, .axis_stop = p_axis_stop, .axis_discrete = p_axis_discrete,
};

/* ---- touch: a finger ---------------------------------------------------------------------- */

static void t_down(void *d, struct wl_touch *t, uint32_t s, uint32_t tm, struct wl_surface *sf, int32_t id, wl_fixed_t x,
		   wl_fixed_t y)
{
	if (W.touch_id >= 0 || W.pen.down)
		return; /* one finger, and never while the pen writes (palm) */
	W.touch_id = id;
	now();
	plan_down(W.p, T_TOUCH, lx(wl_fixed_to_double(x)), lx(wl_fixed_to_double(y)), 0);
}
static void t_up(void *d, struct wl_touch *t, uint32_t s, uint32_t tm, int32_t id)
{
	if (id != W.touch_id)
		return;
	W.touch_id = -1;
	now();
	plan_up(W.p);
}
static void t_motion(void *d, struct wl_touch *t, uint32_t tm, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
	if (id != W.touch_id)
		return;
	now();
	plan_motion(W.p, lx(wl_fixed_to_double(x)), lx(wl_fixed_to_double(y)), 0);
}
static void t_frame(void *d, struct wl_touch *t) {}
static void t_cancel(void *d, struct wl_touch *t)
{
	W.touch_id = -1;
	plan_cancel(W.p);
}
static void t_shape(void *d, struct wl_touch *t, int32_t id, wl_fixed_t a, wl_fixed_t b) {}
static void t_orient(void *d, struct wl_touch *t, int32_t id, wl_fixed_t o) {}
static const struct wl_touch_listener touch_listener = { t_down, t_up, t_motion, t_frame, t_cancel, t_shape, t_orient };

/* ---- the pen: tablet-v2 tools ------------------------------------------------------------- */

static void tt_type(void *d, struct zwp_tablet_tool_v2 *t, uint32_t type)
{
	W.pen.eraser = type == ZWP_TABLET_TOOL_V2_TYPE_ERASER;
}
static void tt_serial(void *d, struct zwp_tablet_tool_v2 *t, uint32_t hi, uint32_t lo) {}
static void tt_wacom(void *d, struct zwp_tablet_tool_v2 *t, uint32_t hi, uint32_t lo) {}
static void tt_cap(void *d, struct zwp_tablet_tool_v2 *t, uint32_t c) {}
static void tt_done(void *d, struct zwp_tablet_tool_v2 *t) {}
static void tt_removed(void *d, struct zwp_tablet_tool_v2 *t)
{
	zwp_tablet_tool_v2_destroy(t);
}
static void tt_prox_in(void *d, struct zwp_tablet_tool_v2 *t, uint32_t s, struct zwp_tablet_v2 *tab, struct wl_surface *sf)
{
	W.pen.prox = true;
}
static void tt_prox_out(void *d, struct zwp_tablet_tool_v2 *t)
{
	W.pen.prox = false;
	W.pen.down = false;
}
static void tt_down(void *d, struct zwp_tablet_tool_v2 *t, uint32_t s)
{
	W.pen.down = true;
}
static void tt_up(void *d, struct zwp_tablet_tool_v2 *t)
{
	W.pen.down = false;
}
static void tt_motion(void *d, struct zwp_tablet_tool_v2 *t, wl_fixed_t x, wl_fixed_t y)
{
	W.pen.x = lx(wl_fixed_to_double(x));
	W.pen.y = lx(wl_fixed_to_double(y));
}
static void tt_pressure(void *d, struct zwp_tablet_tool_v2 *t, uint32_t p)
{
	W.pen.p = p / 65535.0;
}
static void tt_distance(void *d, struct zwp_tablet_tool_v2 *t, uint32_t v) {}
static void tt_tilt(void *d, struct zwp_tablet_tool_v2 *t, wl_fixed_t x, wl_fixed_t y) {}
static void tt_rotation(void *d, struct zwp_tablet_tool_v2 *t, wl_fixed_t v) {}
static void tt_slider(void *d, struct zwp_tablet_tool_v2 *t, int32_t v) {}
static void tt_wheel(void *d, struct zwp_tablet_tool_v2 *t, wl_fixed_t v, int32_t c) {}
static void tt_button(void *d, struct zwp_tablet_tool_v2 *t, uint32_t s, uint32_t b, uint32_t st) {}
static void tt_frame(void *d, struct zwp_tablet_tool_v2 *t, uint32_t time)
{
	now();
	if (W.pen.down && !W.pen.was_down) {
		if (W.touch_id >= 0) {
			plan_cancel(W.p); /* the pen wins over a palm's touch */
			W.touch_id = -1;
		}
		plan_down(W.p, W.pen.eraser ? T_ERASER : T_PEN, W.pen.x, W.pen.y, W.pen.p);
	} else if (W.pen.down) {
		plan_motion(W.p, W.pen.x, W.pen.y, W.pen.p);
	} else if (W.pen.was_down) {
		plan_up(W.p);
	}
	W.pen.was_down = W.pen.down;
}
static const struct zwp_tablet_tool_v2_listener tool_listener = {
	.type = tt_type, .hardware_serial = tt_serial, .hardware_id_wacom = tt_wacom, .capability = tt_cap,
	.done = tt_done, .removed = tt_removed, .proximity_in = tt_prox_in, .proximity_out = tt_prox_out,
	.down = tt_down, .up = tt_up, .motion = tt_motion, .pressure = tt_pressure, .distance = tt_distance,
	.tilt = tt_tilt, .rotation = tt_rotation, .slider = tt_slider, .wheel = tt_wheel, .button = tt_button,
	.frame = tt_frame,
};

static void ts_tablet(void *d, struct zwp_tablet_seat_v2 *s, struct zwp_tablet_v2 *t) {}
static void ts_tool(void *d, struct zwp_tablet_seat_v2 *s, struct zwp_tablet_tool_v2 *t)
{
	zwp_tablet_tool_v2_add_listener(t, &tool_listener, NULL);
	fprintf(stderr, "planner: a pen (tablet tool)\n");
}
static void ts_pad(void *d, struct zwp_tablet_seat_v2 *s, struct zwp_tablet_pad_v2 *p) {}
static const struct zwp_tablet_seat_v2_listener tseat_listener = { ts_tablet, ts_tool, ts_pad };

/* ---- keys ---------------------------------------------------------------------------------- */

static void k_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size)
{
	if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	char *m = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (m == MAP_FAILED)
		return;
	if (W.ks)
		xkb_state_unref(W.ks);
	if (W.km)
		xkb_keymap_unref(W.km);
	W.km = xkb_keymap_new_from_string(W.xkb, m, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(m, size);
	W.ks = W.km ? xkb_state_new(W.km) : NULL;
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys) {}
static void k_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf) {}
static void k_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t state)
{
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !W.ks)
		return;
	now();
	xkb_keysym_t sym = xkb_state_key_get_one_sym(W.ks, key + 8);
	switch (sym) {
	case XKB_KEY_BackSpace:
		plan_backspace(W.p);
		return;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
	case XKB_KEY_Escape:
		plan_key_done(W.p);
		return;
	case XKB_KEY_F2:
		plan_key(W.p, "debug");
		return;
	case XKB_KEY_F3:
		plan_key(W.p, "template");
		return;
	case XKB_KEY_F4:
		plan_key(W.p, "text");
		return;
	default:
		break;
	}
	char u[16];
	int n = xkb_state_key_get_utf8(W.ks, key + 8, u, sizeof u);
	if (n > 0 && (unsigned char)u[0] >= 0x20 && u[0] != 0x7f)
		plan_text(W.p, u);
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp)
{
	if (W.ks)
		xkb_state_update_mask(W.ks, dep, lat, lock, 0, 0, grp);
}
static void k_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener kbd_listener = { k_keymap, k_enter, k_leave, k_key, k_mods, k_repeat };

static void seat_caps(void *d, struct wl_seat *seat, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !W.ptr) {
		W.ptr = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(W.ptr, &ptr_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !W.touch) {
		W.touch = wl_seat_get_touch(seat);
		wl_touch_add_listener(W.touch, &touch_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !W.kbd) {
		W.kbd = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(W.kbd, &kbd_listener, NULL);
	}
	/* a capability that goes (the last keyboard: an on-screen keyboard that quits, a
	 * virtual one in the tests) takes its object with it; the next one is a new object */
	if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && W.kbd) {
		wl_keyboard_release(W.kbd);
		W.kbd = NULL;
	}
	if (!(caps & WL_SEAT_CAPABILITY_POINTER) && W.ptr) {
		wl_pointer_release(W.ptr);
		W.ptr = NULL;
		W.btn = 0;
	}
	if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && W.touch) {
		wl_touch_release(W.touch);
		W.touch = NULL;
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

static void o_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sp,
		       const char *mk, const char *md, int32_t tr) {}
static void o_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t r)
{
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		W.mode_w = w;
		W.mode_h = h;
	}
}
static void o_done(void *d, struct wl_output *o)
{
	if (W.surf && W.configured)
		apply_size();
}
static void o_scale(void *d, struct wl_output *o, int32_t f)
{
	W.out_scale = f;
}
static const struct wl_output_listener out_listener = { .geometry = o_geometry, .mode = o_mode, .done = o_done, .scale = o_scale };

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	if (!strcmp(iface, wl_compositor_interface.name))
		W.comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
	else if (!strcmp(iface, wl_shm_interface.name))
		W.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, xdg_wm_base_interface.name)) {
		W.wm = wl_registry_bind(r, name, &xdg_wm_base_interface, ver < 4 ? ver : 4);
		xdg_wm_base_add_listener(W.wm, &wm_listener, NULL);
	} else if (!strcmp(iface, wl_seat_interface.name) && !W.seat) {
		W.seat = wl_registry_bind(r, name, &wl_seat_interface, ver < 5 ? ver : 5);
		wl_seat_add_listener(W.seat, &seat_listener, NULL);
	} else if (!strcmp(iface, wl_output_interface.name) && !W.out) {
		W.out = wl_registry_bind(r, name, &wl_output_interface, ver < 2 ? ver : 2);
		wl_output_add_listener(W.out, &out_listener, NULL);
	} else if (!strcmp(iface, zwp_tablet_manager_v2_interface.name)) {
		W.tabm = wl_registry_bind(r, name, &zwp_tablet_manager_v2_interface, 1);
	} else if (!strcmp(iface, zwp_text_input_manager_v3_interface.name)) {
		W.tim = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
	}
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

static void on_term(int sig)
{
	W.running = false;
}

/* the system's voice, as GlassineOS/home reads it */
static void voice_from_system(char *out, size_t n)
{
	const char *e = getenv("PLANNER_VOICE");
	if (e && *e) {
		snprintf(out, n, "%s", e);
		return;
	}
	char p[1024];
	snprintf(p, sizeof p, "%s/.config/glassine-shell/theme", getenv("HOME") ? getenv("HOME") : "/");
	FILE *f = fopen(p, "r");
	out[0] = 0;
	if (f) {
		if (fscanf(f, "%31s", out) != 1)
			out[0] = 0;
		fclose(f);
	}
}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printf("usage: glassine-planner\nA throwaway UI spike; see the comment at the top of src/wl.c for the environment.\n");
			return 0;
		}
	static struct plan p;
	struct plan_io io = { .commit = commit, .text_input = text_input };
	const char *tdir = getenv("PLANNER_TEMPLATES") ? getenv("PLANNER_TEMPLATES") : "templates";
	char data[1024];
	if (getenv("PLANNER_DATA"))
		snprintf(data, sizeof data, "%s", getenv("PLANNER_DATA"));
	else if (getenv("XDG_DATA_HOME"))
		snprintf(data, sizeof data, "%s/glassine-planner-spike", getenv("XDG_DATA_HOME"));
	else
		snprintf(data, sizeof data, "%s/.local/share/glassine-planner-spike", getenv("HOME") ? getenv("HOME") : ".");
	char err[256];
	ui_draw_init();
	if (!plan_init(&p, tdir, data, &io, err, sizeof err)) {
		fprintf(stderr, "glassine-planner: %s\n", err);
		return 1;
	}
	W.p = &p;
	W.dump = getenv("PLANNER_DUMP");
	char voice[32];
	voice_from_system(voice, sizeof voice);
	plan_set_voice(&p, voice);
	if (getenv("PLANNER_DEMO"))
		plan_seed_demo(&p);
	if (getenv("PLANNER_VIEW") && !strcmp(getenv("PLANNER_VIEW"), "week"))
		plan_set_day(&p, P_WEEK, p.today);
	if (getenv("PLANNER_TEMPLATE"))
		plan_use_template(&p, getenv("PLANNER_TEMPLATE"));
	p.debug = getenv("PLANNER_DEBUG") != NULL;

	W.d = wl_display_connect(NULL);
	if (!W.d) {
		fprintf(stderr, "glassine-planner: no Wayland display\n");
		return 1;
	}
	frames_init(&W.f);
	W.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct wl_registry *reg = wl_display_get_registry(W.d);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(W.d);
	if (W.tabm && W.seat) {
		struct zwp_tablet_seat_v2 *ts = zwp_tablet_manager_v2_get_tablet_seat(W.tabm, W.seat);
		zwp_tablet_seat_v2_add_listener(ts, &tseat_listener, NULL);
	}
	if (W.tim && W.seat) {
		W.ti = zwp_text_input_manager_v3_get_text_input(W.tim, W.seat);
		zwp_text_input_v3_add_listener(W.ti, &ti_listener, NULL);
	}
	wl_display_roundtrip(W.d);
	if (!W.comp || !W.shm || !W.wm) {
		fprintf(stderr, "glassine-planner: the compositor lacks wl_compositor, wl_shm or xdg_wm_base\n");
		return 1;
	}
	fprintf(stderr, "planner: tablet-v2 %s, text-input-v3 %s\n", W.tabm ? "yes" : "no", W.tim ? "yes" : "no");
	W.surf = wl_compositor_create_surface(W.comp);
	W.xs = xdg_wm_base_get_xdg_surface(W.wm, W.surf);
	xdg_surface_add_listener(W.xs, &xs_listener, NULL);
	W.top = xdg_surface_get_toplevel(W.xs);
	xdg_toplevel_add_listener(W.top, &top_listener, NULL);
	xdg_toplevel_set_app_id(W.top, "glassine-planner");
	xdg_toplevel_set_title(W.top, "Planner (spike)");
	wl_surface_commit(W.surf);
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGPIPE, SIG_IGN);

	while (W.running) {
		int wait = plan_tick(&p, mono_ms());
		wl_display_flush(W.d);
		struct pollfd pf = { wl_display_get_fd(W.d), POLLIN, 0 };
		while (wl_display_prepare_read(W.d) != 0)
			wl_display_dispatch_pending(W.d);
		wl_display_flush(W.d);
		int r = poll(&pf, 1, wait < 0 ? 1000 : wait + 1);
		if (r > 0 && (pf.revents & POLLIN))
			wl_display_read_events(W.d);
		else
			wl_display_cancel_read(W.d);
		if (wl_display_dispatch_pending(W.d) < 0)
			break;
		if (pf.revents & (POLLHUP | POLLERR))
			break;
		if (W.f.owed)
			present();
	}
	plan_free(&p);
	for (int i = 0; i < FRAME_BUFFERS; i++)
		wlbuf_free(&W.buf[i]);
	wl_display_disconnect(W.d);
	return 0;
}
