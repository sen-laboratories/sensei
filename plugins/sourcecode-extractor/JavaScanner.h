/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "LanguageScanner.h"

/**
 * @brief Java: `import a.b.C;`, `import a.b.*;` and `import static a.b.C.member;`.
 *
 * Comments, strings, text blocks and character literals are skipped; imports are read at the top level of the file (outside of any braces).
 * The reference is the name as written; for a static import it is the class (`a.b.C`, without the member), for `a.b.*` the package with the star
 * (which is no file and stays unresolved).
 *
 * A class `a.b.C` is looked up as `a/b/C.java` in the source roots: the folder that the `package` declaration of the file leads to (the file
 * `/p/src/com/x/Foo.java` with `package com.x;` has the root `/p/src`), and, for each folder up from the file, `src/main/java`, `src/test/java`, `src` and
 * the folder itself. What is found in the project is not `global`; the classes of the Java libraries are (there are no sources to find).
 */
class JavaScanner : public LanguageScanner {
public:
	const char*	Name() const;
	const std::vector<std::string>&	Extensions() const;
	const std::vector<std::string>&	SemanticTypes() const;
	void		Scan(const std::string& text, std::vector<Dependency>* dependencies) const;
	void		Resolve(Dependency* dependency, const std::string& sourcePath) const;
};
