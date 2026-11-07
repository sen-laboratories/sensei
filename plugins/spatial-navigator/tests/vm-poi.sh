#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The points of interest of the spatial navigator on Haiku, against the public Overpass API (needs the network): a Location in the 14th district of Vienna is asked for what is around it; the
# places that are made get a name, a kind, a position and the link to OpenStreetMap; a second ask finds the same files (nothing is made twice); the place itself is not one of them, a place
# that has the link of a thing is that thing; a place with no position and one that is a whole city have none. Run it in the Haiku VM after building the plugin:
#   NAV=<path of SenSpatialNavigator> sh vm-poi.sh
# Prints `ok` or `FAIL` for each check, and `FAILED` at the end if one failed. The places that were made (in the Library of the current context if the SEN server answers, else next to the
# file) are found by their time and removed at the end.
NAV=${NAV:-$(dirname "$0")/../bin/SenSpatialNavigator}
T=${TEST_DIR:-/Develop/test}/_spatial-poi
POI=relation/x-vnd.sen-labs.relation.spatial.point-of-interest
rm -rf "$T"; mkdir -p "$T"; cd "$T" || exit 1
failed=0
touch -d "1 second ago" stamp 2>/dev/null || touch stamp
sleep 2
# the places that were made since the start: files of the type Place with a link to OpenStreetMap, in the Library (~/Library) or here
made_places() {
	find "$HOME/Library" "$T" -type f -newer "$T/stamp" 2>/dev/null | while read -r f; do
		catattr -d schema:sameAs "$f" 2>/dev/null | grep -q 'openstreetmap.org/\(node\|way\|relation\)/[0-9]' && echo "$f"
	done
}
check() { if [ "$1" = ok ]; then echo "ok   $2"; else echo "FAIL $2"; failed=1; fi; }

# a Location with a position (Hütteldorfer Straße, Vienna)
touch Here
addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.location Here
addattr -t string dc:title Here Here
addattr -t double schema:latitude -- 48.1985 Here; addattr -t double schema:longitude -- 16.3010 Here

$NAV $POI "$T/Here" > first.txt 2>&1
count=$(grep -c '^  point of interest ' first.txt)
[ "$count" -ge 10 ] && check ok "Here: $count points of interest" || { check FAIL "Here: only $count points of interest"; cat first.txt; }
[ "$count" -le 100 ] && check ok "at most 100" || check FAIL "more than 100: $count"

# the ones that were made: a Place with a link to OpenStreetMap, a position and (mostly) a kind
made=$(made_places)
[ -n "$made" ] && check ok "$(echo "$made" | wc -l | tr -d ' ') places were made" || check FAIL "no place was made"
bad=0; typed=0
OLDIFS=$IFS; IFS='
'
for f in $made; do
	t=$(catattr -d BEOS:TYPE "$f" 2>/dev/null)
	[ "$t" = "entity/x-vnd.sen-labs.entity.place" ] || bad=1
	catattr schema:sameAs "$f" 2>/dev/null | grep -q 'openstreetmap.org/\(node\|way\|relation\)/[0-9]' || bad=1
	catattr schema:latitude "$f" >/dev/null 2>&1 && catattr schema:longitude "$f" >/dev/null 2>&1 || bad=1
	catattr dc:title "$f" >/dev/null 2>&1 || bad=1
	catattr SEN:placeType "$f" >/dev/null 2>&1 && typed=$((typed+1))
done
IFS=$OLDIFS
[ $bad -eq 0 ] && check ok "each is a Place with title, OpenStreetMap link and position" || check FAIL "a made place lacks type, title, link or position"
[ "$typed" -ge 5 ] && check ok "$typed have a kind" || check FAIL "only $typed have a kind"

# asked again: the same files, nothing new (the answer is also kept for a day)
before=$(made_places | wc -l)
$NAV $POI "$T/Here" > second.txt 2>&1
after=$(made_places | wc -l)
[ "$before" = "$after" ] && check ok "second ask makes nothing" || check FAIL "second ask made $((after-before)) more"
[ "$(grep -c '^  point of interest ' second.txt)" = "$count" ] && check ok "second ask: the same $count" || check FAIL "second ask found another number"

# a place with the link of a thing is that thing, and not its own point of interest
firstfile=$(made_places | head -n1)
first=$(basename "$firstfile")
link=$(catattr -d schema:sameAs "$firstfile" | head -n1)
lat=$(catattr -d schema:latitude "$firstfile"); lon=$(catattr -d schema:longitude "$firstfile")
touch Self
addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.place Self
addattr -t string dc:title Self Self
addattr -t string schema:sameAs "$link" Self
addattr -t double schema:latitude -- "$lat" Self; addattr -t double schema:longitude -- "$lon" Self
$NAV $POI "$T/Self" > self.txt 2>&1
grep -q "^  point of interest $first\$" self.txt && check FAIL "Self is its own point of interest" || check ok "a place is not its own point of interest"

# no position: none. A whole city: none (more than 3 km across)
touch Nowhere
addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.place Nowhere
addattr -t string dc:title Nowhere Nowhere
$NAV $POI "$T/Nowhere" > nowhere.txt 2>&1
[ "$(grep -c '^  point' nowhere.txt)" = 0 ] && check ok "no position: none" || check FAIL "no position: found some"
touch City
addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.place City
addattr -t string dc:title City City
addattr -t double SEN:boxSouth -- 48.12 City; addattr -t double SEN:boxNorth -- 48.32 City
addattr -t double SEN:boxWest -- 16.18 City; addattr -t double SEN:boxEast -- 16.58 City
$NAV $POI "$T/City" > city.txt 2>&1
[ "$(grep -c '^  point' city.txt)" = 0 ] && grep -q "too large" city.txt && check ok "a city is too large" || check FAIL "a city was asked for"

# the mirror of the settings answers (a place away from the others: the answer of the public server is kept, and would be used)
SETTINGS="$HOME/config/settings/sen/spatial-navigator.settings"
mkdir -p "$(dirname "$SETTINGS")"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$T/settings.saved"
printf 'overpass_mirror = vk-maps\n' > "$SETTINGS"
touch There
addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.location There
addattr -t string dc:title There There
addattr -t double schema:latitude -- 48.2088 There; addattr -t double schema:longitude -- 16.3125 There
$NAV $POI "$T/There" > there.txt 2>&1
grep -q '^asking https://maps.mail.ru/osm/tools/overpass/api/interpreter' there.txt && check ok "the mirror of the settings (VK Maps) is the one that is asked" || check FAIL "the mirror was not asked"
[ "$(grep -c '^  point of interest ' there.txt)" -ge 10 ] && check ok "the mirror answers: $(grep -c '^  point of interest ' there.txt) points of interest" || { check FAIL "the mirror gave no points of interest"; cat there.txt; }
if [ -f "$T/settings.saved" ]; then cp "$T/settings.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi

# remove what was made (not in the folder of the test, which is removed as a whole)
IFS='
'
for f in $(made_places); do rm -f "$f"; done
IFS=$OLDIFS
rm -rf "$T"
[ $failed -eq 0 ] && echo "all passed" || echo "FAILED"
exit $failed
