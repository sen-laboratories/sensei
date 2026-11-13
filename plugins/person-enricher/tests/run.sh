#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds and runs the tests of core/ (no Haiku needed).
set -e
cd "$(dirname "$0")/.."
out=${TMPDIR:-/tmp}/person-core-test

JSON_INC=""
if [ -n "$JSON_INCLUDE" ]; then
    JSON_INC="-I$JSON_INCLUDE"
elif [ -d "/opt/homebrew/opt/nlohmann-json/include" ]; then
    JSON_INC="-I/opt/homebrew/opt/nlohmann-json/include"
elif [ -d "/usr/local/include" ]; then
    JSON_INC="-I/usr/local/include"
fi

${CXX:-c++} -std=c++20 -Wall -Wextra -Icore $JSON_INC -o "$out" tests/PersonCoreTest.cpp core/*.cpp
"$out"
