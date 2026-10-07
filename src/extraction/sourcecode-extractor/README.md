<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# SEN source code extractor

Finds the `#include` directives of a source file (normal relations to the files it includes) and replies to the SEN server as described
in the developer guide of `sento`.

It uses **libclang** (the stable C API of clang, HaikuPorts package `llvm23_clang`, library `clang`): the file is only preprocessed, so no compile
command and no project files are needed, and an include whose header is missing is still found (without a resolved path). Only the directives of the
file itself are reported, not those of the headers it includes.

Result (one item, an entry per include in each field): `_label` the name as written, `path` where it was found, `global` for `<name>`,
and for relations inside the file `line` of the directive.

An earlier version used the clang C++ tooling API, adapted from [self-inc-first](https://github.com/xaizek/self-inc-first) (GPL-2.0-or-later).
That code is gone; this implementation is written against the documented libclang API (`clang_getInclusions` and the cursors of
`CXCursor_InclusionDirective`) and is MIT like the rest of SEN.
