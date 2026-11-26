/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperAffiliations.h"

#include <cctype>

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

}	// namespace


std::vector<Stay>
PlanAffiliations(const std::vector<Affiliation>& named, const std::vector<Employment>& employments)
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

	// where the person is now, if the paper does not say it
	for (const Employment& job : employments) {
		if (job.rorId.empty() || !job.until.empty())
			continue;
		bool named = false;
		for (const Stay& stay : stays)
			named = named || stay.rorId == job.rorId;
		if (named)
			continue;
		Stay stay;
		stay.rorId = job.rorId;
		stay.name = job.organization;
		stay.country = job.country;
		stay.city = job.city;
		stay.since = job.since;
		stay.role = job.role;
		stay.note = job.department;
		stay.fromEmployment = true;
		Add(&stays, stay);
	}
	return stays;
}

}	// namespace paperboy
