# Feature Specification: Planner

**Feature Branch**: `spec-001-planner`
**Status**: Draft
**Input**: A pen-first planner on e-ink with month, week and day views. Every text field is
written by hand and stays the person's own ink; the planner reads it into text for search
and reminders. A text mode edits those readings with the on-screen keyboard. Times and dates
are set with widgets. Pages are laid out from editable templates of reusable widgets on a
fluid grid, and the data belongs to dates, not to layouts.

## Context

A paper planner is written by hand and laid out the way its owner likes: a spread for the
week, a page per day, a habit grid in the margin. A calendar app knows what is written in it,
so it can find things and remind its owner, but it asks for typing and fixes the layout. The
planner keeps the paper side, the person's own ink in a layout of their choosing, and gets the
calendar side from what the handwriting service reads in that ink.

It depends on the handwriting work in `GlassineOS/handwriting`: the pen and service
interfaces (spec 004), writing regions (spec 005), and the person's letter model, font and
neatening (spec 003). It draws in Interleaf, the GlassineOS design system, and its voices
(`GlassineOS/ui`).

Terms used below:

- **Period**: one day, one week or one month in the calendar. Its kind is day, week or month.
- **View**: what the planner shows for one period, drawn from the template chosen for it.
- **Template**: an arrangement of widgets for one kind of period.
- **Widget**: a reusable part of a view (an agenda, a to-do list, notes) that shows some of
  the data of the period it is in.
- **Handwriting field**: a place in a widget where the person writes free text (an event's
  title, a to-do, a note).
- **Reading**: the text the handwriting service reads from the ink of one field.

## User scenarios

### Scenario 1 – Write an event into the day (Priority: P1)

**Given** today's view with a day agenda, **When** the person draws a block over 14:00–15:00
in the agenda and writes "dentist" in it, **Then** an event from 14:00 to 15:00 exists, its
title shows as the person's ink exactly as written, and a moment later searching for
"dentist" finds it. No converted text appears anywhere on the page.

### Scenario 2 – Move and stretch an event without writing (Priority: P1)

**Given** that event, **When** the person drags the block down, **Then** it moves in steps of
30 minutes and lands at 14:30–15:30, its ink unchanged. **When** the person drags its lower
edge, **Then** its length changes in 30-minute steps. **When** the person opens its date and
picks another day, **Then** the event moves there with its ink and its reading.

### Scenario 3 – Write to-dos into the week (Priority: P1)

**Given** this week's view with a to-do list, **When** the person writes three to-dos and
ticks one, **Then** the week holds three to-dos, one done, each shown in the person's ink.
**Given** the day view of a day in that week, **When** its template shows the week's to-dos,
**Then** the same three appear there; ticking one there ticks it in the week.

### Scenario 4 – Switch a template without losing anything (Priority: P1)

**Given** a day with events, to-dos and a note, shown with a template that has an agenda,
a to-do list and notes, **When** the person switches that day to a template with only an
agenda, **Then** the events show in the new layout and the to-dos and the note are kept.
**When** the person switches back, **Then** the to-dos and the note reappear as they were.

### Scenario 5 – Correct a reading in text mode (Priority: P1)

**Given** a to-do written as "call the bank about the card" that was read as "call the bonk
about the card", **When** the person turns that field to text mode, **Then** the reading shows
as an ordinary text field and the system on-screen keyboard opens. **When** the person
changes "bonk" to "bank" and returns to ink mode, **Then** the field shows the person's ink
for every word except "bank", which is set in the person's own font in its place, and search
finds the to-do under "bank".

### Scenario 6 – Plan the year's layouts ahead (Priority: P2)

**Given** several week templates, **When** the person sets one template for January to
March and another from April onwards, **Then** every week shows the template set for it,
and any single week can still be given a different one, before, during or after it.

### Scenario 7 – Edit a template on the device (Priority: P2)

**Given** a day template, **When** the person copies it, adds a habit tracker, moves the
notes below the agenda and gives the agenda more weight, **Then** the copy is a new template
the person can name and pick, and the original is unchanged.

### Scenario 8 – Search what was written by hand (Priority: P2)

**Given** months of handwritten entries, **When** the person searches for a word, **Then**
the planner lists the entries whose reading or edited text contains it, each shown in its
ink with its date, and opening one shows its period with that entry visible.

### Scenario 9 – Be reminded (Priority: P2)

**Given** an event with a reminder, **When** the reminder's time comes, **Then** the person
is reminded of the event and its time, whether or not the planner is open.

### Scenario 10 – Neater ink, by choice (Priority: P3)

**Given** a notes field, **When** the person turns on neatening for that field, **Then** its
ink is shown neatened at the chosen strength and the original can be brought back. **When**
the person instead turns on "set in my font", **Then** each word that was read is shown set
in the person's own font. Both can be turned on for one field or for all fields.

## Requirements

### Views and periods

- **FR-001**: The planner has a month view, a week view and a day view. Each shows one
  period, drawn from the template chosen for that period (FR-030).
- **FR-002**: The person moves to the previous or next period of the same kind, to today,
  and from a period to the periods it contains or lies in (a day of a month view opens that
  day; a day view reaches its week and month).
- **FR-003**: The first day of the week and the date and time formats follow the system's
  settings.

### Handwriting fields

- **FR-004**: Every text field is a handwriting field. The person writes into it with the
  pen; there is no field that asks for typing in ink mode.
- **FR-005**: What the person writes stays the person's ink. In ink mode the planner never
  shows a reading in place of the ink, beside it or over it: no conversion preview, no
  correction pop-up, no candidate list, no peek and no marker of uncertainty.
- **FR-006**: Each field's ink is read into text by the handwriting service through its
  writing regions (handwriting spec 005), one region per field. The reading is stored with
  the entry and is what the planner's data and backend use: search (FR-050), reminders
  (FR-053), and any later sync.
- **FR-007**: A reading is kept up to date when the ink of its field changes (more writing,
  erasing). Reading happens after the person pauses and never delays the ink.
- **FR-008**: Ink that is not read as text (a drawing, an arrow, a doodle) stays ink and is
  kept with the field; it does not count as text.
- **FR-009**: The person erases with the pen's eraser or an erase tool; erasing ink removes
  it and updates the reading.
- **FR-010**: Two opt-ins change how ink is shown, never what is stored as written:
  - **Neaten**: the ink is shown neatened (handwriting spec 003), at a strength the person
    chooses; turning it off shows the ink as written.
  - **Set in my font**: each word that was read is shown set in the person's own font
    instead of the ink; turning it off shows the ink.

  Each can be set for one field or for every field; a field's own choice wins over the
  global one. Both are off unless the person turns them on.

### Text mode

- **FR-011**: A control turns one field, or the whole view, into text mode. In text mode
  each handwriting field shows its current text (FR-013) as an ordinary text field, edited
  with the system on-screen keyboard.
- **FR-012**: Leaving text mode returns the field or view to ink mode. Text mode never
  changes the ink.
- **FR-013**: A field's current text is its reading, with the person's text-mode edits
  applied. When a field's text was changed in text mode, ink mode shows the field word by
  word: words the person did not change keep their ink, and words the person changed or
  added are set in the person's font in their place. A deleted word's ink is not shown.
- **FR-014**: Writing more ink into a field that has edited words adds that ink in its place
  and its reading to the current text; the edited words stay as edited.
- **FR-015**: A field whose ink is erased completely and whose edited text is empty is empty.
- **FR-016**: Text mode has no correction suggestions, candidate lists or markers of
  uncertainty either; it shows the text and lets the person change it.

### Times and dates

- **FR-017**: Times and dates are never written by hand. They are set with widgets made for
  them: drawing a block in an agenda creates an event over that time; dragging an event's
  block moves it and dragging its edge changes its length, both in steps of 30 minutes;
  date pickers set or change the day, week or month an entry belongs to.
- **FR-018**: Every handwriting field is free text. Writing a time or date into a field does
  not set the entry's time or date.
- **FR-019**: An event is timed (a start and an end on a day) or all-day (one or more whole
  days). Timed events may cross midnight; they show on each day they cover.

### Data belongs to dates

- **FR-020**: Every entry belongs to a date, not to a layout:
  - an **event** to a day and a time, or to whole days (FR-019);
  - a **to-do** to a day, a week or a month, with a done state;
  - a **note** to a day, a week or a month;
  - a **priority** to a day, a week or a month, with a rank;
  - a **habit mark** to a habit and a day.
- **FR-021**: A template is a view of the data. Choosing, switching, editing or deleting a
  template never creates, changes or deletes an entry.
- **FR-022**: An entry no widget of the current view shows stays stored, unchanged, and
  appears again in any view whose template shows it.
- **FR-023**: A view tells the person, without opening anything, when its period holds
  entries none of its widgets shows, and how many of each kind; from there the person can
  see them or switch to a template that shows them.
- **FR-024**: Entries of a longer period may show in a shorter one when the shorter view's
  template asks for them (the week's to-dos in a day view), and entries of shorter periods
  in a longer one (a month view's days with their events). Changing an entry in any view
  changes the one entry.

### Templates

- **FR-030**: Each period kind (month, week, day) has several templates. Each period has at
  most one template chosen for it; a period with none uses its kind's default (FR-034).
- **FR-031**: The person can create, name, copy, edit, delete and pick templates on the
  device. A template that a period uses can't be deleted without the person choosing what
  those periods use instead.
- **FR-032**: The person can choose a template for a single period, for a range of periods
  (all weeks from April to June), or for every period of a kind from a date onwards, ahead
  of time; a choice for a single period wins over a range.
- **FR-033**: The person can switch the template of any period at any time, including
  periods in the past. A switch takes effect at once and follows FR-021.
- **FR-034**: Each kind has one default template, which the person can change. The planner
  comes with at least one template for each kind.

### Widgets

- **FR-040**: Widgets are reusable across templates. The planner ships at least:
  - **day agenda**: the hours of a day with its timed events as blocks, all-day events above;
  - **to-do list**: the to-dos of a period, to write, tick and reorder;
  - **free notes**: an open writing area for the notes of a period;
  - **habit tracker**: the person's habits against the days of the period, a tap marks a day;
  - **mini month**: the days of a month, marking days with entries, a tap opens a day;
  - **week overview**: the days of a week with their events and to-dos in short;
  - **priorities**: a short ranked list for a period.
- **FR-041**: Each widget declares which data it shows (which kinds of entry, of which
  period relative to the view's period) and its minimum size and preferred size.
- **FR-042**: A widget may have settings that change what it shows (an agenda's first and
  last hour, a to-do list's period), set per use in a template.

### Layout

- **FR-043**: A template places widgets on a grid as an arrangement with relative weights,
  not fixed sizes. Widgets give and take space according to their content: an empty to-do
  list shrinks towards its minimum, a full agenda grows towards its preferred size, within
  the weights the template sets.
- **FR-044**: Every widget gets at least its minimum size. When the view can't give every
  widget its minimum, the template editor says so before the template is saved.
- **FR-045**: In the template editor the person adds, removes, moves and resizes widgets
  and changes their weights, and sees the result with the data of a real period.
- **FR-046**: Layout changes caused by content never move ink under the pen: while the pen
  is writing in a widget, and until the person pauses, the layout stays as it is; it settles
  afterwards, in one refresh.
- **FR-047**: Ink is anchored to its field. When a field changes size or place, its ink
  moves with it unscaled; a field never becomes smaller than the ink it holds, unless the
  widget is at its minimum (FR-048).
- **FR-048**: When a widget's content doesn't fit its space, the widget shows what fits and
  a mark that more exists, with the count where it is countable; a tap opens the widget's
  whole content. Nothing is dropped and ink is never shrunk to fit.

### Search, reminders

- **FR-050**: Search finds entries whose current text (FR-013) contains the words searched
  for, across all periods. Results show the entries' ink with their dates; opening one shows
  its period with that entry visible, under a template that shows it or by itself when the
  period's template doesn't.
- **FR-051**: Entries are written to the person's storage as soon as they are written; a
  crash, a power loss or a full battery cut loses at most the stroke being drawn.
- **FR-052**: Readings are made on the device. Nothing the person writes leaves the device.
- **FR-053**: An event can have a reminder at a chosen time before it. At that time the
  person is reminded of the event and its time, also when the planner is closed.
  [NEEDS CLARIFICATION: Must a reminder wake the device from sleep, and how is it shown on a
  sleeping e-ink screen (on the sleep screen, a light, a sound), or does it only show at the
  next wake? And does a reminder outside the planner show the event's ink or its text?]
- **FR-054**: Events may repeat: [NEEDS CLARIFICATION: which repeat rules belong in 001?
  Proposed basics: daily, weekly on chosen weekdays, monthly on the same date, yearly, with an
  end date or none; changing or removing one occurrence or all of them. Everything else
  (nth weekday of a month, exceptions lists, repeating to-dos) later?]

### Look

- **FR-060**: The planner draws in Interleaf in the voice the person chose for the system,
  and changes voice with it.
- **FR-061**: Nothing animates. Input is answered in place, a period change or template
  switch is one refresh, and a change inside a widget refreshes only that widget's area.
  Ink is drawn with the pen's normal latency.

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| Pen interface (raw and neatened strokes, readings) | consumed | 1 | `GlassineOS/handwriting` spec 004 |
| Writing regions (one region per handwriting field) | consumed | not yet specified | `GlassineOS/handwriting` spec 005 (not published yet) |
| Service interface (reading saved ink, letter model and font status) | consumed | 1 | `GlassineOS/handwriting` spec 004 |
| Neatening and the person's font | consumed | 1 | `GlassineOS/handwriting` spec 003 |
| Interleaf and the voices, the drawing library | consumed | current | `GlassineOS/ui` |
| System on-screen keyboard | consumed | — | `GlassineOS/shell`, `GlassineOS/wvkbd` |
| Ink protocol (ink areas, brushes) | consumed | glassine_ink_v1 | `GlassineOS/protocols` |
| The planner's stored data | owned, not yet an interface | — | later spec; the format is out of scope here |

## Device scope and risk

Device-neutral: any device with a pen and an e-ink panel; a device without a pen gets no
handwriting fields and is not a target of this spec. No device risk. User data: the planner
keeps the person's entries and templates in the person's storage; FR-021 and FR-051 protect
them.

## Acceptance

Host level:

- switching through every shipped template of a kind and back leaves the stored entries
  byte-for-byte as before, for a period holding every kind of entry;
- an entry shown by no widget is counted by the view's notice (FR-023) and reappears with a
  template that shows it;
- recorded pen input for a field gives a stored reading, and search finds it;
- a text-mode edit of one word shows that word in the font and every other word as ink;
- the layout gives every widget at least its minimum; content past it raises the more mark;
- a layout change requested while a stroke is in progress is held until the pause;
- dragging an event moves it in 30-minute steps;
- offscreen renders of every shipped template in each voice.

Product level: in the images acceptance suite, write an event and a to-do by pen, switch the
day's template to one without a to-do list and back, and find the to-do by search.

Hardware: ink latency and refresh counts on a device, supervised.

## Out of scope

- CalDAV and any other sync. The readings make it possible later; nothing in 001 depends on
  it.
- Sharing entries, templates or calendars with other people.
- Repeat rules beyond the basics FR-054 settles.
- The storage format.
- Importing from or exporting to other calendars.
- Time zones: times are the device's local wall-clock time.
- Undated entries (an inbox or "someday" list).

## Key entities

- **Period**: a day, week or month; identified by its kind and its dates.
- **Entry**: an event, to-do, note, priority or habit mark, attached to a date (FR-020). An
  entry with text holds one or more handwriting fields.
- **Handwriting field**: the ink written into it, its reading, the person's text-mode edits
  and its display opt-ins (FR-010).
- **Habit**: a named habit the habit tracker lists; its marks belong to days.
- **Template**: a named arrangement of widgets for one period kind, with weights and widget
  settings.
- **Template choice**: which template a period, a range of periods or a kind from a date on
  uses; plus each kind's default.
- **Widget**: a reusable part of a view with what data it shows and its minimum and preferred
  size.
- **Reminder**: a time before an event at which the person is reminded.

## Edge cases

- **Switching to a template that shows some data nowhere**: the entries stay stored (FR-022)
  and the view's notice names what is hidden (FR-023); nothing asks the person to confirm,
  since nothing is lost.
- **A widget too small for its content**: it shows what fits and the more mark; a tap opens
  all of it (FR-048). A template whose widgets can't all get their minimum can't be saved
  without the person seeing that (FR-044).
- **Writing into a field that's being resized**: the layout holds while the pen writes and
  settles after the pause (FR-046); ink moves with its field and is never scaled (FR-047).
  Resizing in the template editor applies to the template, not to entries.
- **Text mode on a whole view**: every handwriting field in the view becomes a text field;
  time and date widgets stay as they are. Ink with no reading (FR-008) stays visible and is
  not editable as text. The keyboard takes space from the view in one refresh and gives it
  back on leaving.
- **A person without a letter model**: readings, search and reminders work as for anyone.
  Words edited in text mode are set in the voice's text face until the person's font exists,
  then in the font (open question below). "Set in my font" can't be turned on and says why; neatening offers what
  the handwriting service can do without a letter model.
- **A period with no template chosen**: it uses the default of its kind (FR-034); the person
  can choose another from the view.
- **An entry written in a field and then moved to another date**: the ink, reading and edits
  move with it.
- **Erasing all of an event's title**: the event stays with its time and an empty title; it
  is removed only by an explicit delete.
- **A reading that fails** (the service is not running, a script it can't read): the ink is
  kept, the field has no text yet and is read again later; search can't find it until then.
- **A to-do of a week that spans two months**: it belongs to the week; month views show it
  only if their template asks for the weeks' to-dos.
- **Deleting a template in use**: FR-031.

## Assumptions

- The handwriting service and the person's font are available as handwriting specs 003 and
  004 describe; the planner adds no recognizer of its own.
- Writing regions (handwriting spec 005) are not published yet. The planner assumes of them:
  one region per handwriting field, read on its own; a reading that says which strokes form
  which word, so text-mode edits can replace single words (FR-013); and readings that follow
  changes to a region's ink (FR-007).
- A new period with no template chosen gets its kind's default without asking; the person
  picks another from the view when they want one.
- Without a letter model or font, words edited in text mode are set in the voice's text face
  (see the open question below).
- An event is created by drawing a block over its time in an agenda, or with a date and time
  picker; there is no other way in 001.
- Every entry has a date; there is no undated inbox or "someday" list in 001.
- The planner runs in the person's own session; guests see none of its data.
- One person, one planner: there are no separate calendars to pick from in 001.
- The 30-minute step is the same for every agenda.
- A day agenda shows the hours its settings name; events outside them show as a count at
  its top or bottom edge.

## Open questions

- [NEEDS CLARIFICATION: Unfinished to-dos when their period ends: do they stay where they
  were, move to the next period by themselves, or wait for the person to move them (with a
  one-tap "move to next")?]
- [NEEDS CLARIFICATION: Editing a template that past periods use: does the edit change how
  those past periods look, or do periods that already used it keep the old arrangement?]
- [NEEDS CLARIFICATION: A person without a letter model or font who edits a word in text
  mode: is that word set in the voice's text face (typed-looking text inside the ink, until
  the font exists), or does editing in text mode wait until the font exists, or is the edit
  kept in the text only while the ink stays as written?]

FR-053 (reminders on a sleeping device) and FR-054 (which repeat rules) are open as well.

## Success criteria

- **SC-001**: Writing a handwritten event or to-do takes no more steps than on paper plus
  setting its time with the widget: no confirmation, no conversion step.
- **SC-002**: Switching a period's template through every shipped template and back loses
  and changes no entry, in every test case.
- **SC-003**: An entry written by hand can be found by search within 5 seconds of the
  person's pause, for at least 95 % of entries whose reading matches what was written.
- **SC-004**: A period change or template switch costs one panel refresh; a change inside a
  widget refreshes only that widget's area; nothing on screen moves while the pen writes.
- **SC-005**: A person new to the planner can copy a template, add a widget and use the copy
  for next week in under 3 minutes without help.
- **SC-006**: In ink mode no converted text is ever shown, except words the person edited
  in text mode or chose to set in their font.
