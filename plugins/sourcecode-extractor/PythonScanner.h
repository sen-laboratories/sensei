/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "LanguageScanner.h"

/**
 * @brief Python: `import a.b`, `import a as b, c`, `from a.b import c, d`, `from . import x`, `from ..pkg import y`.
 *
 * Comments, strings (also triple quoted ones, so docstrings) and line continuations are skipped; the statements are found at the start of a
 * logical line, after a `;` and after the `:` of a one line compound statement (`if x: import y`). A reference is the module as written: `a.b` for
 * `import a.b` and `from a.b import c`, `.pkg` for `from .pkg import y`. `from . import x` names the modules `.x` (one reference for each name),
 * because the dots alone are the package, not a file that is imported.
 *
 * A module is looked up in the folder of the file, in the folder above the package that the file is in (the first one without `__init__.py`)
 * and in the library folders of Python as Haiku names them (the `python3*` folders and their `site-packages` in B_USER_NONPACKAGED_LIB_DIRECTORY,
 * B_USER_LIB_DIRECTORY, B_SYSTEM_NONPACKAGED_LIB_DIRECTORY and B_SYSTEM_LIB_DIRECTORY): `a/b.py` or `a/b/__init__.py` (and `.pyi` stubs).
 * Relative imports count from the folder of the file (one dot) and up (more dots). What is found in the project is not `global`;
 * relative imports never are.
 */
class PythonScanner : public LanguageScanner {
public:
	const char*	Name() const;
	const std::vector<FileKind>&	FileKinds() const;
	std::string	TypeFromContent(const std::string& text) const;
	void		Scan(const std::string& text, std::vector<Dependency>* dependencies) const;
	void		Resolve(Dependency* dependency, const std::string& sourcePath) const;

private:
	const std::vector<std::string>&	LibraryFolders() const;

	mutable	std::vector<std::string>	fLibraryFolders;
	mutable	bool						fLibraryFoldersKnown = false;
};
