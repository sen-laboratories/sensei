#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds the HPKG of each plugin (or of the named ones: ./package.sh pdf-navigator text-navigator) from plugins/<name>/bin/<binary>
# (./build.sh is run first for the plugins whose binary is missing or older than their sources or common/, FORCE=1 builds all of them;
# headers outside this repository (sen-core) are not tracked: build.sh by hand after changing them). The packages are written to the repository root; REVISION overrides the revision (default 1).
#
# From the plugin folder it takes: Resources.rdef (signature, version, descriptions, features, file types), the Makefile (name of the binary),
# the optional file "requires" (one HaikuPorts requirement per line, e.g. lib:libspdlog) and the optional script "package.stage" that is
# run with the stage folder as its argument to add files (a bundled library).
# The binary goes to add-ons/sen/plugins/ and keeps its attributes (the signature BEOS:APP_SIG is how the server finds it, see Sensei.h).
set -e
cd "$(dirname "$0")"
ROOT=$(pwd)
ARCH=$(getarch)
REVISION=${REVISION:-1}

plugins="$*"
all=
allrequires=
[ -n "$plugins" ] || { plugins=$(cd plugins && ls); all=1; }

# build what is not up to date (a plugin is out of date if a file of its folder or of common/ is newer than its binary)
stale=
for name in $plugins; do
	dir=$ROOT/plugins/$name
	[ -f "$dir/Resources.rdef" ] || { echo "no such plugin: $name" >&2; exit 1; }
	binary=$(sed -n 's/^NAME[[:space:]]*=[[:space:]]*\([^[:space:]]*\).*/\1/p' "$dir/Makefile" | head -n1)
	if [ -n "$FORCE" ] || [ ! -f "$dir/bin/$binary" ] || [ -n "$(find "$dir" "$ROOT/common" "$ROOT/tools" -type f ! -path '*/bin/*' ! -path '*/objects.*' ! -name '*.rsrc' -newer "$dir/bin/$binary" 2>/dev/null | head -n1)" ]; then
		stale="$stale $name"
	fi
done
if [ -n "$stale" ]; then
	echo "building:$stale"
	./build.sh $stale
fi

for name in $plugins; do
	dir=$ROOT/plugins/$name
	[ -f "$dir/Resources.rdef" ] || { echo "no such plugin: $name" >&2; exit 1; }
	rdef=$dir/Resources.rdef
	binary=$(sed -n 's/^NAME[[:space:]]*=[[:space:]]*\([^[:space:]]*\).*/\1/p' "$dir/Makefile" | head -n1)
	[ -f "$dir/bin/$binary" ] || { echo "$name: bin/$binary not found" >&2; exit 1; }

	major=$(sed -n 's/^[[:space:]]*major[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $rdef | head -n1)
	middle=$(sed -n 's/^[[:space:]]*middle[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $rdef | head -n1)
	minor=$(sed -n 's/^[[:space:]]*minor[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $rdef | head -n1)
	variety=$(sed -n 's/^[[:space:]]*variety[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $rdef | head -n1)
	internal=$(sed -n 's/^[[:space:]]*internal[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $rdef | head -n1)
	version="$major.$middle.$minor"
	# the stage and its count come from the resource (the About window of the plugin shows 0.1.0-beta.3 for the package 0.1.0~beta3);
	# 0 = development, 2 = beta, a development version can be named with SEN_VERSION_SUFFIX (preview1: 0.1.0~preview1)
	suffix=
	case "$variety" in
		0) suffix=develop; [ "${internal:-0}" -gt 0 ] && suffix=develop.$internal; suffix=${SEN_VERSION_SUFFIX:-$suffix} ;;
		2) suffix=beta${internal:-1} ;;
	esac
	[ -n "$suffix" ] && version="$version~$suffix"
	release=$suffix		# (the plugins of a release have the same stage: the package of all of them is named after it)
	version="$version-$REVISION"

	licenses='"MIT"'
	[ -f "$dir/licenses" ] && licenses=$(awk 'NF { printf "%s\"%s\"", (n++ ? "\\n\\t" : ""), $0 }' "$dir/licenses")
	summary=$(sed -n 's/.*short_info[[:space:]]*=[[:space:]]*"\(.*\)".*/\1/p' $rdef | head -n1)
	description=$(sed -n 's/.*long_info[[:space:]]*=[[:space:]]*"\(.*\)".*/\1/p' $rdef | head -n1 | sed 's/"/\\"/g')
	signature=$(sed -n 's/^resource app_signature "\(.*\)";/\1/p' $rdef)
	features=$(for f in extract enrich identify navigate search; do grep -q "\"SEN:plugin:$f\")[[:space:]]*1" $rdef && echo $f; done | tr '\n' ',' | sed 's/,$//; s/,/, /g')
	types=$(sed -n '/resource file_types message/,/};/p' $rdef | sed -n 's/.*"types"[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' | tr '\n' ',' | sed 's/,$//; s/,/, /g')
	package=sen_plugin_$(echo $name | tr '-' '_')
	# the package of all plugins requires each of them at least in this version (an older one in a repository must not do)
	allrequires="$allrequires$(printf '\\n\\t%s >= %s' "$package" "${version%-$REVISION}")"

	requires=
	[ -f "$dir/requires" ] && requires=$(awk 'NF { printf "\\n\\t%s", $0 }' "$dir/requires")

	stage=$ROOT/package-build/$name
	rm -rf "$stage"
	mkdir -p "$stage/add-ons/sen/plugins" "$stage/documentation/packages/$package"
	cp -a "$dir/bin/$binary" "$stage/add-ons/sen/plugins/"
	cp "$ROOT/LICENSE" "$stage/documentation/packages/$package/"
	[ -f "$dir/README.md" ] && cp "$dir/README.md" "$stage/documentation/packages/$package/"
	[ -f "$dir/package.stage" ] && sh "$dir/package.stage" "$stage"

	# the logo of the About window of the plugin (sen/SenAbout.h): the picture in the folder images of the plugin, else the one of SENSEI
	logo=$(ls "$dir"/images/*logo*.jpg 2>/dev/null | head -n1)
	[ -n "$logo" ] || logo=$ROOT/images/sensei-logo.jpg
	if [ -f "$logo" ]; then
		mkdir -p "$stage/data/sen/plugins/$signature"
		cp "$logo" "$stage/data/sen/plugins/$signature/logo.jpg"
	fi

	# the signature must be there as the attribute that the server queries for
	attr=$(catattr -d BEOS:APP_SIG "$stage/add-ons/sen/plugins/$binary" 2>/dev/null || true)
	if [ "$attr" != "$signature" ]; then
		echo "$name: the binary has the signature '$attr', not '$signature' (build it with build.sh, which sets the attributes)" >&2
		exit 1
	fi

	# (the requirements are lines of their own: \n\t before each, GNU sed makes newlines of them)
	sed -e "s|@PACKAGE@|$package|g" -e "s|@VERSION@|$version|g" -e "s|@ARCH@|$ARCH|g" -e "s|@SUMMARY@|$summary|g" \
		-e "s|@DESCRIPTION@|$description|g" -e "s|@FEATURES@|$features|g" -e "s|@TYPES@|$types|g" \
		-e "s|@REQUIRES@|$requires|" -e "s|@LICENSES@|$licenses|" package/PackageInfo.in > "$stage/.PackageInfo"

	out=$package-$version-$ARCH.hpkg
	rm -f $package-*.hpkg
	package create -C "$stage" "$out" >/dev/null
	echo "created $out"
done

# the package that brings all of them (only when all plugins were packaged)
if [ -n "$all" ]; then
	requires=$allrequires
	stage=$ROOT/package-build/meta
	rm -rf "$stage"
	mkdir -p "$stage"
	sed -e "s|@VERSION@|2.0.0${release:+~$release}-$REVISION|g" -e "s|@REQUIRES@|$requires|" package/PackageInfo.meta.in > "$stage/.PackageInfo"
	rm -f sen_plugins-*.hpkg
	package create -C "$stage" "sen_plugins-2.0.0${release:+~$release}-$REVISION-any.hpkg" >/dev/null
	echo "created sen_plugins-2.0.0${release:+~$release}-$REVISION-any.hpkg"
fi
rm -rf package-build
