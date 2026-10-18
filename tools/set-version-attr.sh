#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Gives a plugin the attribute SEN:plugin:version (the "Version" column of the plugins in the SEN menu): the version of its application resource, major.middle.minor.
# usage: set-version-attr.sh Resources.rdef bin/<binary>      (called by the build.sh of the plugin, after resattr)
rdef=$1
binary=$2
major=$(sed -n 's/^[[:space:]]*major[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' "$rdef" | head -n1)
middle=$(sed -n 's/^[[:space:]]*middle[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' "$rdef" | head -n1)
minor=$(sed -n 's/^[[:space:]]*minor[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' "$rdef" | head -n1)
[ -n "$major" ] || { echo "no version in $rdef" >&2; exit 1; }
addattr -t string SEN:plugin:version "$major.${middle:-0}.${minor:-0}" "$binary"
