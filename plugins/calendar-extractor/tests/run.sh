#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds and runs the tests of core/ (plain C++, no Haiku needed): the dates and recurrence rules, the reader of the lines of calendar files, and the events with the files of tests/fixtures.
set -e
cd "$(dirname "$0")/.."
out=${TMPDIR:-/tmp}/calendar-core-test
# the headers of sento as <sen/...>, as they are installed in Haiku
mkdir -p "$out-include" && ln -sfn "$PWD/../../../sento/src/cpp/include" "$out-include/sen"
for test in IcsTimeTest IcsParserTest CalendarTest IcsTextTest CalendarFactsTest CalendarSettingsTest IcsWriteTest; do
	${CXX:-c++} -std=c++17 -Wall -Wextra -Icore -I"$out-include" -DFIXTURES="\"$PWD/tests/fixtures\"" -o "$out-$test" tests/$test.cpp core/*.cpp
	echo "$test: $("$out-$test")"
done
