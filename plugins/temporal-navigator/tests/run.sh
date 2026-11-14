#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds and runs the tests of core/ (no Haiku needed). The core uses one plain header of SEN (<sen/SenEdtf.h>): the checkout of sento beside this one, else the headers that are
# installed (the folder SEN_INCLUDE that contains "sen", else those of the user on Haiku).
set -e
cd "$(dirname "$0")/.."
out=${TMPDIR:-/tmp}/temporal-core-test
inc=${TMPDIR:-/tmp}/temporal-core-include
rm -rf "$inc"; mkdir -p "$inc"
[ -d ../../../sento/src/cpp/include ] && ln -s "$PWD/../../../sento/src/cpp/include" "$inc/sen"
${CXX:-c++} -std=c++17 -Wall -Wextra -Icore -I"$inc" ${SEN_INCLUDE:+-I$SEN_INCLUDE} -I"$HOME/config/non-packaged/develop/headers" -o "$out" tests/TemporalCoreTest.cpp core/*.cpp
"$out"
