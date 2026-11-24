/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>

/**
 * @file PaperString.h
 * @brief The little that the readers of XML and HTML snippets share: entities, tags, blanks.
 */
namespace paperboy {

/** @brief Blanks and line breaks in a row become one blank, none at the ends (arXiv breaks the lines of titles and abstracts). */
std::string SqueezeBlanks(const std::string& text);

/** @brief The five entities of XML (`&amp;`...) and the numeric ones (`&#945;`, `&#x3b1;`) as UTF-8. */
std::string DecodeEntities(const std::string& text);

/** @brief The text of a snippet of HTML (`<a href="...">Name</a>`): the tags go, the entities are decoded, the blanks squeezed. */
std::string StripTags(const std::string& html);

/** @brief `https://ror.org/04d836q62`, `ror.org/04d836q62` or `04d836q62` is `04d836q62`; anything else that is not the id of the Research Organization Registry is empty. */
std::string BareRorId(const std::string& text);

}	// namespace paperboy
