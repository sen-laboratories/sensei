/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "PaperAbstract.h"

namespace paperboy {

namespace {

/** an index that claims more positions than this is nonsense, not an abstract */
constexpr int kMaxPosition = 100000;

}	// namespace


std::string
ReconstructAbstract(const std::vector<InvertedWord>& index)
{
	int last = -1;
	for (const InvertedWord& entry : index) {
		for (int position : entry.second) {
			if (position > last && position <= kMaxPosition)
				last = position;
		}
	}
	if (last < 0)
		return "";

	std::vector<const std::string*> words(last + 1, nullptr);
	for (const InvertedWord& entry : index) {
		for (int position : entry.second) {
			if (position >= 0 && position <= last)
				words[position] = &entry.first;
		}
	}

	std::string text;
	for (const std::string* word : words) {
		if (word == nullptr)
			continue;
		if (!text.empty())
			text += ' ';
		text += *word;
	}
	return text;
}

}	// namespace paperboy
