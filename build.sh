#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds all plugins (or the named ones: ./build.sh pdf-navigator text-navigator) with the build.sh of each plugin folder.
# A plugin that fails does not stop the others; the summary at the end lists them and the exit code is 1 if any failed.
# JOBS sets the number of parallel jobs of make (default 4). ./package.sh then makes the packages from the results.
cd "$(dirname "$0")"
ROOT=$(pwd)
MAKEFLAGS="-j${JOBS:-4}"
export MAKEFLAGS

plugins="$*"
[ -n "$plugins" ] || plugins=$(cd plugins && ls -d */ | tr -d /)

built=
failed=
for name in $plugins; do
	if [ ! -x "$ROOT/plugins/$name/build.sh" ]; then
		echo "no such plugin (or no build.sh): $name" >&2
		failed="$failed $name"
		continue
	fi
	echo "=== $name"
	if (cd "$ROOT/plugins/$name" && ./build.sh); then
		built="$built $name"
	else
		failed="$failed $name"
	fi
done

echo
echo "built:${built:- none}"
if [ -n "$failed" ]; then
	echo "FAILED:$failed" >&2
	exit 1
fi
