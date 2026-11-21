/* SPDX-License-Identifier: GPL-2.0-or-later
 * pen-inject: a stand-in pen for checks on a device without a hand on it. Makes a virtual
 * screen tablet (uinput: INPUT_PROP_DIRECT, BTN_TOOL_PEN, pressure), waits for the
 * compositor to take it, then draws each stroke given as "X0,Y0:X1,Y1" (0..1 of the tablet,
 * before the compositor's calibration and output transform), a straight line of 40 points,
 * and removes the tablet. Needs write access to /dev/uinput (root).
 *   pen-inject [-w SECONDS] STROKE... */
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
	if (write(fd, &e, sizeof e) != sizeof e) perror("pen-inject: write");
}
static void syn(void) { ev(EV_SYN, SYN_REPORT, 0); }
static void ms(int n) { struct timespec t = { n / 1000, (n % 1000) * 1000000L }; nanosleep(&t, NULL); }

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
	int wait = 3, i = 1;
	if (argc > 2 && !strcmp(argv[1], "-w")) wait = atoi(argv[2]), i = 3;
	fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) { perror("pen-inject: /dev/uinput"); return 1; }
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_PEN);
	ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
	ioctl(fd, UI_SET_KEYBIT, BTN_STYLUS);
	ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
	absinfo(ABS_X, MAXV, 100);
	absinfo(ABS_Y, MAXV, 100);
	absinfo(ABS_PRESSURE, 4095, 0);
	struct uinput_setup us = { 0 };
	us.id.bustype = BUS_VIRTUAL;
	us.id.vendor = 0x1d6b, us.id.product = 0x0104;
	snprintf(us.name, sizeof us.name, "glassine-pen-inject");
	if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) { perror("pen-inject: create"); return 1; }
	sleep(wait);
	for (; i < argc; i++) {
		double x0, y0, x1, y1;
		if (sscanf(argv[i], "%lf,%lf:%lf,%lf", &x0, &y0, &x1, &y1) != 4) {
			fprintf(stderr, "pen-inject: %s: not X0,Y0:X1,Y1\n", argv[i]);
			continue;
		}
		/* in proximity, then down, 40 points, up, out of proximity */
		ev(EV_ABS, ABS_X, x0 * MAXV), ev(EV_ABS, ABS_Y, y0 * MAXV), ev(EV_KEY, BTN_TOOL_PEN, 1), syn();
		ms(30);
		for (int k = 0; k <= 40; k++) {
			double t = k / 40.0;
			ev(EV_ABS, ABS_X, (x0 + (x1 - x0) * t) * MAXV);
			ev(EV_ABS, ABS_Y, (y0 + (y1 - y0) * t) * MAXV);
			ev(EV_ABS, ABS_PRESSURE, 2000);
			if (k == 0) ev(EV_KEY, BTN_TOUCH, 1);
			syn();
			ms(8);
		}
		ev(EV_ABS, ABS_PRESSURE, 0), ev(EV_KEY, BTN_TOUCH, 0), syn();
		ms(30);
		ev(EV_KEY, BTN_TOOL_PEN, 0), syn();
		ms(300);
		printf("drew %s\n", argv[i]);
		fflush(stdout);
	}
	sleep(1);
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	return 0;
}
