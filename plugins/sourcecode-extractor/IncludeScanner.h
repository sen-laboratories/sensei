/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>

/**
 * @brief Finds the references of a source file to other files (`#include`, `import`) and puts them in the reply for the SEN server.
 *
 * The scanner of the language of the file does the work (LanguageScanner): the text is read, not compiled, so no compile command, no project
 * files and no compiler libraries are needed, and a reference to a file that does not exist is still found (without a resolved path).
 */
class IncludeScanner {
public:
	/**
	 * @param filePath the source file
	 * @param self     the references are shown as relations inside the file: their line is added to the result
	 */
	IncludeScanner(const char* filePath, bool self = true);

	/**
	 * @brief Scan the file and put the references in the reply, one item with a field per property and an entry per reference:
	 *  - `_label`: the name as written (`sen/Sen.h`)
	 *  - `path`: where it was found, else the name as written
	 *  - `global`: true for `<name>`, false for `"name"`
	 *  - `line`: the line of the reference, only for self relations
	 * @return 0 if done, 1 if the language of the file is not known, 2 if it cannot be read
	 */
	int run(BMessage* reply);

private:
	const char*	fSourcePath;
	bool		fSelf;
};
