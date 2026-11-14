/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

#include <sen/SenEdtf.h>

/**
 * @file TemporalRelations.h
 * @brief What two things in time are to each other: Allen's interval algebra. Plain C++, no Haiku API.
 *
 * An INTERVAL is a start and an end in seconds, the end being where it ends and not its last second (an end of 2026-11-14 is the moment that 2026-11-15 begins, so the two days
 * MEET). An instant has the same start and end. What a file says of its time (a start, an end or a duration; the text of a time that is not exact) makes its interval; a time that is
 * not exact counts with its widest extent.
 *
 * The thirteen relations of the algebra are all told apart; the relation types of the ontology offer ten of them as five types with two directions each (before and after, meets and
 * met by, overlaps and overlapped by, during and contains, equals), the other four (starts, started by, finishes, finished by) are none of them yet. Between instants and intervals: an
 * instant at the beginning of an interval meets it, one inside it is during it.
 */

namespace temporal {

struct Interval {
	long long	start = 0;
	long long	end = 0;
	bool		valid = false;
};

enum class Relation {
	kNone,				///< no relation (one of the intervals is not valid)
	kBefore,			///< A ends before B begins, with a gap
	kAfter,
	kMeets,				///< A ends where B begins
	kMetBy,
	kOverlaps,			///< A begins first and ends inside B
	kOverlappedBy,
	kDuring,			///< A lies inside B, beginning after it and ending before it
	kContains,
	kEquals,			///< the same beginning and the same end
	kStarts,			///< the same beginning, A ends first
	kStartedBy,
	kFinishes,			///< the same end, A begins later
	kFinishedBy
};

/** the relation of A to B (A before B...); kNone if one of them is not valid */
Relation Compare(const Interval& a, const Interval& b);

/** the relation of B to A */
Relation Inverse(Relation relation);

/** the relation type that offers it (`relation/x-vnd.sen-labs.relation.temporal.between` for before and after...), empty for those that no type offers */
std::string TypeOf(Relation relation);

/** in words, as it is seen from A: `before`, `met by`, `overlapped by`... */
std::string LabelOf(Relation relation);

/** the code of Allen's notation: b, bi, m, mi, o, oi, d, di, e, s, si, f, fi */
std::string CodeOf(Relation relation);

/** What a file says of its time: the attributes of an event (`schema:startDate`, `schema:endDate`, `schema:duration`, and the text of a time that is not exact beside the times). */
struct TimeAttributes {
	bool		hasStart = false;
	long long	start = 0;
	std::string	startEdtf;
	bool		hasEnd = false;
	long long	end = 0;
	std::string	endEdtf;
	std::string	duration;		///< ISO 8601 (PT2H)
};

/**
 * The interval of a file. From the start, and the end, else the duration, else (a start that is a day, a month, a year) the whole period, else an instant. The text beside a time counts
 * while it still gives that very moment (someone who set the time without it makes it stale); @p localOffset reads a time of day in it as local time (see SenEdtf.h). Not valid without a
 * start, or when the end is before it.
 */
Interval IntervalOf(const TimeAttributes& attributes, sen::edtf::LocalOffset localOffset = nullptr);

}	// namespace temporal
