/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>

/**
 * @brief Finds the #include directives of a source file with libclang.
 *
 * The file is only preprocessed (clang records the directives while it reads the file), so no compile command and no
 * project files are needed, and missing headers do not stop the scan: such an include is still found, without a resolved path.
 */
class IncludeScanner {
public:
	/**
	 * @param filePath the source file
	 * @param self     the includes are shown as relations inside the file: their line is added to the result
	 */
	IncludeScanner(const char* filePath, bool self = true);

	/**
	 * @brief Scan the file and put the includes in the reply, one item with a field per property and an entry per include:
	 *  - `_label`: the name as written in the directive (`sen/Sen.h`)
	 *  - `path`: where it was found, else the name as written
	 *  - `global`: true for `<name>`, false for `"name"`
	 *  - `line`: the line of the directive, only for self relations
	 * @return 0 if done, 1 if the file could not be parsed, 2 if it cannot be read
	 */
	int run(BMessage* reply);

private:
	const char*	fSourcePath;
	bool		fSelf;
};
