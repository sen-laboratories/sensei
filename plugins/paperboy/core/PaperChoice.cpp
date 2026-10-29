/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperChoice.h"

#include <algorithm>
#include <cctype>

namespace paperboy {

std::string
SourceOfDoi(const std::string& doi)
{
	if (doi.empty())
		return "";

	std::string registrant = doi.substr(0, doi.find('/'));
	std::transform(registrant.begin(), registrant.end(), registrant.begin(),
		[](unsigned char c) { return (char) std::tolower(c); });

	static const struct {
		const char*	registrant;
		const char*	name;
	} kSources[] = {
		{"10.48550", "arXiv"},
		{"10.1101", "bioRxiv/medRxiv"},
		{"10.5281", "Zenodo"},
		{"10.31219", "OSF Preprints"},
		{"10.2139", "SSRN"},
		{"10.21203", "Research Square"},
		{"10.26434", "ChemRxiv"},
		{"10.31234", "PsyArXiv"},
		{"10.20944", "Preprints.org"},
	};
	for (const auto& source : kSources) {
		if (registrant == source.registrant)
			return source.name;
	}
	return "Publisher";
}


double
ScoreHit(double similarity, int rank, int count, int year)
{
	const double like = std::max(0.0, std::min(1.0, similarity));
	const double ranked = count > 1 ? 1.0 - (double) std::max(0, rank) / (double) count : 1.0;
	return like * 0.85 + std::max(0.0, std::min(1.0, ranked)) * 0.10 + (year > 0 ? 0.05 : 0.0);
}

}	// namespace paperboy
