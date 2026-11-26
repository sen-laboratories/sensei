/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "PaperConfidence.h"

#include <sen/SenConfidence.h>

namespace paperboy {

namespace conf = sen::confidence;

double
AuthorshipConfidence(PersonMatch match)
{
	switch (match) {
		case PersonMatch::kByIdentifier:
			return conf::Estimate(conf::weight::kIdentifier);
		case PersonMatch::kCreated:
			return conf::Estimate(conf::weight::kStated);
		case PersonMatch::kByNameOnly:
			break;
	}
	return conf::Estimate(conf::weight::kNameOnly);
}


double
AffiliationConfidence(const Stay& stay)
{
	std::vector<double> evidence;
	if (stay.fromEmployment)
		evidence.push_back(conf::weight::kStated);
	if (stay.namedByPaper)
		evidence.push_back(conf::weight::kStated);
	// (two sources that agree: surer than either; they do not copy each other)
	return conf::Combine(evidence);
}


double
ProductionEvidence(const Stay& stay, int positions)
{
	std::vector<double> evidence;
	if (stay.namedByPaper)
		evidence.push_back(conf::weight::kStated);
	if (stay.heldAtPublication) {
		double weight = conf::weight::kUndated;
		if (!stay.since.empty() && !stay.until.empty())
			weight = conf::weight::kDatedOverlap;
		else if (!stay.since.empty())
			weight = conf::weight::kOpenOverlap;
		evidence.push_back(conf::Share(weight, positions));
	}
	return conf::Combine(evidence);
}


double
ProductionConfidence(const std::vector<double>& evidence)
{
	return conf::Combine(evidence, conf::kRepeatedSource);
}

}	// namespace paperboy
