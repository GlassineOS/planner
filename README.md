# GlassineOS/planner

A pen-first planner for e-ink: month, week and day views in which every text field is
written by hand. The writing stays the person's own ink; under it the planner reads it into
text for search and reminders. Times and dates are set with widgets instead of handwriting,
and each period's page is laid out from a template the person can edit, copy and swap at
any time without losing what was written.

**Early.** The first spec is [`specs/001-planner`](specs/001-planner/spec.md). Built so far:
the day, week and month views with the boards' templates and widgets (GlassineOS/ui
`designs/planner`), handwriting fields as writing regions of the pen interface (each
reading stored with its ink), events drawn or written into the agenda's free time and dragged in
30-minute steps by finger, the (i) popover, the cog's menu, the template picker and the
template editor (New, Copy, Edit). Not yet: text mode, search, planning ahead, reminders.

## Building

    make check        # the store and view tests, and offscreen renders in out/renders
                      # (out/renders-1.6 at 1.6 device px per design px, out/renders-2-t150
                      # at a text size of 150 %)
    make install      # PREFIX=/usr; the program and its desktop entry

It needs cairo, pango, wayland-client, wayland-protocols, GlassineOS/ui (`test/ui.sh` clones
it; `UI_SYSTEM=1` uses an installed glassine-ui) and libglassine-handwriting (installed, or
`HW_DIR=$(sh test/hw.sh)` for a clone). Without the handwriting service's writing regions
the planner shows why and doesn't start (FR-001).

The planner draws at the factor the compositor gives its surface (`wp_fractional_scale_v1`
with `wp_viewporter`, else the output's whole scale; GlassineOS/shell spec 012): a design px
is a logical px, so a smaller interface size gives a larger window. Its text follows
`~/.config/glassine/text-size`, the header bar growing with it; the minimums of text and tap
targets use the density of the output from the session's
`$XDG_RUNTIME_DIR/glassine/output-scale`.

Its entries are kept in `$XDG_DATA_HOME/glassine-planner/planner.ics` (iCalendar), the
template choices in `templates.conf` beside it, and templates made or edited on the device in
`templates/ID.tmpl` (one file each; the format is described in `src/tfile.h`; a file with a
shipped template's id replaces that template). `GLASSINE_PLANNER_DATA` sets another directory.

`test/device/` holds two tools for a device check: `region-smoke.c`, a window with one
writing region that prints what reaches it, and `pen-inject.c`, a virtual screen tablet
(uinput) that draws straight lines.

GlassineOS is a hobby project, written largely with AI coding agents and not reviewed by
experts. Only what a change says was checked on hardware has been.

## License

GPL-2.0-or-later (`LICENSE`).
