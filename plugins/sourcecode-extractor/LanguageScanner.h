/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <string>
#include <vector>

/** One reference from a source file to another file: an `#include`, a Java `import`, a Python `import`... */
struct Dependency {
	std::string	name;		///< as written in the source (`sen/Sen.h`, `java.util.List`)
	std::string	resolved;	///< the file that it leads to, empty if it was not found
	bool		global;		///< from the system or a library (`<name>`), not from the project (`"name"`, a relative import)
	unsigned	line;		///< the line where it is written, counted from 1
};

/**
 * @brief What a language needs to be understood by the extractor: how to find the references in the text, and where to look for the files
 * that they name. IncludeScanner picks the scanner by the extension of the file (see ScannerRegistry.cpp).
 *
 * To add a language (Java, Python, ...): write a subclass, give it the extensions of the files, implement Scan() (a small lexer: skip comments and
 * strings, collect the references with their line) and Resolve() (the folders and the naming rules of the language), and add one line to the
 * registry. Nothing else changes: the result for the SEN server is the same for every language.
 */
class LanguageScanner {
public:
	virtual	~LanguageScanner() {}

	/** @return the name of the language, for the log */
	virtual	const char*	Name() const = 0;

	/** @return the extensions of its source files, lower case without the dot */
	virtual	const std::vector<std::string>&	Extensions() const = 0;

	/** @brief Find the references in the source text, in the order they are written (the `resolved` part is left empty). */
	virtual	void		Scan(const std::string& text, std::vector<Dependency>* dependencies) const = 0;

	/**
	 * @brief Look for the file of a reference.
	 * @param sourcePath the file that the reference is in (for names that are relative to it)
	 * @return the normalized path of the file, or an empty string if there is none
	 */
	virtual	std::string	Resolve(const Dependency& dependency, const std::string& sourcePath) const
							{ return std::string(); }
};

/** @return the scanner for a file (by its extension), or NULL for a language that is not known */
const LanguageScanner*	ScannerForFile(const std::string& path);

/** @return the normalized path of the first folder that holds the relative name as a file, or an empty string */
std::string				FindFileInFolders(const std::vector<std::string>& folders, const std::string& name);
