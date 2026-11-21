/* SPDX-License-Identifier: GPL-2.0-or-later
 * tap-inject: a stand-in finger or pen tip for timing checks on a device without a hand on
 * it. Makes a virtual touch screen (uinput: INPUT_PROP_DIRECT, multitouch slots) or screen
 * tablet (-t pen), waits for the compositor to take it, then taps each point given as "X,Y"
 * (0..1 of the device, before the compositor's calibration and output transform), PAUSE ms
 * apart, and removes it. Each tap's time goes to stdout in glassine-epdd-desktop's log clock
 * (CLOCK_MONOTONIC ms, mod 10^6). Needs write access to /dev/uinput (root).
 *   tap-inject [-t touch|pen] [-w SECONDS] [-p PAUSE_MS] X,Y... */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAXV 20000

static int fd;

static void ev(int type, int code, int value)
{
	struct input_event e = { 0 };
	e.type = type, e.code = code, e.value = value;
	if (write(fd, &e, sizeof e) != sizeof e) perror("tap-inject: write");
}
static void syn(void) { ev(EV_SYN, SYN_REPORT, 0); }
static void ms(int n) { struct timespec t = { n / 1000, (n % 1000) * 1000000L }; nanosleep(&t, NULL); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static void absinfo(int code, int max, int res)
{
	struct uinput_abs_setup a = { 0 };
	a.code = code;
	a.absinfo.maximum = max;
	a.absinfo.resolution = res;
	ioctl(fd, UI_ABS_SETUP, &a);
	ioctl(fd, UI_SET_ABSBIT, code);
}

int main(int argc, char **argv)
{
	int wait = 3, pause = 3000, pen = 0, i = 1;
	for (; i + 1 < argc && argv[i][0] == '-'; i += 2)
		if (!strcmp(argv[i], "-w")) wait = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-p")) pause = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-t")) pen = !strcmp(argv[i + 1], "pen");
	fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) { perror("tap-inject: /dev/uinput"); return 1; }
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
	ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
	if (pen) {
		ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_PEN);
		ioctl(fd, UI_SET_KEYBIT, BTN_STYLUS);
		absinfo(ABS_X, MAXV, 100);
		absinfo(ABS_Y, MAXV, 100);
		absinfo(ABS_PRESSURE, 4095, 0);
	} else {
		absinfo(ABS_X, MAXV, 0);
		absinfo(ABS_Y, MAXV, 0);
		absinfo(ABS_MT_SLOT, 9, 0);
		absinfo(ABS_MT_TRACKING_ID, 65535, 0);
		absinfo(ABS_MT_POSITION_X, MAXV, 0);
		absinfo(ABS_MT_POSITION_Y, MAXV, 0);
	}
	struct uinput_setup us = { 0 };
	us.id.bustype = BUS_VIRTUAL;
	us.id.vendor = 0x1d6b, us.id.product = pen ? 0x0104 : 0x0105;
	snprintf(us.name, sizeof us.name, pen ? "glassine-pen-inject" : "glassine-touch-inject");
	if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) { perror("tap-inject: create"); return 1; }
	sleep(wait);
	for (int id = 1; i < argc; i++, id++) {
		double x, y;
		if (sscanf(argv[i], "%lf,%lf", &x, &y) != 2) { fprintf(stderr, "tap-inject: %s: not X,Y\n", argv[i]); continue; }
		int X = (int)(x * MAXV), Y = (int)(y * MAXV);
		double t = now();
		printf("%10.1f tap %s\n", t - (double)((long long)(t / 1e6)) * 1e6, argv[i]);
		fflush(stdout);
		if (pen) { /* in proximity, down 60 ms, up, out of proximity */
			ev(EV_ABS, ABS_X, X), ev(EV_ABS, ABS_Y, Y), ev(EV_KEY, BTN_TOOL_PEN, 1), syn();
			ms(30);
			ev(EV_ABS, ABS_PRESSURE, 2000), ev(EV_KEY, BTN_TOUCH, 1), syn();
			ms(60);
			ev(EV_ABS, ABS_PRESSURE, 0), ev(EV_KEY, BTN_TOUCH, 0), syn();
			ms(30);
			ev(EV_KEY, BTN_TOOL_PEN, 0), syn();
			if (i + 1 < argc) ms(pause);
			continue;
		}
		ev(EV_ABS, ABS_MT_SLOT, 0);
		ev(EV_ABS, ABS_MT_TRACKING_ID, id);
		ev(EV_ABS, ABS_MT_POSITION_X, X);
		ev(EV_ABS, ABS_MT_POSITION_Y, Y);
		ev(EV_KEY, BTN_TOUCH, 1);
		ev(EV_ABS, ABS_X, X);
		ev(EV_ABS, ABS_Y, Y);
		syn();
		ms(60);
		ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
		ev(EV_KEY, BTN_TOUCH, 0);
		syn();
		if (i + 1 < argc) ms(pause);
	}
	ms(200);
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	return 0;
}
