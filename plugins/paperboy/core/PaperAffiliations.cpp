/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperAffiliations.h"

#include <cctype>

#include "PaperDate.h"

namespace paperboy {

std::string
InstitutionTypeText(const std::string& openAlexType)
{
	std::string text = openAlexType;
	if (text.empty())
		return text;
	text[0] = (char) std::toupper((unsigned char) text[0]);
	for (size_t i = 1; i < text.size(); i++)
		text[i] = (char) std::tolower((unsigned char) text[i]);
	return text;
}


namespace {

bool
Has(const std::vector<Stay>& stays, const Stay& wanted)
{
	for (const Stay& stay : stays) {
		if (stay.rorId == wanted.rorId && stay.since == wanted.since && stay.until == wanted.until && stay.role == wanted.role && stay.note == wanted.note)
			return true;
	}
	return false;
}

void
Add(std::vector<Stay>* stays, const Stay& stay)
{
	if (!Has(*stays, stay))
		stays->push_back(stay);
}

/**
 * Did the employment cover the day of the paper? One that goes on without a start is taken as covering (the person is there now, and nothing says the contrary); one with a start
 * must have begun by the end of the period that the date names, and one that is over must not be over before its beginning (a year is the whole year: the benefit of the doubt).
 */
bool
CoversPaper(const Employment& job, const std::string& paperDate)
{
	if (paperDate.empty())
		return false;
	const time_t first = ParseDate(paperDate);
	const time_t last = ParseDateEnd(paperDate);
	if (first < 0 || last < 0)
		return false;
	if (job.since.empty()) {
		if (!job.until.empty())
			return false;
	} else {
		const time_t begin = ParseDate(job.since);
		if (begin < 0 || begin > last)
			return false;
	}
	if (!job.until.empty()) {
		const time_t end = ParseDateEnd(job.until);
		if (end < 0 || end < first)
			return false;
	}
	return true;
}

}	// namespace


std::vector<Stay>
PlanAffiliations(const std::vector<Affiliation>& named, const std::vector<Employment>& employments, const std::string& paperDate)
{
	std::vector<Stay> stays;

	for (const Affiliation& place : named) {
		if (!place.HasRor())
			continue;
		Stay base;
		base.rorId = place.rorId;
		base.name = place.name;
		base.country = place.country;
		base.type = InstitutionTypeText(place.type);
		base.namedByPaper = true;

		bool dated = false;
		for (const Employment& job : employments) {
			if (job.rorId != place.rorId)
				continue;
			Stay stay = base;
			stay.since = job.since;
			stay.until = job.until;
			stay.role = job.role;
			stay.note = job.department;
			stay.city = job.city;
			stay.fromEmployment = true;
			Add(&stays, stay);
			dated = true;
		}
		if (!dated)
			Add(&stays, base);
	}

	// where the person is now, and where the person was when the paper was written, if the paper does not say it
	for (const Employment& job : employments) {
		if (job.rorId.empty())
			continue;
		const bool covers = CoversPaper(job, paperDate);
		if (!job.until.empty() && !covers)
			continue;
		bool paperNamesIt = false;
		for (const Affiliation& place : named)
			paperNamesIt = paperNamesIt || (place.HasRor() && place.rorId == job.rorId);
		if (paperNamesIt)
			continue;
		Stay stay;
		stay.rorId = job.rorId;
		stay.name = job.organization;
		stay.country = job.country;
		stay.city = job.city;
		stay.since = job.since;
		stay.until = job.until;
		stay.role = job.role;
		stay.note = job.department;
		stay.fromEmployment = true;
		stay.heldAtPublication = covers;
		Add(&stays, stay);
	}
	return stays;
}

}	// namespace paperboy
