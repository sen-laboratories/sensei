#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The relation "located at" that SUESS stores for a Location: a Location at the position of the Karlskirche in Vienna is looked up by its position (the position of a Location is its own, so it is sent
# to OpenStreetMap without the setting), and is then located at the place that was found: the Place that has its link to OpenStreetMap, else one that is made in the Library. NEEDS THE NETWORK (the public
# service of Nominatim, one request per second) and the server of SEN. Run it in the Haiku VM after the plugin is installed: sh vm.sh. Prints `ok` or `FAIL` for each check and `FAILED` at the end.
# The Place that the run makes is removed again, the one that was there before (a Library with the Karlskirche) stays.
PLUGIN=${PLUGIN:-/boot/home/config/add-ons/sen/plugins/SenPlaceEnricher}
T=${TEST_DIR:-/Develop/test}/_place-enricher-located-at
LIBRARY=$(finddir B_USER_DIRECTORY 2>/dev/null)/Library
rm -rf "$T"; mkdir -p "$T"; cd "$T" || exit 1
failed=0
check() { desc=$1; shift; if "$@" >/dev/null 2>&1; then echo "ok   $desc"; else echo "FAIL $desc"; failed=1; fi; }

touch "Point A"
addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.location "Point A"
addattr -t string META:TYPE entity/x-vnd.sen-labs.entity.location "Point A"
addattr -t double schema:latitude -- 48.1982785 "Point A"
addattr -t double schema:longitude -- 16.3718951 "Point A"

"$PLUGIN" --auto --debug "$T/Point A" > run.log 2>&1
REL=SEN:REL:x-vnd.sen-labs.relation.spatial.located-at
check "the Location was looked up (it has a title)" test -n "$(catattr -d dc:title "Point A" 2>/dev/null)"
check "the Location is located at a place (the relation is stored)" test -n "$(catattr -d $REL "Point A" 2>/dev/null)"
check "the place is the church, not a point of interest at the same position" grep -q "located at Karlskirche" run.log
check "the relation says who made it" sh -c "catattr -d $REL 'Point A' | grep -q 'prov:wasAttributedTo'"

# what the run made: the Place is removed again (found: it stays). The server enriches the new place in the background (the picture): that is waited for, else it would find the place gone
sleep 25
made=$(grep -o "located at .* (made)" run.log | sed 's/located at \(.*\) (made)/\1/')
if [ -n "$made" ]; then
	for f in "$LIBRARY"/*/Places/"$made"; do
		[ -f "$f" ] && rm -f "$f" && echo "     (removed the place that the test made: $f)"
	done
fi
cd /; rm -rf "$T"
[ $failed = 0 ] && echo "all ok" || echo FAILED
exit $failed
