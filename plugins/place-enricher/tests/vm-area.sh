#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# A Location that says where it is (its city, region or country) is looked up in that country. "Westbahnhof" is the station in Wien and, in German, the Nyugati station in Budapest, which
# Nominatim ranks first: the city that the Location names decides (and a country on its own does). NEEDS THE NETWORK (the public service of Nominatim, one request per second) and the server of
# SEN. Run it in the Haiku VM after the plugin is installed (or PLUGIN=<the built program>): sh vm-area.sh. Prints `ok` or `FAIL` for each check and `FAILED` at the end. The places that the
# runs make in the Library are removed again.
PLUGIN=${PLUGIN:-/boot/system/add-ons/sen/plugins/SenPlaceEnricher}
T=${TEST_DIR:-/Develop/test}/_place-enricher-area
LIBRARY=$(finddir B_USER_DIRECTORY 2>/dev/null)/Library
rm -rf "$T"; mkdir -p "$T"; cd "$T" || exit 1
failed=0
check() { desc=$1; shift; if "$@" >/dev/null 2>&1; then echo "ok   $desc"; else echo "FAIL $desc"; failed=1; fi; }
attr() { catattr -d "$1" "$2" 2>/dev/null | tr -d '\n'; }
inside() { # inside <value> <from> <to>
	[ -n "$1" ] && [ "$(echo "$1 >= $2 && $1 <= $3" | bc -l 2>/dev/null)" = 1 ]
}
location() { # location <file> <attribute> <value>...: a Location called Westbahnhof
	f=$1; shift
	touch "$f"
	addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.location "$f"
	addattr -t string META:TYPE entity/x-vnd.sen-labs.entity.location "$f"
	addattr -t string dc:title Westbahnhof "$f"
	while [ $# -ge 2 ]; do addattr -t string "$1" "$2" "$f"; shift 2; done
}
run() { "$PLUGIN" --auto --debug --language en "$T/$1" > "$1.log" 2>&1; }

# the city says where: Wien
location in-wien schema:addressLocality Wien
run in-wien
check "Wien: the Location got a position" test -n "$(attr schema:latitude in-wien)"
check "Wien: it is in Vienna, not in Budapest" inside "$(attr schema:latitude in-wien)" 48.1 48.3
check "Wien: the country is Austria" test "$(attr schema:addressCountry in-wien)" = AT

# the city is named in English: the same
location in-vienna schema:addressLocality Vienna
run in-vienna
check "Vienna: it is in Vienna" inside "$(attr schema:latitude in-vienna)" 48.1 48.3

# the city decides, not the name: the same name, a city in Hungary
location in-budapest schema:addressLocality Budapest
run in-budapest
check "Budapest: it is in Budapest" inside "$(attr schema:latitude in-budapest)" 47.3 47.7
check "Budapest: the country is Hungary" test "$(attr schema:addressCountry in-budapest)" = HU

# a country on its own (a code) keeps the search in it
location in-austria schema:addressCountry AT
run in-austria
check "AT: the country is Austria" test "$(attr schema:addressCountry in-austria)" = AT

# the log says where the search was (Nominatim is asked for that country only, so that nothing of another country comes back; KeepInArea() is the net below that)
check "Wien: the log says that the search was in the country of the city" grep -q "searching in that country" in-wien.log

# the metro station and the train station of one name are told apart by their kind (a Place that has the link to the object of OpenStreetMap is read again, no search)
place() { # place <file> <link to OpenStreetMap>
	touch "$1"
	addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.place "$1"
	addattr -t string META:TYPE entity/x-vnd.sen-labs.entity.place "$1"
	addattr -t string dc:title Westbahnhof "$1"
	addattr -t string schema:sameAs "$2" "$1"
}
place metro https://www.openstreetmap.org/node/4161756600
place train https://www.openstreetmap.org/way/9881798
run metro
run train
check "the metro station is a metro station" test "$(attr SEN:placeType metro)" = "metro station"
check "the building of the train station is a train station" test "$(attr SEN:placeType train)" = "train station"

# the places that the runs made are removed again (the server enriches them in the background: waited for, else it would find them gone)
sleep 25
for log in in-wien.log in-vienna.log in-budapest.log in-austria.log; do
	made=$(grep -o "located near .* (made)" $log | sed 's/located near \(.*\) (made)/\1/')
	if [ -n "$made" ]; then
		for f in "$LIBRARY"/*/Places/"$made"; do
			[ -f "$f" ] && rm -f "$f" && echo "     (removed the place that the test made: $f)"
		done
	fi
done
[ $failed = 0 ] && echo "all ok" || echo FAILED
exit $failed
