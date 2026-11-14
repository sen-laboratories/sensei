<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# The SEN temporal navigator

Works out the **temporal relations between events** (and any other file that has a time) when you open them: *before* and *after*, *meets* and *met by*, *overlaps* and *overlapped by*,
*during* and *contains*, *equals*. Open the relation at an event (*Open related*, group *Temporal*): the files that it is related to in that way are the answer, each with the label of its
direction (`before` for what comes later, `after` for what came earlier), in the order of time.
The relation of *before* and *after* is called **Between** (one gap lies between the two); the files in its folder say *before* or *after*, as seen from the event that was opened.

The relations are **not stored**. The SEN server asks this plugin (`sensei::cmd::kResolve`, see `Sensei.h` of sento) when the relation is opened, so what you see is right at that moment, also
after you corrected a time. The relation types are those of the ontology *SEN Temporal* (`dynamic`); they are the relations of **Allen's interval algebra**, the names of W3C OWL-Time
(`time:intervalBefore` and so on):

| Relation | Seen from the event | Seen from the other one | Allen |
|---|---|---|---|
| between (before and after) | before | after | b, bi |
| meets | meets | met by | m, mi |
| overlaps | overlaps | overlapped by | o, oi |
| during | during | contains | d, di |
| equals | equals | equals | e |

*Starts*, *started by*, *finishes* and *finished by* (the same beginning or the same end) are told apart by the engine but offered by no relation type yet.

**The interval of a file** is made from its attributes (`schema:startDate`, `schema:endDate`, `schema:duration`, see the ontology and `sento/src/cpp/include/SenTimeAttributes.h`):
the start and the end; else the start and the duration; else, for a start that is a day, a month or a year, the whole period; else an instant. A file without a start has no temporal relations. The
end of a period is the moment that the next one begins, so two days in a row **meet**. A time that is not exact (`1994-05~`, `202X`, in the Extended Date/Time Format, see `SenEdtf.h`) counts
with its **widest extent**, so that a relation is never claimed that the time does not allow. Times of day without a zone are the local time of the person who typed them.

The files are found with a query of the index of `schema:startDate` on every volume (BFS indexes a time as the 64-bit integer that holds it, so the times must be written as `time_t`); those in
the Trash are left out. Any file that has a start is found, whatever its type; the offer of the relation ("Open related") is for events for now: the relation types name the type *Event* as
their source and target.

From the command line (for a test): `SenTemporalNavigator <relation type> <file>`, e.g. `SenTemporalNavigator relation/x-vnd.sen-labs.relation.temporal.overlaps ~/Events/Meeting`.

The tests of the core (Allen's relations, the interval of a file) run everywhere: `sh tests/run.sh`.

Icon: "Calendar" by zuMi (MIT license), the icon of the Event, too (`images/calendar-zumi.svg` is its SVG). The folder of the relations is `images/temporal-relation.svg`.
