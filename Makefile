# SPDX-License-Identifier: GPL-2.0-or-later
# glassine-planner: the pen-first planner (spec 001).
# Needs a C11 compiler, pkg-config, cairo, pango and GlassineOS/ui (glassine-ui) for the drawing;
# wayland-client, wayland-protocols, xkbcommon (the keyboard in text mode) and libglassine-handwriting (the pen interface, its writing
# regions) for the program.
#
# GlassineOS/ui: test/ui.sh finds a checkout ($GLASSINE_UI, ../ui, else a clone); it is built and
# installed into out/ui-stage. UI_SYSTEM=1 uses an installed glassine-ui instead.
# libglassine-handwriting: installed (glassine-handwriting-dev), or HW_DIR=a GlassineOS/handwriting
# checkout whose client/ is built into out/hw.
#
#   make check        the core tests and the offscreen renders (out/renders)

CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-override-init -Wno-missing-field-initializers -Werror=implicit-function-declaration
LDLIBS += -lm
PREFIX ?= /usr
DESTDIR ?=

O := out
CORE := store grid
CORE_OBJ := $(CORE:%=$(O)/%.o)
SCREEN := look view
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

ifneq ($(HW_DIR),)
HW_CFLAGS := -I$(HW_DIR)/client
HW_LIBS := -L$(CURDIR)/$(O)/hw -lglassine-handwriting -Wl,-rpath,$(CURDIR)/$(O)/hw
HW_DEP := $(O)/hw/libglassine-handwriting.so
else
HW_CFLAGS :=
HW_LIBS := -lglassine-handwriting
HW_DEP :=
endif

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

# libglassine-handwriting from a checkout
$(O)/hw/libglassine-handwriting.so: | $(O)
	$(MAKE) -s -C "$(HW_DIR)/client" O=$(CURDIR)/$(O)/hw $(CURDIR)/$(O)/hw/libglassine-handwriting.so

$(O)/proto/%-protocol.c $(O)/proto/%-client-protocol.h: | $(O)
	mkdir -p $(O)/proto
	x=$$(for p in $(PROTOS); do case $$p in */$*.xml) echo $(WL_PROTO)/$$p;; esac; done); \
	wayland-scanner client-header $$x $(O)/proto/$*-client-protocol.h && \
	wayland-scanner private-code $$x $(O)/proto/$*-protocol.c

$(O)/wl.o: src/wl.c src/*.h $(PROTO_H) $(UI_DEP) $(HW_DEP) | $(O)
	$(CC) $(CFLAGS) $(SCREEN_CFLAGS) $(HW_CFLAGS) -I$(O)/proto $(shell $(PKG_CONFIG) --cflags $(WL_PKGS)) -c -o $@ $<

$(O)/glassine-planner: $(O)/wl.o $(PROTO_C) $(SCREEN_OBJ) $(CORE_OBJ) | $(O)
	$(CC) $(CFLAGS) -I$(O)/proto -o $@ $(O)/wl.o $(PROTO_C) $(SCREEN_OBJ) $(CORE_OBJ) \
	    $(SCREEN_LIBS) $(HW_LIBS) $(shell $(PKG_CONFIG) --libs $(WL_PKGS)) $(LDLIBS) $(UI_RPATH)

$(O)/sample.o: test/sample.c test/sample.h src/store.h | $(O)
	$(CC) $(CFLAGS) -c -o $@ $<

$(O)/test_store: test/test_store.c $(O)/sample.o $(CORE_OBJ) test/t.h | $(O)
	$(CC) $(CFLAGS) -o $@ $< $(O)/sample.o $(CORE_OBJ) $(LDLIBS)

$(O)/test_view: test/test_view.c $(O)/sample.o $(SCREEN_OBJ) $(CORE_OBJ) test/t.h | $(O)
	$(CC) $(CFLAGS) $(SCREEN_CFLAGS) -o $@ $< $(O)/sample.o $(SCREEN_OBJ) $(CORE_OBJ) $(SCREEN_LIBS) $(LDLIBS) $(UI_RPATH)

$(O)/render: test/render.c $(O)/sample.o $(SCREEN_OBJ) $(CORE_OBJ) | $(O)
	$(CC) $(CFLAGS) $(SCREEN_CFLAGS) -o $@ $< $(O)/sample.o $(SCREEN_OBJ) $(CORE_OBJ) $(SCREEN_LIBS) $(LDLIBS) $(UI_RPATH)

check: $(O)/test_store $(O)/test_view $(O)/render all
	./$(O)/test_store
	FONTCONFIG_FILE=$$(sh test/fonts.sh) ./$(O)/test_view
	FONTCONFIG_FILE=$$(sh test/fonts.sh) ./$(O)/render

renders: $(O)/render
	FONTCONFIG_FILE=$$(sh test/fonts.sh) ./$(O)/render

install: all
	install -Dm755 $(O)/glassine-planner $(DESTDIR)$(PREFIX)/bin/glassine-planner
	install -Dm644 data/glassine-planner.desktop $(DESTDIR)$(PREFIX)/share/applications/glassine-planner.desktop

clean:
	rm -rf $(O)

.PHONY: all check renders install clean
