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

/** A kind of source file: its extension (lower case, without the dot) and the semantic type that the plugin gives such a file (`text/x-python`). */
struct FileKind {
	std::string	extension;
	std::string	semanticType;
};

/**
 * @brief What a language needs to be understood by the extractor: how to recognize its files, how to find the references in the text, and where
 * to look for the files that they name.
 *
 * The plugin is called for files of the Haiku type `text/x-source-code`, which every language has. It first identifies the language (IdentifyFile:
 * by extension, refined or found by the content) and writes the semantic type to the attribute `META:TYPE`, then lets the scanner of that language read the references.
 *
 * To add a language: write a subclass, name the kinds of files (FileKinds), implement Scan() (a small lexer: skip comments and strings, collect
 * the references with their line) and Resolve() (the folders and the naming rules of the language), and add one line to the registry. Nothing else
 * changes: the result for the SEN server is the same for every language.
 */
class LanguageScanner {
public:
	virtual	~LanguageScanner() {}

	/** @return the name of the language, for the log */
	virtual	const char*	Name() const = 0;

	/** @return the kinds of source files of the language, by extension, each with its semantic type */
	virtual	const std::vector<FileKind>&	FileKinds() const = 0;

	/** @return the semantic type that the content tells (a shebang line), for a file whose extension is not known; empty if it does not tell */
	virtual	std::string	TypeFromContent(const std::string& text) const
							{ return std::string(); }

	/** @return the semantic type, made more exact by the content (a `.h` file is a C++ header if it has classes); the type itself by default */
	virtual	std::string	RefineType(const std::string& type, const std::string& text) const
							{ return type; }

	/** @return true if the semantic type is one of this language */
	bool				HandlesType(const std::string& type) const;

	/** @brief Find the references in the source text, in the order they are written (the `resolved` part is left empty). */
	virtual	void		Scan(const std::string& text, std::vector<Dependency>* dependencies) const = 0;

	/**
	 * @brief Look for the file of a reference and set `resolved` (the normalized path of the file; it stays empty if there is none). A
	 * scanner may also correct `global` once it knows where the file is (a Python module of the project is not a library).
	 * @param sourcePath the file that the reference is in (for names that are relative to it)
	 */
	virtual	void		Resolve(Dependency* dependency, const std::string& sourcePath) const
							{}
};

/** The language of a file, found out. */
struct Identification {
	const LanguageScanner*	scanner = NULL;		///< NULL if the language is not known
	std::string				semanticType;		///< the value for META:TYPE
};

/**
 * @brief Find out the language of a source file: by its extension (a `.h` is refined by the content), else by what the content tells (`#!/usr/bin/env
 * python3`), else by the semantic type that the file has already (META:TYPE).
 */
Identification			IdentifyFile(const std::string& path, const std::string& text);

/** @return the normalized path of the first folder that holds the relative name as a file, or an empty string */
std::string				FindFileInFolders(const std::vector<std::string>& folders, const std::string& name);
