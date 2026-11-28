# Feature Specification: Synced calendars — outside entries, repeats and time zones

**Feature Branch**: `spec-002-synced-calendars`
**Status**: Draft
**Input**: "The planner shows the person's synced data the way people expect from a calendar
app: every enabled calendar, outside and read-only calendars, repeating events and changes to
one occurrence, time zones, and new entries going to the system's default for their kind."
**Builds on**: [001 the planner](../001-planner/spec.md); GlassineOS/pim spec 001, calendars
and contacts (the store, its collections and defaults; "pim 001" below); GlassineOS/shell spec
014 (accounts are set up in Settings); GlassineOS/docs decision 0017; GlassineOS/handwriting
spec 007 (another hand for text from elsewhere; "handwriting 007" below).

## Context

The planner's entries live in the person's store (001 FR-020), which can hold several
calendars: the one on this device and any synced from outside servers, set up in Settings. So
the planner shows entries it didn't write: an appointment added on a phone, a shared team
calendar, a holiday calendar that can't be changed, events that repeat every week and events
made in another time zone. A person who connects their calendar expects the planner to show
these the way any calendar app does, and to change them without surprises: a moved occurrence
of a weekly meeting asks whether only this one moves, a read-only event can't be scribbled
into, and a 9:00 call made in another city's time shows at the right hour here.

This spec says how the planner shows and changes such entries. Syncing, conflicts and the
accounts themselves are pim 001's and Settings'; the planner has no account UI (001 FR-025).

Terms are 001's and pim 001's (collection, default collection, read-only, account). Also:

| Term | Meaning |
|---|---|
| calendar | a collection of the store that holds events, to-dos or journal entries |
| outside entry | an entry the planner didn't write: it has the standard text but none of the planner's ink, slot or template data |
| repeating event | an event with a repeat rule; each of its days is an **occurrence** |
| series | a repeating event with all its occurrences |
| exception | an occurrence changed on its own (another time, title or notes) or removed from its series |
| scope | which occurrences a change to a repeating event applies to: this event, this and following events, or all events |
| floating time | a time with no time zone, the same clock time wherever the device is |

## User scenarios

### Scenario 1 – A phone's appointment shows in the planner (Priority: P1)

**Given** a calendar synced from a server, **When** the person adds "team call 10:00–10:30"
there on their phone, **Then** after the next sync the planner's day agenda shows it as a pair
of brackets with its title typed in the voice's text face, refreshing only that widget, and
search finds it. **When** the person writes "bring notes" with the pen after its title, **Then**
the title is "team call bring notes", the typed words stay typed and the new words are ink, and
the phone shows the new title after the next sync.

### Scenario 2 – Move one occurrence of a weekly meeting (Priority: P1)

**Given** a weekly event on Mondays 9:00–10:00, **When** the person drags this Monday's
occurrence to 11:00 and releases it, **Then** a card asks "Change which events?" with This
event, This and following events and All events, and the event sits dashed at 11:00 until the
answer. **When** they tap This event, **Then** only this Monday is at 11:00; the other Mondays
stay at 9:00. **When** they tap outside the card instead, **Then** the occurrence goes back to
9:00 and nothing changes.

### Scenario 3 – Delete one occurrence (Priority: P1)

**Given** the same weekly event, **When** the person strikes this Monday's title through,
**Then** the delete card asks "Delete this event?" with Keep, and Delete as This event, This
and following events or All events. **When** they tap This event, **Then** this Monday is free
and the next Monday still shows the meeting.

### Scenario 4 – Write into a repeating event's title (Priority: P2)

**Given** the weekly event, **When** the person writes "room 3" into this Monday's title and
pauses, **Then** the ink shows at once, and a small card under the title asks "Change which
events?" with This event and All events. **When** they tap All events, **Then** every Monday's
title reads "… room 3" with the same ink. **When** they tap outside it instead, **Then** only this
Monday's title changes.

### Scenario 5 – A holiday calendar (Priority: P1)

**Given** a read-only holiday calendar, **When** a holiday falls on a Friday, **Then** the
Friday shows it as an all-day event, typed. **When** the person draws a line down the hour
margin there, writes into its title, drags it or strikes it through, **Then** nothing changes on
the holiday; a stroke over its title is not kept, and the hour-margin line makes a new event in
the default calendar. **When** they tap it, **Then** its card shows the title, the day, "Read-only
· Holidays" and Open; its page can't be changed.

### Scenario 6 – An event made in another time zone (Priority: P1)

**Given** a device set to Berlin time, **When** a colleague in New York makes a call at 9:00 New
York time in a shared calendar, **Then** the planner shows it at 15:00, and its page says
"15:00–16:00 (9:00–10:00 New York time)". **When** the person travels and the device's time zone
changes to London, **Then** the call shows at 14:00, and an event the person wrote in Berlin at
14:00 shows at 13:00; an all-day event stays on its day.

### Scenario 7 – Every enabled calendar, and where new entries go (Priority: P1)

**Given** a calendar on this device and two synced calendars, all turned on in Settings, **When**
the person opens a week, **Then** it shows the entries of all three. **When** they write a new
event, **Then** it goes to the default calendar for events (set in Settings), and its card names
that calendar. **When** they open its page and pick another calendar under Calendar, **Then** the
event moves there with its ink.

### Scenario 8 – Hide a calendar in the planner (Priority: P2)

**Given** a shared work calendar the person doesn't want in their planner, **When** they turn off
its Show switch in the planner's settings, **Then** its entries leave every view in one refresh,
are not counted as hidden entries, and are still synced and shown in other apps.

### Scenario 9 – An entry deleted elsewhere while open (Priority: P3)

**Given** an event's page open in the planner, **When** the event is deleted on another device and
the deletion arrives, **Then** the page says "This event was deleted elsewhere." with Keep it (it is
written again, as it is on the page) and Close.

### Scenario 10 – Outside entries in another hand (Priority: P3)

**Given** the person picked Another hand for outside entries in the planner's settings, **When**
"team call" arrives from their phone, **Then** the day agenda shows its title written in a neat,
upright hand that is plainly not theirs, where their own events are their ink. **When** they write
"bring notes" after it with the pen, **Then** the new words are their ink beside it. **When** they
change "call" to "meeting" in text mode, **Then** "meeting" is shown as the person's edited words
are (001 FR-014) and "team" stays in the other hand. **When** they pick Typed again, **Then** the
outside words are typed in the voice's text face in one refresh of each view.

**Given** the handwriting generator isn't installed, **When** the person opens the planner's
settings, **Then** Another hand is shown as unavailable with one sentence why, and outside entries
are typed.

## Requirements

### Calendars

- **FR-001**: The planner shows the entries of every calendar of the person's store that is
  turned on (pim 001), on this device and synced alike, except calendars the person hid in the
  planner (FR-004).
- **FR-002**: A new entry goes to the store's default collection for its kind (pim 001 FR-011):
  events to the default for events; to-dos and priorities to the default for to-dos; notes and
  habit marks to the default for journal entries.
- **FR-003**: An event's card names its calendar when the person has more than one calendar.
  Its page has a Calendar row listing the writable calendars that take its kind; picking another
  moves the entry there with its ink, its reading and its edits. A repeating event moves as a
  whole series.
- **FR-004**: The planner's settings (001 FR-053) list the calendars with a Show switch each, all
  on at first, and one line saying that calendars are added in Settings, which opens Settings'
  accounts page. Hiding a calendar changes only the planner's views: its entries are still in the
  store, synced and shown by other apps, and are not counted or listed as hidden entries (001
  FR-028). Showing it again brings them back in one refresh.
- **FR-005**: Views don't mark entries by calendar beyond FR-003: no colours or patterns.

### Outside entries

- **FR-010**: An outside entry is shown in the field its kind uses, its text typed in the voice's
  text face, or written in another hand when the person chose it (FR-016). The person can write into it with the pen and edit it in text mode as any field (001
  FR-012, FR-014, FR-015): typed words stay typed, new words are ink.
- **FR-011**: Outside events show as 001 says for timed and all-day events (001 FR-018, FR-019).
  Their place and notes are shown on the event's page in Where and Notes; their participants as
  their names, typed, in With. Properties the planner doesn't show (links, attachments, more than
  one reminder, a status) are kept unchanged (001 FR-025a) and listed on the page as "Also:" with
  their names, read-only.
- **FR-011a**: An outside event marked cancelled shows in its place with its title struck
  through in a light line and takes its lane as before; its card says "Cancelled". It can be
  deleted like any event.
- **FR-012**: Outside to-dos show as to-dos of the day they are due; done or not as they are
  stored. Outside to-dos without a date, and outside journal entries that aren't of a day, stay in
  the store unchanged and are shown by no view; they are not hidden entries.
- **FR-013**: An outside journal entry of a day without a slot belongs to the slot "Notes" (001
  FR-022): a free-notes widget with that slot shows it, otherwise it counts as a hidden entry.
- **FR-014**: An entry that arrives or changes while the pen writes waits until the pause and
  then shows, refreshing only the widgets it is in (001 FR-046, FR-061). A change to a field's text
  follows 001 FR-025a.
- **FR-015**: When an entry whose card or page is open is changed elsewhere, the card or page shows
  the new version at once; when it is deleted elsewhere, the page says so with Keep it and Close
  (Scenario 9), and a card closes.
- **FR-016**: **Another hand.** The planner's settings (001 FR-053) have an "Outside entries" row
  with Typed (the default) and Another hand. With Another hand, the planner hands each outside
  field's text to its writing region as text from elsewhere (handwriting 007 FR-026), and its words
  are shown written in the device's other hand (handwriting 007 FR-027), the same hand for every
  calendar (FR-005: no marking by calendar). Another hand is offered only while the system announces
  it available (handwriting 007 FR-004); otherwise the row says in one sentence why it isn't
  ("Another hand needs the handwriting generator, which isn't installed.") and outside entries are
  typed. When it stops being available, outside entries are typed from the next drawing of each view.
- **FR-017**: Only the outside text is in the other hand: a word the person changes or adds in text
  mode is theirs from then on and shown as 001 FR-014 says; ink they write is their ink; a word keeps
  the other hand until the person changes it, also after the planner is closed and opened. Set in
  Autograph (001 FR-011) changes only the person's read ink, never the outside words. Read-only and
  cancelled outside entries (FR-020, FR-011a) are shown in the other hand the same way.
- **FR-018**: Outside text is written in the other hand once, when it arrives or changes, and kept
  (handwriting 007 FR-030): drawing, scrolling and opening a period never make it again. Text not
  ready when its widget is drawn is shown typed and replaced at most once, in one refresh of only
  that widget, never while the pen writes (FR-014). The entry's text, search and what other apps see
  are the same with either choice.

### Read-only calendars

- **FR-020**: An entry of a read-only calendar is shown like any other but can't be written into,
  moved, stretched, struck through, ticked or deleted: a pen stroke over it is not kept, a finger
  drag doesn't move it, and a tap opens its card with "Read-only" and the calendar's name and only
  Open; its page shows everything and changes nothing. The hour margin and free time beside it work
  as always, and a new event drawn over its time goes to the default calendar (FR-002).

### Repeating events

- **FR-030**: A repeating event, from any calendar, shows on every day it falls on, in every view,
  with each occurrence's own changes (exceptions) and without the occurrences removed from it. All
  repeat rules of the standard calendar format are followed, not only the planner's own choices.
- **FR-031**: An occurrence is a full event in the views: it can be moved, stretched, written into,
  struck through and opened like any event (001 FR-018).
- **FR-032**: **Scope.** A change to an occurrence of a repeating event asks which events it applies
  to, with a card under the event:
  - moving or stretching it, changing its day or times on its page, and deleting it: This event,
    This and following events, All events;
  - writing into or editing its title, notes, place or participants: This event, All events.

  The card opens once per change: after the drag's release, after the pause that ends the writing
  (001 FR-046), on the delete card (001 FR-018), or when the person leaves the event's page. While
  it is open, the occurrence shows the change, dashed for a time change. Tapping outside the card
  keeps a time change from happening (the occurrence goes back) and applies a text change to this
  event only. An occurrence that is already an exception asks the same.

  The card never holds up the pen: it opens only after the pause, the pen may go on writing in
  the field while it is open, and the answer applies to all the writing in that field until the
  card is answered or the person leaves the field. Writing a new entry never asks (001 SC-001);
  only changes to an existing repeating event do.
- **FR-033**: This event makes or changes this occurrence's exception; This and following events
  ends the series before this occurrence and starts a new one from it with the change; All events
  changes the series and keeps the exceptions' own changes to other properties. Deleting This event
  removes only this occurrence.
- **FR-034**: The event page's Repeat shows the planner's choices (001 FR-018) when the rule is one
  of them, otherwise "Custom (set elsewhere)" with the rule in words ("every second Tuesday"). Picking
  another choice replaces the rule for the scope the person picks, after asking. Never is offered too.
- **FR-035**: The handwriting of a series' title belongs to the series; an occurrence whose title was
  changed for This event only has its own ink.
- **FR-036**: Repeating to-dos show the open occurrence on its due day; ticking it is done for that
  occurrence only, and the next one shows on its day.
- **FR-037**: Search (001 FR-050) lists a repeating event once, with the date of its next
  occurrence from today (else its last); an exception whose text differs is listed on its own.
  Opening a result shows that occurrence's period.

### Time zones

- **FR-040**: Every time is shown in the device's local time zone. An event with a time zone keeps
  its moment: when the device's zone changes, it moves on the agenda. An event with a floating time
  keeps its clock time. All-day events keep their days in every zone.
- **FR-041**: The planner writes new timed events, and changes the times of existing ones, in the
  device's time zone, so other devices show them at the same moment. An outside event keeps its own
  zone when its times are changed in the planner.
- **FR-042**: An event whose zone differs from the device's says so on its page, as the times in the
  event's zone after the local ones (Scenario 6); its card shows the local times only.
- **FR-043**: Changes of daylight saving time follow the event's zone: a weekly 9:00 event stays at
  9:00 in its zone across the change, and its local time follows.
- **FR-044**: An event whose local times cross midnight shows on each day it covers (001 FR-019).

## Contracts

| Contract | Owned / consumed | Version | Spec location |
|---|---|---|---|
| The store's collections, defaults, read-only flag and change notification | consumed | 1, draft | GlassineOS/pim spec 001 (FR-001–FR-013) |
| The planner's entries as standard calendar objects, with ink, slots and template choices; exceptions and time zones as standard properties | owned | 1, draft | 001 (FR-020–FR-025a); FR-030–FR-043 here; the exact properties in the README |
| iCalendar repeat rules, exceptions and time zones (RFC 5545) | consumed | — | — |
| Settings' accounts page, opened by its page argument | consumed | 1 | GlassineOS/shell spec 014 FR-001 |
| Interleaf and the voices | consumed | current | `GlassineOS/ui` |
| Text from elsewhere in another hand, and its availability | consumed | draft | `GlassineOS/handwriting` spec 007 (FR-004, FR-026, FR-027, FR-030) |

## Device scope and risk

Device-neutral. User data: the planner now changes entries other apps and other devices share;
FR-032 asks before a change reaches other occurrences, FR-020 keeps read-only calendars unchanged,
and FR-011 keeps what the planner doesn't show. A wrong scope or a lost exception would change the
person's appointments on every synced device; the host tests below cover each scope.

## Acceptance

Host level, against a store with several calendars (a stand-in, or pim 001's test server):

- entries of every turned-on calendar show; a hidden calendar's don't and aren't counted as hidden;
  new entries of each kind land in the default for their kind; moving an event to another calendar
  keeps its ink;
- an outside event, to-do and journal entry from a recorded set (made by other calendar apps) show
  typed, with "Also:" properties kept byte for byte after the planner edits the title;
- read-only entries: a stroke, a drag, a strike-through and a tick change nothing; the hour margin
  beside them makes an event in the default calendar;
- repeating events: daily, weekly on several days, monthly by weekday, yearly, with counts, ends,
  removed occurrences and exceptions, shown on the right days over two years; each scope of FR-032
  for a move, a delete and a title change gives the stored series and exceptions expected, checked
  against the standard format; tapping outside the card;
- time zones: an event in another zone, a floating one and an all-day one, before and after the
  device's zone changes and across a daylight saving change, each at the expected local time;
- an outside change arriving during a stroke shows only after the pause; a deletion while the page is
  open shows the notice;
- with Another hand and a stand-in generator: outside words come in the other hand, the person's
  text-mode edits and ink as with Typed, the mark kept through closing and opening; with the
  generator unavailable, the row's sentence and typed outside words; the entry's stored text is byte
  for byte the same with either choice;
- offscreen renders of the scope cards, the read-only card, the event page with Calendar, "Also:" and
  another zone, and the planner's calendar settings, in each voice.

Product level: in the images acceptance suite, a weekly event made in GNOME Calendar shows in the
planner; moving one occurrence there shows the change in GNOME Calendar for that day only.

Hardware: refresh counts when a sync brings changes into an open week, supervised.

## Out of scope

- Accounts, syncing and conflicts (pim 001, shell 014).
- Invitations: answering them, attendee status, sending to participants.
- Choosing a time zone for an event in the planner, and showing a second zone in views.
- Reminders and notifications (their own spec).
- Undated to-dos (001 Out of scope).
- Marking calendars by colour or pattern in views.

## Key entities

- **Calendar (as the planner sees it)**: a store collection, its name, read-only or not, shown or
  hidden in the planner (FR-001, FR-004).
- **Outside entry**: an entry with standard text and no planner data (FR-010–FR-013).
- **Series, occurrence, exception**: a repeating event, one of its days, and an occurrence changed or
  removed on its own (FR-030–FR-036).
- **Scope choice**: which occurrences a change applies to (FR-032).
- **Event time zone**: the zone an event's times are in, or none for floating times (FR-040–FR-043).

## Edge cases

- **A repeating event that started long ago**: its occurrences show from its start, whenever the
  person looks.
- **An exception moved to another day**: it shows on its new day, not its old one.
- **This and following events on the first occurrence**: it is the same as All events, and the card
  says so by offering only This event and All events.
- **A rule the planner can't show**: Repeat shows it as custom in words (FR-034); the occurrences show
  exactly.
- **A repeating event in a read-only calendar**: no card asks; nothing can be changed (FR-020).
- **A sync brings a change to a series while a scope card is open**: the card stays; the person's
  choice applies to the series as it is then.
- **The default calendar doesn't take notes**: pim 001 keeps notes on this device; the planner
  writes them there.
- **An outside event with a time in a zone that doesn't exist** (a skipped hour at a daylight saving
  change): it shows at the next valid time.
- **An outside event longer than the agenda's hours**: shown as 001's Assumptions say for events
  outside the agenda's hours.
- **The same appointment in two calendars**: two events, side by side in lanes; nothing merges them.
- **A title of mixed origin** (outside words, the person's ink and the person's typed edits): each
  part is shown as its own (FR-017), in reading order on the title's line.
- **The handwriting generator is removed with Another hand chosen**: outside entries are typed from
  the next drawing of each view; the choice stays and applies again when it comes back.

## Assumptions

- Calendars are added, turned on and off, and their defaults set in Settings (shell 014).
- The store keeps every standard property and every `X-` property of an entry (pim 001 FR-005).
- The device knows its time zone (001 FR-004 follows the system's settings).
- A series of a few years shows without delay on the slowest supported device.

## Success criteria

- **SC-001**: Every entry from a recorded set of other calendar apps' calendars (repeating, with
  exceptions, in other zones, all-day, read-only) shows on the same days and local times as GNOME
  Calendar shows them.
- **SC-002**: No change in the planner reaches occurrences the person didn't choose (host tests of
  every scope).
- **SC-003**: No read-only entry and no property the planner doesn't show is ever changed by the
  planner (host test, byte for byte).
- **SC-004**: An outside change shows in an open view after the next sync in one refresh of the
  widgets it is in, never while the pen writes.

## Design boards

Needed in GlassineOS/ui `designs/planner` before implementation: the scope card after a drag, after
writing, and on the delete card (three and two choices); the read-only card and page; an outside
event typed in the day agenda next to a handwritten one; the same in another hand, with the person's
ink and an edited word in its title; the planner's settings with the "Outside entries" row, Another
hand available and unavailable; an outside event's page with Calendar,
"Also:" and the other zone's times; the "deleted elsewhere" notice; the planner's settings with the
calendar list and the line to Settings. Each in portrait and landscape.
