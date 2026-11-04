/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * The separator of the values of a list attribute (genres, links, actors) is not Akari's to choose: the attribute info of the type says it (the ontology: the slot has `is_list`, and a comma
 * unless it says another character), and Shoji, Akita and the other plugins read it from there. A semicolon that Akari wrote before did not split in Shoji: the genres were one entry.
 */

#include <MimeType.h>
#include <Message.h>

#include <string>
#include <vector>

#include <sen/SenAttrInfo.h>
#include <sen/SenList.h>

namespace movie {

/** What the attribute info of the type says for the attribute; a comma (the default of the ontology) if it says nothing. */
inline char
ListSeparatorOf(const char* mimeType, const char* attribute)
{
	BMessage info;
	BMimeType type(mimeType);
	if (type.InitCheck() == B_OK && type.GetAttrInfo(&info) == B_OK) {
		const sen::ListConfig config = sen::ListConfigOf(info, attribute);
		if (config.isList)
			return config.separator;
	}
	return ',';
}

/** The values of a list: split by the separator, and by the semicolon too, which an earlier version of Akari wrote (links are not split by it: they have no semicolon). */
inline std::vector<std::string>
SplitValues(const std::string& text, char separator)
{
	std::vector<std::string> values;
	for (const std::string& part : sen::list::Split(text, separator)) {
		if (separator != ';' && part.find(';') != std::string::npos) {
			for (const std::string& old : sen::list::Split(part, ';'))
				values.push_back(old);
		} else {
			values.push_back(part);
		}
	}
	return values;
}

}	// namespace movie
