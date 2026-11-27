# Feature Specification: Planner

**Feature Branch**: `spec-001-planner`
**Status**: Draft
**Input**: A pen-first planner on e-ink with month, week and day views. Every text field is
written by hand and stays the person's own ink; the handwriting service reads it into text,
which the planner's data uses. A text mode edits those readings with the on-screen keyboard.
Times and dates are set with widgets. Pages are laid out from editable templates of reusable
widgets on a fluid grid, and the data belongs to dates, not to layouts.

## Context

A paper planner is written by hand and laid out the way its owner likes: a spread for the
week, a page per day, a habit grid in the margin. A calendar app knows what is written in it,
so it can find things, remind its owner and sync, but it asks for typing and fixes the
layout. The planner keeps the paper side, the person's own ink in a layout of their choosing,
and makes the calendar side possible from what the handwriting service reads in that ink.

This spec is the planner's handwriting frontend: the views, the fields, the templates and
widgets, and the data they write. The data live in the system's calendar and contacts
store (GlassineOS/docs decision 0017), where other apps on the device read and change them
and from where they are synced (FR-020, FR-025a). Reminders, notifications and repeat rules
are a separate piece with its own spec later.

It depends on the handwriting work in `GlassineOS/handwriting`: writing regions (spec 005),
the pen and service interfaces they extend (spec 004), and neatening and the person's font,
Autograph (spec 003). It draws in Interleaf, the GlassineOS design system, and its voices
(`GlassineOS/ui`).

Terms used below:

- **Period**: one day, one week or one month in the calendar. Its kind is day, week or month.
- **View**: what the planner shows for one period, drawn from the template chosen for it.
- **Template**: an arrangement of widgets for one kind of period.
- **Widget**: a reusable part of a view (an agenda, a to-do list, notes) that shows some of
  the data of the period it is in.
- **Handwriting field**: a place in a widget where the person writes free text (an event's
  title, a to-do, a note); one writing region of handwriting spec 005.
- **Reading**: the text the handwriting service reads from the ink of one field.
- **Slot**: the name of a free-text place in a period ("morning notes", "evening notes"),
  shared by every template that uses the name.
- **Header bar**: the one bar at the top of every view; it holds only navigation and the
  details button (i).
- **Details popup**: the popup the (i) opens, with the period's details and search; its
  cogwheel turns it into the maintenance menu.

## User scenarios

### Scenario 1 – Write an event into the day (Priority: P1)

**Given** today's view with a day agenda, **When** the person draws a line down the hour
margin from 14:00 to 15:00 with the pen and writes "dentist" between the brackets that
appear, **Then** an event from 14:00 to 15:00 exists, shown as a pair of brackets, `[` in the
hour margin and `]` at the agenda's right end, its title the person's ink exactly as
written, and a moment later searching for "dentist" finds it. No converted text appears
anywhere on the page. **When** the title is longer than the hour holds, **Then** its title
field grows into the free time below the brackets and the event still ends at 15:00.
**When** another event later takes that free time, **Then** the title shrinks evenly to fit,
and if even its smallest readable size doesn't fit, it is cut before the `]`; a tap on it
opens the event's card, which shows the whole title. **When** a call to the bank from 14:30
to 15:30 is added, **Then** both events stand side by side in two equal lanes for as long as
either runs, the dentist in the first, and a stroke belongs to the lane its pen-down point is
in.

### Scenario 2 – Move and stretch an event without writing (Priority: P1)

**Given** that event, **When** the person drags its title down with a finger, **Then** its
brackets turn dashed and it moves in steps of 30 minutes while the hour margin shows its
start, the page refreshing once per step and only where the brackets leave and enter, and
it lands at 14:30–15:30 with solid brackets, its ink unchanged. **When** the person drags the
handle at the bottom of its `[`, **Then** its end changes in 30-minute steps, the held
serifs thicker. **When** the person taps its title, opens its page and picks another day,
**Then** the event moves there with its ink and its reading. **When** the person strikes its
title through with the pen from before its first letter to past its last, **Then** a card
asks "Delete this event?"; Keep brings it back as it was, Delete event leaves the time
free.

### Scenario 3 – Write to-dos into the week (Priority: P1)

**Given** this week's view with a to-do list, **When** the person writes three to-dos and
ticks one, **Then** the week holds three to-dos, one done, each shown in the person's ink.
**Given** the day view of a day in that week, **When** its template shows the week's to-dos,
**Then** the same three appear there; ticking one there ticks it in the week. **When** the
list is full, **Then** the done to-do gives way first, a "+1 more" says so, and one empty row
to write in stays.

### Scenario 4 – Switch a template without losing anything (Priority: P1)

**Given** a day with events, to-dos and a "morning notes" entry, shown with a template that
has an agenda, a to-do list and morning notes, **When** the person switches that day to a
template with only an agenda, **Then** the events show in the new layout, and a small number
beside the (i) in the header bar gives the count of hidden entries. **When** the person taps
the (i), **Then** the details popup names the template and how the day got it, and lists the
to-dos and the "morning notes" as not shown. **When** the person taps the to-dos there,
**Then** the hidden to-dos are listed, still in ink. **When** the person switches back,
**Then** the to-dos and the note show in their places as they were, and the number beside the
(i) is gone.

### Scenario 5 – Correct a reading in text mode (Priority: P1)

**Given** a to-do written as "call the bank about the card" that was read as "call the bonk
about the card", **When** the person taps that field with a finger, **Then** the reading shows
as an ordinary text field and the system on-screen keyboard opens, with the field's Neaten
and Set-in-Autograph switches on the bar above it. **When** the person changes "bonk" to
"bank" and taps outside the field, **Then** it returns to ink mode and shows the person's ink
for every word except "bank", which is set in Autograph, the person's font, in its place, and
search finds the to-do under "bank".

### Scenario 6 – Keep a diary (Priority: P1)

**Given** a day template with "morning notes" and "evening notes", **When** the person writes
into both, **Then** the day holds two journal entries, one per slot. **Given** another day
template that also has "evening notes", **When** the person switches to it, **Then** the
evening entry shows there, and the morning entry is counted as hidden.

### Scenario 7 – Plan the year's layouts ahead (Priority: P2)

**Given** several week templates, **When** the person sets one template for January to
March and another from April onwards, **Then** every week shows the template set for it,
and any single week can still be given a different one, before, during or after it.

### Scenario 8 – Edit and copy templates on the device (Priority: P2)

**Given** a day template, **When** the person adds a habit tracker, moves the notes below the
agenda and gives the agenda more weight, **Then** every day that uses the template, past days
included, shows the new arrangement. **When** the person wants the old arrangement kept for
some days, **Then** they copy the template before editing and give those days the copy.

### Scenario 9 – Search what was written by hand (Priority: P2)

**Given** months of handwritten entries, **When** the person opens the details popup and
types a word into the search field at its top, **Then** the planner lists the entries whose
text contains it, each shown in its ink with its date, and opening one shows its period with
that entry visible.

### Scenario 10 – Neater ink, by choice (Priority: P3)

**Given** a notes field in text mode, **When** the person turns on Neaten on the bar above the
keyboard and leaves the field, **Then** its ink is shown neatened at the chosen strength and
the original can be brought back. **When** the person instead turns on Set in Autograph,
**Then** each word that was read is shown set in Autograph. Both can be turned on for one
field there, or for all fields in the settings.

### Scenario 11 – No handwriting service (Priority: P3)

**Given** a system without writing regions, **When** the person opens the planner, **Then**
it says that it needs the handwriting service and why, and doesn't start.

## Requirements

### Start

- **FR-001**: The planner needs writing regions (handwriting spec 005). When the system
  doesn't offer them, the planner doesn't start and tells the person, in plain words, that it
  needs the handwriting service. There is no keyboard-only mode.

### Views and periods

- **FR-002**: The planner has a month view, a week view and a day view. Each shows one
  period, drawn from the template chosen for that period (FR-030).
- **FR-003**: The view's chrome is one header bar that holds only navigation (Month, Week
  and Day; previous, next and today) and, at its end, the (i) (FR-051); the rest of the
  screen is the period's content. There is no permanent foot line or status line. With the
  navigation the person moves to the previous or next period of the same kind, to today,
  and from a period to the periods it contains or lies in (a day of a month view opens that
  day; a day view reaches its week and month).
- **FR-004**: The first day of the week and the date and time formats follow the system's
  settings.

### Handwriting fields

- **FR-005**: Every text field is a handwriting field. The person writes into it with the
  pen; there is no field that asks for typing in ink mode.
- **FR-006**: What the person writes stays the person's ink. In ink mode the planner never
  shows a reading in place of the ink, beside it or over it: no conversion preview, no
  correction pop-up, no candidate list, no peek and no marker of uncertainty.
- **FR-007**: Each handwriting field is one writing region (handwriting spec 005). Its
  reading is stored with its entry as the entry's text, and is what the planner's data use:
  search (FR-050) here, and every other app that reads the entry (FR-025a).
- **FR-008**: A reading is kept up to date when the ink of its field changes (more writing,
  erasing). Reading happens after the person pauses and never delays the ink.
- **FR-009**: Ink that is not read as text (a drawing, an arrow, a doodle) stays ink and is
  kept with the field; it does not count as text.
- **FR-010**: The person erases with the pen's eraser or an erase tool; erasing ink removes
  it and updates the reading.
- **FR-011**: Two opt-ins change how ink is shown, never what is stored as written:
  - **Neaten**: the ink is shown neatened (handwriting spec 003), at a strength the person
    chooses; turning it off shows the ink as written.
  - **Set in Autograph**: each word that was read is shown set in Autograph, the person's
    font, instead of the ink; turning it off shows the ink.

  Each can be set for one field or for every field; a field's own choice wins over the
  global one. A field's two switches sit on the bar above the keyboard in that field's text
  mode (FR-012); the global ones are in the settings (FR-053). Both are off unless the
  person turns them on.

### Text mode

- **FR-012**: A tap with a finger on a handwriting field opens that field's text mode (the
  title of an event in an agenda opens its card instead, FR-018; its page's title opens text
  mode); a tap outside the field leaves it. The pen writes and never opens text mode. In text mode a
  field shows its current text (FR-014) as an ordinary text field, edited with the system
  on-screen keyboard; the bar above the keyboard holds the field's Neaten and Set in
  Autograph switches (FR-011). Opening or leaving a field's text mode lays nothing out
  again.
- **FR-013**: Text mode for the whole view is turned on and off from the maintenance menu
  (FR-053). Every handwriting field of the view then shows its current text as a text field;
  the keyboard covers the lower part of the screen and the view is not laid out again;
  fields under the keyboard are reached by scrolling the view. Leaving text mode returns
  the field or view to ink mode. Text mode never changes the ink.
- **FR-014**: A field's current text is its reading, with the person's text-mode edits
  applied. When a field's text was changed in text mode, ink mode shows the field word by
  word: words the person did not change keep their ink, and words the person changed or
  added are set in Autograph in their place. A deleted word's ink is not shown.
- **FR-015**: Writing more ink into a field that has edited words adds that ink in its place
  and its reading to the current text; the edited words stay as edited.
- **FR-016**: A field whose ink is erased completely and whose edited text is empty is empty.
- **FR-017**: Text mode has no correction suggestions, candidate lists or markers of
  uncertainty either; it shows the text and lets the person change it.

### Times and dates

- **FR-018**: Times and dates are never written by hand. They are set with widgets made for
  them:
  - drawing a line with the pen down an agenda's hour margin creates an event over that
    time, in 30-minute steps from the half hour it starts in, at least 30 minutes; while it
    is drawn, a dashed bracket and the start and end in the margin show what it covers.
    Drawing a block into the free time creates an event too, and so does writing into it.
    The free time is a writing region with reading off (handwriting spec 005): a stroke
    there is never read as text itself. A stroke taller than a written letter, running down
    over an hour or more and much taller than wide, is the block: the event covers it in
    30-minute steps and the stroke goes. Any other stroke is writing: the event starts at the
    half hour the stroke starts in and lasts an hour, the stroke stays where it was written
    as the first ink of the event's title, and the title takes the next stroke at once, so
    writing into an empty slot is writing from the first stroke;
  - a finger on an event's title that moves more than 8 px moves the event, and one on the
    handle at the top or the bottom of its `[` moves that end, both in steps of 30 minutes
    and at least 30 minutes long. While moved, the brackets are dashed; while an end is
    held, its serifs are thicker. During a drag the view refreshes once per step, only
    where the brackets leave and enter. The only other mark during a drag is, in the
    agenda's hour margin, the time of the end that moves (the start when moved); that
    readout is part of the margin, changes with each step and goes on release. An event
    held past the agenda's first or last hour scrolls its hours one step per step, with
    the readout pinned at that edge of the margin; on release the agenda shows its own
    hours again, and the release refreshes only the brackets' place and the margin;
  - date pickers set or change the day, week or month an entry belongs to; on an event's
    page the day is picked from a month and − and + move the start or the end by 30
    minutes.

  Every timed event in a day agenda is a pair of brackets: `[` in the hour margin and `]` at
  the agenda's right end, from its start to its end, their ends turned in as serifs, with no
  box and no fill; its title is written between them. Events that overlap, one through the
  next, split the width into equal lanes for the whole span of the group, the first start
  in the first lane (the longer one first at the same start), each taking the first lane
  free at its start, at most three lanes (a fourth at the same time shares the third).
  Each lane holds one event's brackets and its title at its start; neighbours meet as
  `] [` 12 px apart, and the hour lines run on across. A stroke belongs to the event whose
  lane holds its pen-down point, never to two. A stroke that comes down outside every
  bracket at a time an event already holds (the gap between lanes, beside a narrow lane)
  belongs to no event and makes none; it is not kept. While the pen writes in a lane, the other brackets of its group are light grey,
  black again about a second after the pen lifts; a pen that reports hover shows the same
  before it touches. The end handles of a lane after the first cover only the gap and the
  serif, so they never take a pen-down from the neighbour's title. A small agenda keeps
  each title to one line beside its event.

  When an event's title needs more room than its brackets, the title field grows into the
  free time below them, never over another title; the event's start and end stay as set.
  A title that no longer fits its space (the event is shortened, another event takes the
  free time below, the view is smaller, or the title is set in the person's font) shrinks
  evenly to fit, down to a smallest readable size; below that size it is cut before the
  `]`. No title is cut with an ellipsis.

  A tap on an event's title opens its card under the title (above it when there is no
  room): the title as written, its time and day, Delete event and Open; a tap outside
  closes it. Open turns the screen into the event's page, whose bar holds only Back, to the
  event's day: its title, written with the pen (a finger tap types it, FR-012), its day and
  times, picked, its place (Where), notes and participants (With), written, and Repeat
  (never, daily, weekly, monthly, yearly) and Remind (none, at its start, 15 minutes, an
  hour or a day before), chosen. Every change is kept as it is made; there is no Save.
  Repeat and Remind are kept with the event as standard calendar properties; repeating it
  and reminding are not the planner's (out of scope).

  An event is deleted only through a card that asks first, "Delete this event?" with the
  title as written, its time and day, Keep and Delete event, which opens in one refresh of
  its rectangle: after a pen stroke across the title that starts before its first letter
  and runs past its last (a stroke that starts inside the title or doesn't cross all of it
  is writing, and the stroke itself is not kept), after a finger held still on the title or
  an end handle for 0.5 s, or from Delete event on its card. The event page's Delete event
  asks the same in a dialog. Keep, or a tap outside, closes the card and the event is as it
  was; Delete event removes the brackets and the title, and the time is free again.
- **FR-019**: Every handwriting field is free text. Writing a time or date into a field does
  not set the entry's time or date. An event is timed (a start and an end, which may cross
  midnight; it shows on each day it covers) or all-day (one or more whole days).

### Data belongs to dates

- **FR-020**: The planner's data are standard calendar objects (events, to-dos, journal
  entries), each tied to dates, kept in the system's calendar and contacts store.
  The kinds of entry are:
  - an **event**, on a day and a time or on whole days;
  - a **to-do**, of a day, a week or a month, with a done state;
  - a **note** (a journal or diary entry), of a day, a week or a month;
  - a **priority**, of a day, a week or a month, with a rank;
  - a **habit mark**, of a habit and a day.

  A habit or a priority is carried in a standard calendar object too; the planner doesn't
  invent a kind of object a calendar can't hold.
- **FR-021**: The ink is attached to its entry, with its reading and the person's text-mode
  edits; moving an entry to another date moves them with it.
- **FR-022**: Every entry is keyed by its date or period and its kind, and an entry of free
  text (a note) also by its slot, a name such as "morning notes" or "evening notes". A
  widget that shows free text names the slot it shows; templates that use the same slot name
  show the same entries.
- **FR-023**: Entries of a longer period may show in a shorter one when the shorter view's
  template asks for them (the week's to-dos in a day view), and entries of shorter periods
  in a longer one (a month view's days with their events). Changing an entry in any view
  changes the one entry.
- **FR-024**: Entries are kept as soon as they are written; a crash, a power loss or a cut
  battery loses at most the stroke being drawn.
- **FR-025**: Nothing the person writes leaves the device unless the person sets up syncing
  for the system's store; the planner itself sends nothing anywhere.
- **FR-025a**: Other apps share the entries. Every entry carries its reading in the standard
  text of its kind (title, description, place, participants), so an app that knows nothing of
  ink shows the entry as text. The ink, slot and template choices travel with the entry, and
  an app that doesn't know them keeps them. A change another app makes shows in the planner
  without a restart; where another app has changed the text of a handwritten field, the field
  shows that text typed, and its earlier ink no longer shows.

### No template operation loses data

- **FR-026**: A template is a view of the data. Choosing, switching, editing, copying or
  deleting a template never creates, changes or deletes an entry.
- **FR-027**: An entry that no widget of the current view shows stays stored, unchanged, and
  appears again in any view whose template shows it.
- **FR-028**: A view whose period holds entries none of its widgets shows names them in its
  details popup (FR-051) per kind, and per slot for notes, each one tap from its
  hidden-entries list. That list shows them in ink, where the person can open, move or
  delete each one. A small number beside the (i) gives how many entries are hidden
  (FR-052).
- **FR-029**: A template has no coverage rules: it may show any part of its period's data,
  or none of a kind (a week template need not show all seven days).

### Templates

- **FR-030**: Each period kind (month, week, day) has several templates. Each period has at
  most one template chosen for it; a period with none uses its kind's default (FR-034).
- **FR-031**: The person can create, name, copy, edit, delete and pick templates on the
  device. A template that periods use can't be deleted without the person choosing what
  those periods use instead.
- **FR-032**: Editing a template changes it wherever it is used, past periods included.
  Copying a template before editing is the way to keep the old arrangement for some periods.
  A template the planner comes with that the person edited can go back to the version it
  came with, after asking; like an edit, that applies wherever it is used.
- **FR-033**: The person can choose a template for a single period, for a range of periods
  (all weeks from April to June), or for every period of a kind from a date onwards, ahead
  of time; a choice for a single period wins over a range. The person can switch the
  template of any period at any time, including periods in the past; a switch takes effect
  at once.
- **FR-034**: Each kind has one default template, which the person can change. The planner
  comes with at least one template for each kind.

### Widgets

- **FR-040**: Widgets are reusable across templates. The planner ships at least:
  - **day agenda**: the hours of a day with its timed events as bracket pairs in lanes,
    all-day events above;
  - **to-do list**: the to-dos of a period, to write, tick and reorder;
  - **free notes**: an open writing area for the notes of a period, in a named slot;
  - **habit tracker**: the person's habits against the days of the period, a tap marks a day;
  - **mini month**: the days of a month, marking days with entries, a tap opens a day;
  - **week overview**: the days of a week with their events and to-dos in short;
  - **priorities**: a short ranked list for a period.
- **FR-041**: Each widget declares which data it shows (which kinds of entry, of which
  period relative to the view's period, and for free text which slot) and its size:
  - its width as a minimum, a preferred width, a weight and whether it takes leftover space;
  - its height as a minimum, beyond which its content says how much it needs.

  Each widget is either compressing or truncating. A compressing widget (the agenda) scales
  its layout down towards its minimum, the agenda by narrowing its hour spacing; a
  truncating widget (lists, notes) shows what fits and "+n more" (FR-048). A widget's
  minimum includes room for that mark and for one empty row to write in.
- **FR-042**: A widget may have settings that change what it shows (an agenda's first and
  last hour, a to-do list's period, a notes widget's slot), set per use in a template.
  A notes widget's slot is one already in use or a new name the person writes with the pen.

### Layout

- **FR-043**: A template places widgets on a grid as an arrangement of rows and columns, not
  fixed sizes. Space is shared along each axis in three steps: every widget gets its
  minimum; then widgets grow towards what they need, by weight; then the rest goes to the
  widgets that take leftover space. An empty to-do list stays near its minimum, a full
  agenda grows towards its preferred size. A part of a template may be a row in one
  orientation and a column in the other; the view follows the orientation of the screen.
- **FR-044**: Every widget gets at least its minimum size. When the view can't give every
  widget its minimum, in either orientation, the template editor says so and by how much,
  and doesn't save the template until it fits.
- **FR-045**: In the template editor the person adds, removes, moves and resizes widgets,
  changes their weights and settings, and sees the result with the data of a real period.
  Each of these has a tap target; dragging a widget or its edge does the same by hand. The
  editor only arranges: its view of the period's entries can't be written into, and
  nothing done in it creates, changes or deletes an entry.
- **FR-046**: There is no relayout while the person writes: while the pen is down, and
  until the person pauses after lifting it, the layout stays as it is. It settles once after
  the pause, in one refresh of only the widgets whose size changed. A tap on a control (the
  bar, a date, a popover) is no writing: what it changes is laid out at once.
- **FR-047**: Ink is anchored to its field. When a field changes size or place, its ink
  moves with it unscaled; a field never becomes smaller than the ink it holds, unless the
  widget is at its minimum (FR-048).
- **FR-048**: When a truncating widget's content doesn't fit its space, it shows what fits
  and "+n more" with the count of what doesn't; a tap opens the widget's whole content. In
  a full list, done entries give way first, then the oldest open ones; one empty row to
  write in always stays. Nothing is dropped and ink is never shrunk to fit.
- **FR-049**: The editor opens from the template picker and from the maintenance menu.
  New starts an empty template of the kind and Copy a copy of the chosen template; each
  has a name from the start, which the person can write over with the pen. Save keeps the
  template and applies it wherever it is used; a new template or a copy is used by no
  period until the person chooses it. Cancel leaves the template as it was, and asks first
  when something was changed.

### Details, maintenance and search

- **FR-050**: Search is a field at the top of the details popup (FR-051). It finds entries
  whose current text (FR-014) contains the words searched for, across all periods. Results
  show the entries' ink with their dates; opening one shows its period with that entry
  visible, or the period's hidden-entries list when its template doesn't show it.
- **FR-051**: A quiet (i) at the end of the header bar opens the details popup of the
  period shown. Under the search field it holds:
  - the template in use and how this period got it (chosen for this period, from a range,
    for the kind from a date onwards, or the kind's default);
  - what the layout doesn't show, per kind, and per slot for notes, each one tap from its
    hidden-entries list (FR-028).
- **FR-052**: When entries of the period are hidden, a small number beside the (i) gives
  their count; when none are, there is no number. The number is redrawn only when the count
  changes.
- **FR-053**: A cogwheel in the popup's head turns the popup, in its place, into the
  maintenance menu:
  - text mode for the whole view (FR-013);
  - choose template and plan ahead (FR-030, FR-033);
  - edit this template (FR-045, FR-049);
  - settings, including the global Neaten and Set in Autograph (FR-011);
  - debug, in developer builds only.

### Look

- **FR-060**: The planner draws in Interleaf in the voice the person chose for the system,
  and changes voice with it.
- **FR-061**: Nothing animates. Input is answered in place, a period change or template
  switch is one refresh, and a change inside a widget refreshes only that widget's area.
  Opening or closing the details popup, its cogwheel menu, a hidden-entries list or a
  widget's whole content refreshes only that rectangle. Ink is drawn with the pen's normal
  latency.
- **FR-062**: The planner tells the panel what each change on its page is, so the panel does
  not have to find out by watching. The answer to a tap on a control (a view, a period, a
  template, a popup opened or closed, text mode) is a new page: it is shown at once and exact,
  in one refresh. A step of an event moved or stretched with the finger, or of a widget dragged
  in the template editor, is motion: it is shown fast while the drag lasts, and the result of
  the drag brings the exact picture. What the writing changes says nothing, and a system
  whose panel takes no such advice shows the planner as before.

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| Writing regions (one region per handwriting field, its value, text mode; regions with reading off for drawn event blocks) | consumed | 1, draft | `GlassineOS/handwriting` spec 005 (GlassineOS/handwriting#122) |
| Pen and service interfaces | consumed | 1 | `GlassineOS/handwriting` spec 004 |
| Neatening and Autograph, the person's font | consumed | 1 | `GlassineOS/handwriting` spec 003 |
| Interleaf and the voices, the drawing library | consumed | current | `GlassineOS/ui` |
| System on-screen keyboard | consumed | — | `GlassineOS/shell`, `GlassineOS/wvkbd` |
| Ink protocol (ink areas, brushes; what a commit is, version 4) | consumed | glassine_ink_v1 | `GlassineOS/protocols` |
| The planner's entries as standard calendar objects, with ink, slots and template choices | owned | 1, draft | this spec (FR-020–FR-025a); the exact properties in the README |
| The system's calendar and contacts store | consumed | — | `GlassineOS/docs` decision 0017 |

## Device scope and risk

Device-neutral: any device with a pen and an e-ink panel and the handwriting service. No
device risk. User data: the planner keeps the person's entries and templates on the device;
FR-024 and FR-026–FR-028 protect them.

## Acceptance

Host level:

- switching through every shipped template of a kind and back, and editing and deleting
  templates, leaves the stored entries byte-for-byte as before, for a period holding every
  kind of entry and several slots;
- an entry shown by no widget is counted by the number beside the (i) (FR-052), named in
  the details popup and listed in ink from it (FR-028, FR-051), and reappears with a
  template that shows it;
- recorded pen input for a field gives a stored reading, and search finds it;
- a text-mode edit of one word shows that word in Autograph and every other word as ink;
- the layout gives every widget at least its minimum, in both orientations; content past it
  raises "+n more", done rows give way first, then the oldest open, and one empty row stays;
- a layout change requested while a stroke is in progress is held until the pause;
- dragging an event moves it in 30-minute steps, each step refreshing only the area the
  brackets leave and enter and the hour margin, which shows the time of the end that moves,
  and an end handle moves only that end;
- overlapping events split the width into equal lanes (first start first, at most three),
  each title's field in its own lane, the end handles of later lanes only over the gap and
  the serif, and a pen-down point belonging to one lane; while the pen is in a lane the
  rest of its group is grey;
- a long title in a short event grows its title field into the free time below its brackets
  without changing the event's times; a title that no longer fits shrinks evenly down to
  the smallest readable size, then is cut before the `]`;
- a strike-through from before an event's first letter to past its last opens the card that
  asks; one from inside the title, one that doesn't reach past it and a steep one don't;
- an event's card offers Open and Delete event; its page has a region each for title,
  place, notes and participants, the times' − and +, the Repeat and Remind choices, and
  Delete event that asks; place, notes, participants, repeat and reminder are kept as the
  event's calendar properties and survive a restart;
- a finger tap on a field opens its text mode and a tap outside leaves it, with no relayout;
- without writing regions the planner refuses to start with its message;
- offscreen renders of every shipped template in each voice.

Product level: in the images acceptance suite, write an event and a to-do by pen, switch the
day's template to one without a to-do list and back, and find the to-do by search.

Hardware: ink latency and refresh counts on a device, supervised.

## Out of scope

- The system's calendar and contacts store and its syncing (decision 0017).
- **Reminders and repeats**, their own spec later:
  - reminders and notifications;
  - repeat rules;
  - what happens to unfinished to-dos when their period ends.
- Sharing entries, templates or calendars with other people.
- Importing from or exporting to other calendars.
- Undated entries (an inbox or "someday" list).
- Time zones: times are the device's local wall-clock time.

## Key entities

- **Period**: a day, week or month; identified by its kind and its dates.
- **Entry**: an event, to-do, note (journal entry), priority or habit mark, a standard
  calendar object keyed by its date or period, its kind and, for free text, its slot (FR-020,
  FR-022).
- **Handwriting field**: the ink written into it, its reading, the person's text-mode edits
  and its display opt-ins (FR-011); attached to its entry.
- **Slot**: a name for a free-text place in a period, shared across templates.
- **Habit**: a named habit the habit tracker lists; its marks belong to days.
- **Template**: a named arrangement of widgets for one period kind in rows and columns, with
  weights, widget settings and, per part, a kind for each orientation.
- **Template choice**: which template a period, a range of periods or a kind from a date on
  uses; plus each kind's default.
- **Widget**: a reusable part of a view with the data it shows, its width (minimum,
  preferred, weight, whether it takes leftover space), its minimum height, and whether it
  compresses or truncates.

## Edge cases

- **Switching to a template that shows some data nowhere**: the entries stay stored (FR-027),
  the number beside the (i) counts them and the details popup names them per kind; a tap
  there lists them in ink (FR-028). Nothing asks the person to confirm, since nothing is
  lost.
- **A template edit removes the only widget showing a slot that has entries**: the edit is
  saved; every period using the template, past ones included, keeps its entries in that slot
  and names them in its details popup. Adding a widget for that slot name again, to this or
  any template, shows them in place. The editor may say how many periods hold entries in the
  slot; it doesn't block the edit.
- **A widget too small for its content**: a truncating widget shows what fits and "+n more",
  a compressing one scales down to its minimum; a tap on "+n more" opens all of it
  (FR-048). A template whose widgets can't all get their minimum, in either orientation,
  can't be saved (FR-044).
- **Writing into a field that's being resized**: the layout holds while the pen writes and
  settles after the pause (FR-046); ink moves with its field and is never scaled (FR-047).
  Resizing in the template editor applies to the template, not to entries.
- **Text mode on a whole view**: every handwriting field in the view becomes a text field;
  time and date widgets stay as they are. Ink with no reading (FR-009) stays visible and is
  not editable as text. The keyboard covers the lower part of the screen; the view is not
  laid out again, and fields under the keyboard are reached by scrolling the view.
- **A person without a letter model**: readings and search work as for anyone. Autograph
  exists from the start, plain until it has learned the person's letters, so edited words
  and Set in Autograph use it as it is and follow it as it grows. Neatening offers what the
  handwriting service can do without a letter model.
- **A period with no template chosen**: it uses the default of its kind (FR-034), and the
  details popup says so; the person can choose another from the maintenance menu.
- **No handwriting service**: the planner doesn't start and says why (FR-001).
- **An entry moved to another date**: its ink, reading and edits move with it (FR-021).
- **A title that no longer fits**: it can't overflow while written, since its field grows
  into the free time below. When its space shrinks later (the event is shortened, another
  event takes the free time below, a smaller view, or a typed title set in the person's
  font), it shrinks evenly to fit down to the smallest readable size, then is cut before
  the `]`; its card shows it whole.
- **Four or more events at the same time**: there are three lanes at most; a fourth shares
  the third lane.
- **A stroke in the gap between two lanes**: it belongs to no event, makes none and is not
  kept; in free time, where no event runs, a stroke writes a new event (FR-018).
- **A strike-through, then Keep**: the stroke goes and the title is as it was before it.
- **Erasing all of an event's title**: the event stays with its time and an empty title; it
  is removed only by an explicit delete.
- **A reading that fails** (the service is busy, a script it can't read): the ink is kept,
  the field has no text yet and is read again later; search can't find it until then.
- **A to-do of a week that spans two months**: it belongs to the week; month views show it
  only if their template asks for the weeks' to-dos.
- **Writing into the last empty row of a full list**: the layout holds until the pause; then
  a done row, or if none the oldest open one, gives way to "+n more", and a new empty row
  stays (FR-048).
- **Turning the device**: parts of the template that change kind with the orientation do so;
  entries and their ink are unchanged.
- **Two widgets in one template showing the same slot**: both show the same entry; writing in
  either changes it.

## Assumptions

- Writing regions are as handwriting spec 005 (GlassineOS/handwriting#122) describes: many
  regions per surface, each one field with its reading as its value, a text mode on the
  system keyboard, and a stored region handed back with its strokes and value shows them
  as before.
- Autograph, the person's font, always exists (handwriting spec 003); it starts plain and
  grows as the person's letters are learned.
- A new period with no template chosen gets its kind's default without asking.
- An event is created by drawing a line down an agenda's hour margin, by drawing a block
  over its time or writing into the agenda's free time, or with a date and time picker.
- Every image that has the planner has the system's calendar and contacts store.
- The planner runs in the person's own session; guests see none of its data.
- One person, one planner: there are no separate calendars to pick from in 001.
- The 30-minute step is the same for every agenda.
- A day agenda shows the hours its settings name; events outside them show as a count at
  its top or bottom edge.

## Success criteria

- **SC-001**: Writing a handwritten event or to-do takes no more steps than on paper plus
  setting its time with the widget: no confirmation, no conversion step.
- **SC-002**: Switching, editing, copying and deleting templates in every combination the
  tests cover loses and changes no entry.
- **SC-003**: An entry written by hand can be found by search within 5 seconds of the
  person's pause, for at least 95 % of entries whose reading matches what was written.
- **SC-004**: A period change or template switch costs one panel refresh; a change inside a
  widget refreshes only that widget's area; nothing on screen moves while the pen writes.
- **SC-005**: A person new to the planner can copy a template, add a widget and use the copy
  for next week in under 3 minutes without help.
- **SC-006**: In ink mode no converted text is ever shown, except words the person edited
  in text mode or chose to set in their font.
- **SC-007**: Every hidden entry of a period is reachable from its view in two taps: the
  (i), then its kind or slot.
