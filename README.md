# GlassineOS/planner

A pen-first planner for e-ink: month, week and day views in which every text field is
written by hand. The writing stays the person's own ink; under it the planner reads it into
text for search and reminders. Times and dates are set with widgets instead of handwriting,
and each period's page is laid out from a template the person can edit, copy and swap at
any time without losing what was written.

**Not started.** Nothing is built yet (this branch holds only a throwaway UI spike, see
"Spike" below); the first spec is
[`specs/001-planner`](specs/001-planner/spec.md).

GlassineOS is a hobby project, written largely with AI coding agents and not reviewed by
experts. Nothing in this repository has been checked on hardware.

## Spike

**This branch is a throwaway UI prototype, not meant to merge.** It exists to answer
questions for spec 001 before the real program is planned. The code is C with cairo, pango
and librsvg on plain Wayland, drawn with GlassineOS/ui (Interleaf and the voices), no GTK.

    make check      layout and core unit tests, offscreen renders (out/renders), and a
                    headless sway smoke run (out/shots); test/in-alpine.sh runs it in Alpine 3.24

Layout: `src/layout.c` (host-tested, `test/test_layout.c`). Templates: `templates/*.tmpl`.
The handwriting reading is a stand-in (words become placeholder words); all ink in tests and
screenshots is synthetic. E-ink figures are damage counted on the host, not measured panel
refreshes.

What was learned:

- **Widths from declarations, heights from content.** Widgets declare min, preferred and
  weight on the width axis; on the height axis they report what their content needs. Space
  is shared in three steps: every widget gets its minimum, then grows towards its need by
  weight, then the rest goes to widgets marked `grow`. Weight alone was not enough: a
  separate `grow` flag says who takes leftover space.
- **Two kinds of widget.** Some compress (the agenda shrinks its hour pitch down to a
  minimum), some truncate (lists and notes, with a "+n more" mark). Minimum sizes must
  include room for that mark and for one empty row to write in.
- **One template, two orientations.** A node can change kind by orientation
  (`landscape=row`); without it, landscape day layouts squeezed the lists to one row.
  Seven week columns fit portrait only with a column minimum at or below about 100 px.
- **No relayout while the pen writes.** Layout requests are held while the pen is down and
  for a pause after it lifts; the page settles once. Between strokes nothing repaints; ink
  is drawn straight onto the shown frame and only its box is committed.
- **Rows give way in a fixed order.** When a list is full, done rows hide first, then the
  oldest open ones; the empty row and the row just written never move into "more".
- **Templates are separate from data.** Entries belong to dates; template choices live in
  another file. Switching templates leaves the entries file byte-identical, and the view
  counts the entries it does not show.
- **Drags step locally.** Moving or stretching an event redraws only its column, once per
  30-minute step and once on release.
- **Text mode** uses text-input-v3; the input method is activated, typed keys edit the
  reading, and leaving the field deactivates it.
- **Event titles overflow.** A title field bounded by its time block clips ink in short
  events; the spec must say whether fields may grow past their block.

Unverified: the tablet-v2 pen path on hardware, ink latency on e-ink, and the on-screen
keyboard actually appearing (only the input method's activation is checked).

## License

GPL-2.0-or-later (`LICENSE`).
