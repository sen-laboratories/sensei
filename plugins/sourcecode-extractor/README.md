<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# SEN source code extractor

Identifies the language of a source file and sets its semantic type, finds its references to other files (`#include`, `import`, `mod`, `use`) and gives them to the SEN server as relations
inside the file (normal relations to the files it includes), as described in the developer guide of `sento`.

It **reads** the file, it does not compile it: no compiler libraries, no compile command and no project files are needed, it is fast (a few
milliseconds) and small, and a reference to a file that does not exist is still found (without a resolved path). Only the references written in the
file itself are reported, not those of the headers it includes.

Result (one item, an entry per reference in each field): `_label` the name as written, `path` where it was found, `global` for `<name>`,
and for relations inside the file `line` of the directive.

## Identify, then scan

The plugin is called for files of the Haiku type `text/x-source-code`, which every language has. It has the features **extract** and **identify**: first it finds out
the language of the file and sets its **semantic type**, the attribute `META:TYPE` (`text/x-csrc`, `text/x-c++src`, `text/x-chdr`, `text/x-c++hdr`, `text/x-objcsrc`,
`text/x-python`, `text/x-java-source`, `text/x-go`, `text/x-rust`, `text/javascript`, `text/jsx`, `text/x-typescript`, `text/tsx`), then it lets the scanner of that language read the references. The language is found

1. by the extension (a `.h` file is a C++ header if it has classes, namespaces, templates or the like, else a C header),
2. else by the content (a script: `#!/usr/bin/env python3`),
3. else by a semantic type that the file has already.

Before it sets `META:TYPE` it makes sure that the file has its Haiku type (`BEOS:TYPE`): without it Haiku takes a file with `META:TYPE` for an entry of the MIME
database. The type is in the reply (`META:TYPE`, `sensei::key::kSemanticType`), too. `SenCodeExtractor --identify <file>` only identifies and sets the type.

## Languages

| Language | Scanner | What it finds |
|---|---|---|
| C, C++, Objective-C | `CFamilyScanner` | `#include`, `#include_next`, `#import` (C++ headers: `hh hpp hxx h++ inl` and the files of template and inline definitions `ipp tpp txx tcc icc`); skips comments, strings (also raw strings), line continuations and blocks that are off (`#if 0`) |
| Go | `GoScanner` | `import "x"`, aliased, `.`, `_` and groups `import ( ... )`; skips comments, strings, raw strings; a package is resolved to its **folder** (module from `go.mod`, `vendor`, GOROOT/GOPATH, `go/src` in the library folders) |
| Java | `JavaScanner` | `import a.b.C;`, `import a.b.*;`, `import static a.b.C.member;` (the class); skips comments, strings, text blocks; imports at the top level only |
| JavaScript, TypeScript (JSX, TSX) | `JavaScriptScanner` | `import ... from 'x'`, `import 'x'`, `import x = require('x')`, `export ... from 'x'`, `require('x')`, `import('x')`, `/// <reference path="x" />`; skips comments, strings, template literals (nested `${}`) and regular expressions; resolves like Node and TypeScript (extensions, `x.js` for `x.ts`, folders with `package.json`/`index.*`, `node_modules` up the tree and in the library folders of Haiku); `tsconfig` paths are not read |
| Python | `PythonScanner` | `import a.b as c, d`, `from a.b import c`, `from . import x`, `from ..pkg import y`; skips comments, strings and docstrings; finds statements after `;` and `if x:` |
| Rust | `RustScanner` | `mod name;` (also `#[path = ".."]` and inside inline modules), `use` trees expanded to one reference per path, `extern crate`; skips nested comments, strings, raw strings, characters (lifetimes are told apart); module paths resolve to `a.rs` or `a/mod.rs` from the crate root, the module of the file or `super` |

A note on the **Haiku type**: the plugin is only called for files that Haiku types `text/x-source-code`, which is decided by the extensions that the MIME database lists for that
type (Tracker types a new file by them). The system lists `c cc cpp cxx h h++ hpp go java js json py rs cs php lua ...`, but not `hh hxx inl mjs cjs jsx ts tsx mts cts`: such files
are `text/plain` until their type is set (or the extensions are added to `text/x-source-code` in the MIME database, which is the user's choice).

To add a language (C#, PHP, Ruby, ...): write a subclass like `JavaScanner` or `PythonScanner` (about 300 lines with the lexer), name its file kinds (extension and semantic type), implement the functions (a small lexer that skips
comments and strings, and the naming rules of the language: Java maps `a.b.C` to `a/b/C.java` below a source root), add it to the list in `ScannerRegistry.cpp`
and the file types to `Resources.rdef`. `Resolve()` may also correct `global`: a Python module found in the project is not a library.

C family headers are looked up like the compiler of Haiku does: `"name"` relative to the file first, then the header folders that `find_directory` names
(`B_USER_NONPACKAGED_HEADERS_DIRECTORY`, `B_USER_HEADERS_DIRECTORY`, `B_SYSTEM_NONPACKAGED_HEADERS_DIRECTORY`, `B_SYSTEM_HEADERS_DIRECTORY`, then what `BPathFinder`
adds), and in each of them the kits below `os`, `posix`, `bsd`, `gnu` and `c++`. Include folders of a project (`-I`) are not known: such headers are reported as written.

Java classes are looked up as `a/b/C.java` in the source root that the `package` declaration leads to and in the usual layouts (`src/main/java`, `src/test/java`, `src`) above the file;
the classes of libraries have no sources and stay unresolved (and global).

Python modules are looked up in the folder of the file, in the folder above its package, and in the library folders of Python (`python3*` and `site-packages` in
`B_USER_NONPACKAGED_LIB_DIRECTORY`, `B_USER_LIB_DIRECTORY`, `B_SYSTEM_NONPACKAGED_LIB_DIRECTORY` and `B_SYSTEM_LIB_DIRECTORY`).

An earlier version used libclang (which needs the 100+ MB package `llvm23_clang`) and before that the clang C++ tooling API (adapted from
[self-inc-first](https://github.com/xaizek/self-inc-first), GPL-2.0-or-later, long gone).
