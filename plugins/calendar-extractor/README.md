<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# The SEN calendar extractor

Reads **calendar files**: invitations and entries of iCalendar (`.ics`, RFC 5545) and the old vCalendar 1.0 (`.vcs`), also those of Google, Outlook, Apple and Thunderbird. A file with **one event** is an *Event*: it gets
the semantic type of the ontology *SEN Temporal* and the attributes that the file says. A file with **several events** (an export, a subscription) is a *Calendar* that holds them.

| Feature | What it does |
|---|---|
| **identify** | One event: the semantic type `entity/x-vnd.sen-labs.entity.event`; several: `entity/x-vnd.sen-labs.entity.calendar` (`META:TYPE`) |
| **enrich** | Writes the attributes (below), names the file after the event and keeps the description in a Markdown file beside it. No network |
| **navigate** | Works out the relations of an event when they are opened (below) |

```sh
SenCalendarExtractor --describe invite.ics             # what would be written, nothing is changed
SenCalendarExtractor --enrich invite.ics               # the attributes, the name, the Markdown file
SenCalendarExtractor --identify *.ics
SenCalendarExtractor --library /tmp/lib --resolve relation/x-vnd.sen-labs.relation.event.attendee invite.ics
```
Use `--library <folder>` when you try a relation: the people and places that it finds are made there (else in the Library of the current context of SEN, where a test leaves files).

## The name of the file

The file is named after the event: its **date and time in the zone of this computer**, then its title: `2026-11-12_0930_Planning.ics`. A day has no time: `2026-12-24_Christmas.ics`. The description is in a file with the
same name: `2026-11-12_0930_Planning.md`. A name that is taken by another file gets `-2`. *Name the file after the event* in the settings switches the renaming off, and the **name pattern** (`rename_pattern`) changes the name: the tokens are `{date}` (`2026-11-12`), `{time}` (`0930`, nothing for a day: its separator goes with it), `{year}` `{month}` `{day}` `{hour}` `{minute}` and `{title}`; the default is `{date}_{time}_{title}`, and `{year}{month}{day} {title}` gives `20261112 Planning.ics`. A pattern with an unknown token or a `/` is refused; a `.md` that somebody else made, or changed, is never replaced.

## What is written

The attributes are those of the ontology (`SenOntoTemporal.h`): `dc:title`, `dc:description` (the first lines of the description, plain, for a column), `dc:identifier` (the UID), `schema:startDate` and `schema:endDate`
(**UTC**: the file manager shows them in the zone that you set, so a change of the zone needs nothing), `schema:duration` (only for an event that has no end), `schema:location`, `schema:organizer`, `schema:eventStatus`
(Confirmed, Tentative, Cancelled), `schema:eventAttendanceMode` (Offline, Online, Mixed), `schema:url`, `SEN:meetingUrl` and `SEN:meetingType`, `SEN:recurrence` (the rule as the file has it).

* **Times.** A time in UTC (`...Z`) or in a zone (`TZID=Europe/Vienna`) is converted once, with the rules of the zone that the file defines itself (`VTIMEZONE`, also the Windows names of Outlook) or those of the system. A **day** (all-day event) and a time
  **without a zone** mean the zone of this computer; their text is kept as well (`SEN:startDateEdtf`, `SEN:endDateEdtf`: `2026-12-24`), so that it can be worked out again if the zone changes. The end of an all-day event is the day after (as in the file).
* **Meetings.** The link of the online meeting is found in the conference properties (`CONFERENCE`, `X-GOOGLE-CONFERENCE`, the Teams properties of Outlook), the location and the description. The first link of a known service is `SEN:meetingUrl`, the service is
  `SEN:meetingType`: Zoom, Google Meet, Microsoft Teams, Webex, Jitsi, GoToMeeting, Whereby, Skype; any other service of a conference property is *Other*. An event with a meeting and no place is *Online*.
* **The description** is Markdown: the HTML of Outlook (`X-ALT-DESC`) or of Google (tags in the text) becomes links, bold, italic, lists and line breaks; plain text keeps its lines and gets its links.

## The relations of an event

They are **not stored**: the server asks the plugin when the relation is opened (*Open related*, group *Event*), so they are right at that moment. People and places are **entities**: they are made in the Library when the relation is opened (a person that
is there already is found by its address), and the folder of the relation is temporary until you keep it.

| Relation | Seen from the event | Seen from the other one | What it has |
|---|---|---|---|
| **Attendee** | attendee | attends | the *Role* (organizer, required, optional, chair, resource), the *Response* (Accepted, Declined, Tentative, Needs action, Delegated); the *Kind* is `you` for the person that you are (*Myself*) |
| **Description** | described in | describes | the Markdown file beside the event, with its name (when there is one) |
| **Takes place at** | takes place at | hosts | the place of the location text, with its coordinates if the file has them (it is enriched by Spaces) |
| **Series** | next | previous | the instance of a repeating event after and before this one: title, start, end and *Instance* (the RECURRENCE-ID); the rule, its exceptions, extra dates and moved instances of the file are taken into account |
| **Events** (of a calendar) | contains | part of | a **contained** relation (a self relation, as the outline of a PDF is): the calendar extractor reads the events from the file (feature *extract*) and the menu shows them as a list, in order of their start, each with title, start, end and identifier; a click opens the calendar file with its application (Shoji). The relation has `materialise_as` Event: **Add to Library** (and *Open related* on an item) makes the event a calendar file of its own in the Library (found again by its UID, so nothing is made twice; an event without a UID is known by a key made of what it says), enriched like a received event: the *materialise* request of the server (`sensei::cmd::kMaterialise`, command line `--materialise <key> --library <folder>`) |

The instances of a series and the events of a calendar are **places in the same file**, as the pages of a PDF are: the target is the calendar file, the instance is told by its properties. The response of an invitation is a property of the *relation*
between the event and the person, not of the person.

## Settings

In the settings window of SEN, under the calendar extractor (`~/config/settings/sen/calendar-extractor.settings`): *Name the file after the event*, its *Name pattern*, *Keep the description in a Markdown file*, and **Myself**: a field with a list of all the people that SEN knows
that follows what you type; other text is fine too (`Name <address>`). The attendee with that address (or, if you give only a name, that name) is the person that you are.

## What it forgives

Any line end, a byte order mark, folded lines, lower case names, components that are not closed or nested wrongly, lines that are none, quoted-printable text over several lines, text that is not UTF-8 (read as Windows-1252), the parameters of vCalendar 1.0
without a name, the old recurrence rules (`D1 #5`, `W2 TU TH #10`, `MP1 1+ MO #3`), zones that nobody knows (taken as the local time of the computer). What could not be read is left out, nothing makes it stop.

**Recurrence rules** that are worked out: daily to yearly with interval, count and until, the days of the week (also the first or last of a month), days of the month and year, months, week numbers, the position in the set, exceptions and extra dates. A rule that
is none of these gives no series and a line in the log.

## Tests

`tests/run.sh` builds and runs the tests of `core/` on any computer (plain C++): the dates and rules (with the examples of RFC 5545), the reader of the lines, the events of files in the style of Google, Outlook, Apple and Thunderbird, a vCalendar 1.0 file, calendars and
damaged files (`tests/fixtures`), the Markdown, the links and meetings, the names of the files and the settings. The plugin itself is tried in the VM (`--describe`, `--enrich`, `--resolve`) and through the server.

**Attributes before content.** The series of an event and its place are worked out from the attributes of the file as they are now (how it repeats, start, end, location), not from the calendar content that the file was made from: what the
user changed in Shoji or in the list of Akita counts, and an event that has no calendar content (made by hand) works too. The words of the field Repeats (every day, every weekday, every week, every month, every year, every 2 weeks) are
read as a rule; other words keep the rule of the content. **The attendees are the exception**: they are a structure of the calendar content (a self relation), so the relation Attendee is always read from the content, never from attributes
(which cannot tell whether the list was complete); an event without content has no attendees. See the developer guide of SEN.

