#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds the test client that talks to the plugin like the SEN server does (Haiku only), into $OUT (default /tmp/PluginClient).
set -e
cd "$(dirname "$0")"
g++ -std=c++20 -I/boot/home/config/non-packaged/develop/headers -o "${OUT:-/tmp/PluginClient}" PluginClient.cpp -lbe
