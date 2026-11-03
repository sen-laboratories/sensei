#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds and runs the tests of the settings of BERT (no Haiku needed).
set -e
cd "$(dirname "$0")/.."
out=${TMPDIR:-/tmp}/bert-settings-test
${CXX:-c++} -std=c++20 -Wall -Wextra -o "$out" tests/BertSettingsTest.cpp BertSettings.cpp
"$out"
