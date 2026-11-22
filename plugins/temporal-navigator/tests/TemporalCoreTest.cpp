/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/** Tests of core/: Allen's relations between intervals, what a file's time attributes make of them. Plain C++, no Haiku API. */

#include <stdio.h>

#include <string>
#include <vector>

#include "TemporalRelations.h"

using namespace temporal;

static int sFailures = 0, sChecks = 0;
#define CHECK(condition) do { sChecks++; if (!(condition)) { sFailures++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static Interval I(long long start, long long end) { Interval interval; interval.start = start; interval.end = end; interval.valid = true; return interval; }

static void TestThirteenRelations()
{
	const Interval b = I(10, 20);
	struct { Interval a; Relation expected; } cases[] = {
		{I(0, 5), Relation::kBefore}, {I(25, 30), Relation::kAfter}, {I(5, 10), Relation::kMeets}, {I(20, 30), Relation::kMetBy},
		{I(5, 15), Relation::kOverlaps}, {I(15, 25), Relation::kOverlappedBy}, {I(12, 18), Relation::kDuring}, {I(5, 25), Relation::kContains},
		{I(10, 20), Relation::kEquals}, {I(10, 15), Relation::kStarts}, {I(10, 25), Relation::kStartedBy}, {I(15, 20), Relation::kFinishes}, {I(5, 20), Relation::kFinishedBy},
	};
	for (const auto& test : cases) {
		const Relation relation = Compare(test.a, b);
		CHECK(relation == test.expected);
		CHECK(Compare(b, test.a) == Inverse(relation));			// (seen from the other side it is the inverse)
		CHECK(Inverse(Inverse(relation)) == relation);
		CHECK(!LabelOf(relation).empty() && !CodeOf(relation).empty());
	}
	CHECK(Compare(Interval(), b) == Relation::kNone && Compare(b, Interval()) == Relation::kNone);
}

static void TestInstants()
{
	CHECK(Compare(I(5, 5), I(5, 5)) == Relation::kEquals);
	CHECK(Compare(I(5, 5), I(6, 6)) == Relation::kBefore && Compare(I(6, 6), I(5, 5)) == Relation::kAfter);
	CHECK(Compare(I(10, 10), I(10, 20)) == Relation::kMeets);			// (an instant at the beginning meets the interval)
	CHECK(Compare(I(20, 20), I(10, 20)) == Relation::kMetBy);			// (one at its end is met by it)
	CHECK(Compare(I(15, 15), I(10, 20)) == Relation::kDuring && Compare(I(10, 20), I(15, 15)) == Relation::kContains);
}

static void TestTypesAndDirections()
{
	// five types, two directions each: the label is that of the direction
	CHECK(TypeOf(Relation::kBefore) == TypeOf(Relation::kAfter) && TypeOf(Relation::kBefore).find("temporal.between") != std::string::npos);
	CHECK(TypeOf(Relation::kMeets) == TypeOf(Relation::kMetBy) && TypeOf(Relation::kOverlaps) == TypeOf(Relation::kOverlappedBy));
	CHECK(TypeOf(Relation::kDuring) == TypeOf(Relation::kContains) && TypeOf(Relation::kEquals).find("temporal.equals") != std::string::npos);
	CHECK(TypeOf(Relation::kStarts).empty() && TypeOf(Relation::kFinishedBy).empty() && TypeOf(Relation::kNone).empty());
	CHECK(LabelOf(Relation::kMetBy) == "met by" && LabelOf(Relation::kContains) == "contains" && CodeOf(Relation::kAfter) == "bi" && CodeOf(Relation::kContains) == "di");
}

static TimeAttributes Times(long long start, long long end) { TimeAttributes t; t.hasStart = true; t.start = start; t.hasEnd = true; t.end = end; return t; }

static void TestIntervalOfAFile()
{
	// a start and an end
	Interval a = IntervalOf(Times(1000, 4600));
	CHECK(a.valid && a.start == 1000 && a.end == 4600);
	// a start and a duration
	TimeAttributes withDuration; withDuration.hasStart = true; withDuration.start = 1000; withDuration.duration = "PT2H";
	a = IntervalOf(withDuration);
	CHECK(a.valid && a.end == 1000 + 7200);
	// a start alone is an instant; a duration that is none does not matter
	TimeAttributes alone; alone.hasStart = true; alone.start = 1000; alone.duration = "soon";
	a = IntervalOf(alone);
	CHECK(a.valid && a.start == 1000 && a.end == 1000);
	// no start: no interval; an end before the start: none
	TimeAttributes none; none.hasEnd = true; none.end = 5;
	CHECK(!IntervalOf(none).valid);
	CHECK(!IntervalOf(Times(2000, 1000)).valid);

	// a day without an end is the whole day; a day with an end that is a day is both days
	sen::edtf::Value day14, day15;
	sen::edtf::Parse("2026-11-14", &day14);
	sen::edtf::Parse("2026-11-15", &day15);
	TimeAttributes dayOnly; dayOnly.hasStart = true; dayOnly.start = day14.earliest; dayOnly.startEdtf = "2026-11-14";
	a = IntervalOf(dayOnly);
	CHECK(a.valid && a.start == day14.earliest && a.end == day15.earliest);
	TimeAttributes twoDays = dayOnly; twoDays.hasEnd = true; twoDays.end = day15.latest; twoDays.endEdtf = "2026-11-15";
	a = IntervalOf(twoDays);
	CHECK(a.valid && a.end == day15.latest + 1);
	// two days in a row meet
	TimeAttributes next; next.hasStart = true; next.start = day15.earliest; next.startEdtf = "2026-11-15";
	CHECK(Compare(IntervalOf(dayOnly), IntervalOf(next)) == Relation::kMeets);
	// a text that no longer gives the moment of the attribute is ignored (someone set the time without it)
	TimeAttributes stale = dayOnly; stale.start += 3600;
	a = IntervalOf(stale);
	CHECK(a.valid && a.start == stale.start && a.end == stale.start);
	// an approximate month has its widest extent
	sen::edtf::Value fuzzy;
	sen::edtf::Parse("1994-05~", &fuzzy);
	TimeAttributes month; month.hasStart = true; month.start = fuzzy.earliest; month.startEdtf = "1994-05~";
	a = IntervalOf(month);
	CHECK(a.valid && a.start == fuzzy.earliest && a.end == fuzzy.latest + 1);
}

static void TestAnAfternoon()
{
	// two meetings that collide, and one that follows: what the navigator is for
	const long long noon = 1794049200LL;
	const Interval first = IntervalOf(Times(noon, noon + 3600)), second = IntervalOf(Times(noon + 1800, noon + 5400)), third = IntervalOf(Times(noon + 5400, noon + 7200));
	CHECK(Compare(first, second) == Relation::kOverlaps && Compare(second, first) == Relation::kOverlappedBy);
	CHECK(Compare(second, third) == Relation::kMeets && Compare(first, third) == Relation::kBefore);
}

int main()
{
	TestThirteenRelations();
	TestInstants();
	TestTypesAndDirections();
	TestIntervalOfAFile();
	TestAnAfternoon();
	printf("%d checks, %d failures\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
