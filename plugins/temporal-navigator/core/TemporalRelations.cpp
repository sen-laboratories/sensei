/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "TemporalRelations.h"

namespace temporal {

Relation
Compare(const Interval& a, const Interval& b)
{
	if (!a.valid || !b.valid)
		return Relation::kNone;
	// (the order matters for instants: two equal ones are equal, not meeting; an instant at the start of an interval meets it before it can be said to start it)
	if (a.start == b.start && a.end == b.end)
		return Relation::kEquals;
	if (a.end < b.start)
		return Relation::kBefore;
	if (b.end < a.start)
		return Relation::kAfter;
	if (a.end == b.start)
		return Relation::kMeets;
	if (b.end == a.start)
		return Relation::kMetBy;
	if (a.start == b.start)
		return a.end < b.end ? Relation::kStarts : Relation::kStartedBy;
	if (a.end == b.end)
		return a.start > b.start ? Relation::kFinishes : Relation::kFinishedBy;
	if (a.start > b.start && a.end < b.end)
		return Relation::kDuring;
	if (a.start < b.start && a.end > b.end)
		return Relation::kContains;
	return a.start < b.start ? Relation::kOverlaps : Relation::kOverlappedBy;
}


Relation
Inverse(Relation relation)
{
	switch (relation) {
		case Relation::kBefore: return Relation::kAfter;
		case Relation::kAfter: return Relation::kBefore;
		case Relation::kMeets: return Relation::kMetBy;
		case Relation::kMetBy: return Relation::kMeets;
		case Relation::kOverlaps: return Relation::kOverlappedBy;
		case Relation::kOverlappedBy: return Relation::kOverlaps;
		case Relation::kDuring: return Relation::kContains;
		case Relation::kContains: return Relation::kDuring;
		case Relation::kStarts: return Relation::kStartedBy;
		case Relation::kStartedBy: return Relation::kStarts;
		case Relation::kFinishes: return Relation::kFinishedBy;
		case Relation::kFinishedBy: return Relation::kFinishes;
		default: return relation;
	}
}


std::string
TypeOf(Relation relation)
{
	switch (relation) {
		case Relation::kBefore:
		case Relation::kAfter:
			return "relation/x-vnd.sen-labs.relation.temporal.before";
		case Relation::kMeets:
		case Relation::kMetBy:
			return "relation/x-vnd.sen-labs.relation.temporal.meets";
		case Relation::kOverlaps:
		case Relation::kOverlappedBy:
			return "relation/x-vnd.sen-labs.relation.temporal.overlaps";
		case Relation::kDuring:
		case Relation::kContains:
			return "relation/x-vnd.sen-labs.relation.temporal.during";
		case Relation::kEquals:
			return "relation/x-vnd.sen-labs.relation.temporal.equals";
		default:
			return std::string();
	}
}


std::string
LabelOf(Relation relation)
{
	switch (relation) {
		case Relation::kBefore: return "before";
		case Relation::kAfter: return "after";
		case Relation::kMeets: return "meets";
		case Relation::kMetBy: return "met by";
		case Relation::kOverlaps: return "overlaps";
		case Relation::kOverlappedBy: return "overlapped by";
		case Relation::kDuring: return "during";
		case Relation::kContains: return "contains";
		case Relation::kEquals: return "equals";
		case Relation::kStarts: return "starts";
		case Relation::kStartedBy: return "started by";
		case Relation::kFinishes: return "finishes";
		case Relation::kFinishedBy: return "finished by";
		default: return std::string();
	}
}


std::string
CodeOf(Relation relation)
{
	switch (relation) {
		case Relation::kBefore: return "b";
		case Relation::kAfter: return "bi";
		case Relation::kMeets: return "m";
		case Relation::kMetBy: return "mi";
		case Relation::kOverlaps: return "o";
		case Relation::kOverlappedBy: return "oi";
		case Relation::kDuring: return "d";
		case Relation::kContains: return "di";
		case Relation::kEquals: return "e";
		case Relation::kStarts: return "s";
		case Relation::kStartedBy: return "si";
		case Relation::kFinishes: return "f";
		case Relation::kFinishedBy: return "fi";
		default: return std::string();
	}
}


Interval
IntervalOf(const TimeAttributes& attributes, sen::edtf::LocalOffset localOffset)
{
	Interval interval;
	if (!attributes.hasStart)
		return interval;

	// the text beside a time is the time as it was said: it counts while it gives the very moment that the attribute holds
	sen::edtf::Value start, end;
	const bool fuzzyStart = !attributes.startEdtf.empty() && sen::edtf::Parse(attributes.startEdtf, &start, localOffset) && start.earliest == attributes.start;
	const bool fuzzyEnd = attributes.hasEnd && !attributes.endEdtf.empty() && sen::edtf::Parse(attributes.endEdtf, &end, localOffset) && end.latest == attributes.end;

	long long finish = attributes.start;
	long long seconds = 0;
	if (attributes.hasEnd)
		finish = fuzzyEnd ? end.EndMoment() : attributes.end;
	else if (!attributes.duration.empty() && sen::edtf::DurationSeconds(attributes.duration, &seconds))
		finish = attributes.start + seconds;
	else if (fuzzyStart && start.IsPeriod())
		finish = start.EndMoment();		// (a day without an end is the whole day)

	if (finish < attributes.start)
		return interval;
	interval.start = attributes.start;
	interval.end = finish;
	interval.valid = true;
	return interval;
}

}	// namespace temporal
