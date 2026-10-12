/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include "LanguageScanner.h"

/**
 * @brief Rust: module declarations (`mod name;`), `use` paths and `extern crate`.
 *
 * Comments (also nested ones), strings, raw strings, byte strings and characters are skipped; a lifetime (`'a`) is told from a character. A
 * `use` tree is expanded into one reference for each path (`use a::{b, c::d};` is `a::b` and `a::c::d`; `self` in a group is the group's path, a `*` the path before
 * it). `mod name;` is the reference `self::name` (a file of the module); `#[path = "x.rs"] mod name;` is the path as written.
 *
 * Module paths are resolved to files: `crate::` from the source folder of the crate (the folder of `lib.rs` or `main.rs`, found above the file),
 * `self::` from the folder of the module of the file, `super::` from the one above, and a path that starts with a name from the module and then the crate
 * root (the 2018 edition). A module `a::b` is `a/b.rs` or `a/b/mod.rs`; where the path goes on into things that are not modules (`crate::a::Item`),
 * the last module is the file. The module of a file `foo.rs` has its folder `foo/`; the one of `main.rs`, `lib.rs` and `mod.rs` the folder of the file.
 * Paths into other crates (`std::io`, `serde::...`) have no sources in the project: they stay unresolved, and global. Modules of the crate are not global.
 */
class RustScanner : public LanguageScanner {
public:
	const char*	Name() const;
	const std::vector<FileKind>&	FileKinds() const;
	void		Scan(const std::string& text, std::vector<Dependency>* dependencies) const;
	void		Resolve(Dependency* dependency, const std::string& sourcePath) const;
};
