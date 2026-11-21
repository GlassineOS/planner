# SPDX-License-Identifier: GPL-2.0-or-later
# glassine-planner, UI spike: a throwaway prototype (README "Spike").
# Needs a C11 compiler, pkg-config, cairo, pango, librsvg, glib and GlassineOS/ui for the
# drawing; wayland-client, wayland-protocols and xkbcommon for the program; sway, grim,
# wtype and wlr-protocols for the headless smoke run.
#
# GlassineOS/ui: test/ui.sh finds a checkout ($GLASSINE_UI, ../ui, else a clone); it is
# built and installed into out/ui-stage. UI_SYSTEM=1 uses an installed glassine-ui instead.
#
#   make check        the layout and core tests, the offscreen renders, the headless smoke run
#   make smoke        the smoke run alone (shots in out/shots)
#   SMOKE=skip        make check without the smoke run (no sway here); CI never sets it

CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration
LDLIBS += -lm

O := out
CORE := layout model tmpl
CORE_OBJ := $(CORE:%=$(O)/%.o)
SCREEN := plan paint frames
SCREEN_OBJ := $(SCREEN:%=$(O)/%.o)

ifeq ($(UI_SYSTEM),1)
UI_DEP :=
UI_ENV :=
UI_RPATH :=
else
UI_DIR = $(shell sh test/ui.sh)
UI_STAGE := $(CURDIR)/$(O)/ui-stage
UI_DEP := $(UI_STAGE)/lib/pkgconfig/glassine-ui.pc
UI_ENV := PKG_CONFIG_PATH=$(UI_STAGE)/lib/pkgconfig
UI_RPATH := -Wl,-rpath,$(UI_STAGE)/lib
endif
SCREEN_PKGS := glassine-ui pangocairo
SCREEN_CFLAGS = $(shell $(UI_ENV) $(PKG_CONFIG) --cflags $(SCREEN_PKGS))
SCREEN_LIBS = $(shell $(UI_ENV) $(PKG_CONFIG) --libs $(SCREEN_PKGS))

WL_PROTO := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols 2>/dev/null)
WL_PKGS := wayland-client xkbcommon
PROTOS := stable/xdg-shell/xdg-shell.xml stable/tablet/tablet-v2.xml unstable/text-input/text-input-unstable-v3.xml
PROTO_NAMES := $(basename $(notdir $(PROTOS)))
PROTO_C := $(PROTO_NAMES:%=$(O)/proto/%-protocol.c)
PROTO_H := $(PROTO_NAMES:%=$(O)/proto/%-client-protocol.h)

all: $(O)/glassine-planner

$(O):
	mkdir -p $@

$(CORE_OBJ): $(O)/%.o: src/%.c src/*.h | $(O)
	$(CC) $(CFLAGS) -c -o $@ $<

$(SCREEN_OBJ): $(O)/%.o: src/%.c src/*.h $(UI_DEP) | $(O)
	$(CC) $(CFLAGS) $(SCREEN_CFLAGS) -c -o $@ $<

# GlassineOS/ui, staged
$(CURDIR)/$(O)/ui-stage/lib/pkgconfig/glassine-ui.pc:
	$(MAKE) -s -C "$(UI_DIR)" install O=$(CURDIR)/$(O)/ui-build PREFIX=$(CURDIR)/$(O)/ui-stage >/dev/null

# the protocols: xdg-shell, the tablet (the pen), text-input v3 (the on-screen keyboard)
$(O)/proto/%-protocol.c $(O)/proto/%-client-protocol.h: | $(O)
	mkdir -p $(O)/proto
	x=$$(for p in $(PROTOS); do case $$p in */$*.xml) echo $(WL_PROTO)/$$p;; esac; done); \
	wayland-scanner client-header $$x $(O)/proto/$*-client-protocol.h && \
	wayland-scanner private-code $$x $(O)/proto/$*-protocol.c

$(O)/wl.o: src/wl.c src/*.h $(PROTO_H) $(UI_DEP) | $(O)
	$(CC) $(CFLAGS) $(SCREEN_CFLAGS) -I$(O)/proto $(shell $(PKG_CONFIG) --cflags $(WL_PKGS)) -c -o $@ $<

$(O)/glassine-planner: $(O)/wl.o $(PROTO_C) $(SCREEN_OBJ) $(CORE_OBJ) | $(O)
	$(CC) $(CFLAGS) -I$(O)/proto -o $@ $(O)/wl.o $(PROTO_C) $(SCREEN_OBJ) $(CORE_OBJ) \
	    $(SCREEN_LIBS) $(shell $(PKG_CONFIG) --libs $(WL_PKGS)) $(LDLIBS) $(UI_RPATH)

$(O)/test_layout: test/test_layout.c $(O)/layout.o test/t.h | $(O)
	$(CC) $(CFLAGS) -o $@ $< $(O)/layout.o $(LDLIBS)

$(O)/test_model: test/test_model.c $(O)/model.o $(O)/tmpl.o test/t.h | $(O)
	$(CC) $(CFLAGS) -o $@ $< $(O)/model.o $(O)/tmpl.o $(LDLIBS)

$(O)/test_plan: test/test_plan.c $(SCREEN_OBJ) $(CORE_OBJ) test/t.h | $(O)
	$(CC) $(CFLAGS) $(SCREEN_CFLAGS) -o $@ $< $(SCREEN_OBJ) $(CORE_OBJ) $(SCREEN_LIBS) $(LDLIBS) $(UI_RPATH)

# the stand-ins of the smoke run: a virtual pointer, an input method that logs what the
# keyboard would be told (the keyboard's auto-show, as glassine-swayd watches it)
$(O)/vptr: test/smoke/vptr.c | $(O)
	mkdir -p $(O)/proto
	wayland-scanner client-header /usr/share/wlr-protocols/unstable/wlr-virtual-pointer-unstable-v1.xml $(O)/proto/vp.h
	wayland-scanner private-code /usr/share/wlr-protocols/unstable/wlr-virtual-pointer-unstable-v1.xml $(O)/proto/vp.c
	$(CC) $(CFLAGS) -I$(O)/proto -o $@ $< $(O)/proto/vp.c $(shell $(PKG_CONFIG) --cflags --libs wayland-client)

$(O)/imwatch: test/smoke/imwatch.c test/smoke/input-method-unstable-v2.xml | $(O)
	mkdir -p $(O)/proto
	wayland-scanner client-header test/smoke/input-method-unstable-v2.xml $(O)/proto/im.h
	wayland-scanner private-code test/smoke/input-method-unstable-v2.xml $(O)/proto/im.c
	$(CC) $(CFLAGS) -I$(O)/proto -o $@ $< $(O)/proto/im.c $(shell $(PKG_CONFIG) --cflags --libs wayland-client)

check: $(O)/test_layout $(O)/test_model $(O)/test_plan all
	./$(O)/test_layout
	./$(O)/test_model
	FONTCONFIG_FILE=$$(sh test/fonts.sh) ./$(O)/test_plan
	@if [ "$(SMOKE)" = skip ]; then echo "smoke: skipped (SMOKE=skip)"; else $(MAKE) -s smoke; fi

smoke: all $(O)/vptr $(O)/imwatch
	FONTCONFIG_FILE=$$(sh test/fonts.sh) sh test/smoke/run.sh

clean:
	rm -rf $(O)

.PHONY: all check smoke clean
