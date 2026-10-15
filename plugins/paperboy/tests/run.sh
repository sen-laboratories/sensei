#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds and runs the tests of core/ (no Haiku needed). The nlohmann/json headers are found through JSON_INCLUDE
# (the folder that contains "nlohmann"; on Haiku the package nlohmann_json puts them into the system headers).
set -e
cd "$(dirname "$0")/.."
out=${TMPDIR:-/tmp}/paperboy-core-test
${CXX:-c++} -std=c++20 -Wall -Wextra -Icore ${JSON_INCLUDE:+-I$JSON_INCLUDE} -o "$out" tests/PaperCoreTest.cpp core/*.cpp
"$out" tests/data
