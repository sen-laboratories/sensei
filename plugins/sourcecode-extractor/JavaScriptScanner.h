/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "LanguageScanner.h"

/**
 * @brief JavaScript and TypeScript (also JSX and TSX): `import ... from 'x'`, `import 'x'`, `import x = require('x')`, `export ... from 'x'`,
 * `require('x')`, dynamic `import('x')` and the TypeScript directives `/// <reference path="x" />` and `types="x"`.
 *
 * Comments, strings, template literals (with nested `${ }`) and regular expression literals are skipped (a `/` is a regular expression when it
 * cannot be a division after the token before it). A reference is the specifier as written; relative ones (`./x`, `../x`, `/x`) belong to the
 * project, the others are packages (global).
 *
 * Files are looked up like Node and TypeScript do it: a relative specifier as it is, with the extensions of the language (`.ts`, `.tsx`, `.d.ts`,
 * `.js`, `.jsx`, `.mjs`, `.cjs`, `.json`; TypeScript first for a TypeScript file), `x.js` for `x.ts` (TypeScript imports name the compiled file), then as a folder
 * (`package.json` with `types`, `module` or `main`, else `index.*`). Packages are looked up in `node_modules` folders from the folder of the file up to the
 * root, then in the `node_modules` of the library folders of Haiku (the four B_*_LIB_DIRECTORY places). The `paths` and `baseUrl` of a `tsconfig.json`
 * are not read: such imports stay as written. A package that is found stays global.
 *
 * Text in JSX that holds a quote (`don't`) can swallow the rest of its line.
 */
class JavaScriptScanner : public LanguageScanner {
public:
	const char*	Name() const;
	const std::vector<FileKind>&	FileKinds() const;
	void		Scan(const std::string& text, std::vector<Dependency>* dependencies) const;
	void		Resolve(Dependency* dependency, const std::string& sourcePath) const;
};
