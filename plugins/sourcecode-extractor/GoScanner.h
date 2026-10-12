/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "LanguageScanner.h"

/**
 * @brief Go: `import "fmt"`, `import f "fmt"`, `import . "x"`, `import _ "x"` and the groups `import ( ... )`.
 *
 * Comments, strings, raw strings and runes are skipped; the reference is the import path as written, on the line where it is written. An import names a
 * package, which in Go is a folder with `.go` files, so a package that is found is resolved to its FOLDER.
 *
 * Packages are looked up in the module of the project (the `module` line of the `go.mod` above the file: an import path that starts with the module
 * path leads to the folder in the module), in its `vendor` folder, in the folders `go/src` below GOROOT and the library and home folders of Haiku
 * (B_USER_DIRECTORY, B_USER_LIB_DIRECTORY, B_SYSTEM_LIB_DIRECTORY and the non-packaged ones). Relative imports (`./x`) count from the folder of the file.
 * What is found in the module is not `global`.
 */
class GoScanner : public LanguageScanner {
public:
	const char*	Name() const;
	const std::vector<FileKind>&	FileKinds() const;
	void		Scan(const std::string& text, std::vector<Dependency>* dependencies) const;
	void		Resolve(Dependency* dependency, const std::string& sourcePath) const;

private:
	const std::vector<std::string>&	LibraryFolders() const;

	mutable	std::vector<std::string>	fLibraryFolders;
	mutable	bool						fLibraryFoldersKnown = false;
};
