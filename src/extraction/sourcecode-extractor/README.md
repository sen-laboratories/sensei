<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 SEN Labs e.U.
-->
# SEN source code extractor

Finds the includes of a source file (normal relations to the files it includes) and its structure (contained relations) with the
clang preprocessor, and replies to the SEN server as described in the developer guide of `sento`.

## License note: GPL-2.0-or-later parts

Three files of `clang-include-checker/` come from [self-inc-first](https://github.com/xaizek/self-inc-first) by xaizek, which is
licensed under the **GNU General Public License, version 2 or (at your option) any later version**, and were adapted for newer clang
and for SEN:

- `ClangWrapper.cpp` (the original license header is kept)
- `IncludeFinder.hpp`, `IncludeFinderAction.hpp` ("originally taken from ... and adapted")

They are not MIT and are listed in `../../../.license-headers-skip`. The program built from them, `SenCodeExtractor`, is therefore a combined work
under the GPL-2.0-or-later when it is distributed. It is a separate program that the SEN server starts and talks to with messages, so this does not
reach the rest of SEN (the server, the other plugins, the API headers), which stay MIT.

To get rid of the GPL parts the three files would have to be rewritten (they are small), or the author asked for a different license.
Until then a package of this plugin has to be licensed `GPL-2.0-or-later` and offer the source.
