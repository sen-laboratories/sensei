<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# SEN source code extractor

Finds the references of a source file to other files (`#include`, later `import` of other languages) and gives them to the SEN server as relations
inside the file (normal relations to the files it includes), as described in the developer guide of `sento`.

It **reads** the file, it does not compile it: no compiler libraries, no compile command and no project files are needed, it is fast (a few
milliseconds) and small, and a reference to a file that does not exist is still found (without a resolved path). Only the references written in the
file itself are reported, not those of the headers it includes.

Result (one item, an entry per reference in each field): `_label` the name as written, `path` where it was found, `global` for `<name>`,
and for relations inside the file `line` of the directive.

## Languages

A language is a `LanguageScanner` (`LanguageScanner.h`): the extensions of its files, `Scan()` that finds the references in the text and `Resolve()` that
looks for the file that a reference names. `IncludeScanner` chooses the scanner by the extension of the file (`ScannerRegistry.cpp`).

| Language | Scanner | What it finds |
|---|---|---|
| C, C++, Objective-C | `CFamilyScanner` | `#include`, `#include_next`, `#import`; skips comments, strings (also raw strings), line continuations and blocks that are off (`#if 0`) |
| Python | `PythonScanner` | `import a.b as c, d`, `from a.b import c`, `from . import x`, `from ..pkg import y`; skips comments, strings and docstrings; finds statements after `;` and `if x:` |

To add a language (Java `import`, ...): write a subclass like `PythonScanner` (about 300 lines with the lexer), implement the three functions (a small lexer that skips
comments and strings, and the naming rules of the language: Java maps `a.b.C` to `a/b/C.java` below a source root), add it to the list in `ScannerRegistry.cpp`
and the file types to `Resources.rdef`. `Resolve()` may also correct `global`: a Python module found in the project is not a library.

C family headers are looked up like the compiler of Haiku does: `"name"` relative to the file first, then the header folders that `find_directory` names
(`B_USER_NONPACKAGED_HEADERS_DIRECTORY`, `B_USER_HEADERS_DIRECTORY`, `B_SYSTEM_NONPACKAGED_HEADERS_DIRECTORY`, `B_SYSTEM_HEADERS_DIRECTORY`, then what `BPathFinder`
adds), and in each of them the kits below `os`, `posix`, `bsd`, `gnu` and `c++`. Include folders of a project (`-I`) are not known: such headers are reported as written.

Python modules are looked up in the folder of the file, in the folder above its package, and in the library folders of Python (`python3*` and `site-packages` in
`B_USER_NONPACKAGED_LIB_DIRECTORY`, `B_USER_LIB_DIRECTORY`, `B_SYSTEM_NONPACKAGED_LIB_DIRECTORY` and `B_SYSTEM_LIB_DIRECTORY`).

An earlier version used libclang (which needs the 100+ MB package `llvm23_clang`) and before that the clang C++ tooling API (adapted from
[self-inc-first](https://github.com/xaizek/self-inc-first), GPL-2.0-or-later, long gone).
