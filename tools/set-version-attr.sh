#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Gives a plugin the attributes SEN:plugin:version (the version of its application resource, major.middle.minor) and SEN:plugin:features (what it does: the
# features that its resources switch on, "extract, identify"): the columns of the plugins in the SEN menu.
# usage: set-version-attr.sh Resources.rdef bin/<binary>      (called by the build.sh of the plugin, after resattr)
rdef=$1
binary=$2
major=$(sed -n 's/^[[:space:]]*major[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' "$rdef" | head -n1)
middle=$(sed -n 's/^[[:space:]]*middle[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' "$rdef" | head -n1)
minor=$(sed -n 's/^[[:space:]]*minor[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' "$rdef" | head -n1)
[ -n "$major" ] || { echo "no version in $rdef" >&2; exit 1; }
addattr -t string SEN:plugin:version "$major.${middle:-0}.${minor:-0}" "$binary"
features=$(for f in extract enrich identify navigate search; do grep -q "\"SEN:plugin:$f\")[[:space:]]*1" "$rdef" && echo $f; done | tr '\n' ',' | sed 's/,$//; s/,/, /g')
[ -z "$features" ] || addattr -t string SEN:plugin:features "$features" "$binary"
