#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The spatial navigator with GEOS on Haiku, on squares that are made here: every relation and both directions, and a place that is related to nothing. No network (the places have no Wikidata
# link). Run it in the Haiku VM after building the plugin: NAV=<path of SenSpatialNavigator> sh vm.sh. Prints `ok` or `FAIL` for each check, and `FAILED` at the end if one failed.
NAV=${NAV:-$(dirname "$0")/../bin/SenSpatialNavigator}
T=${TEST_DIR:-/Develop/test}/_spatial-navigator
rm -rf "$T"; mkdir -p "$T"; cd "$T" || exit 1
failed=0
TYPE=relation/x-vnd.sen-labs.relation.spatial

# a place with an outline: the square (x0,y0)-(x1,y1) in longitude/latitude, as Spaces keeps them
square() {
	name=$1; x0=$2; y0=$3; x1=$4; y1=$5
	touch "$name"
	addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.place "$name"
	addattr -t string dc:title "$name" "$name"
	addattr -t string geo:asWKT "POLYGON(($x0 $y0,$x1 $y0,$x1 $y1,$x0 $y1,$x0 $y0))" "$name"
	addattr -t double SEN:boxSouth -- $y0 "$name"; addattr -t double SEN:boxNorth -- $y1 "$name"
	addattr -t double SEN:boxWest -- $x0 "$name"; addattr -t double SEN:boxEast -- $x1 "$name"
	addattr -t double schema:latitude -- $(echo "($y0+$y1)/2" | bc -l) "$name"
	addattr -t double schema:longitude -- $(echo "($x0+$x1)/2" | bc -l) "$name"
}
square Big 0 0 10 10
square Neighbour 10 0 20 10		# its border meets that of Big
square Crossing 5 5 15 15		# has a part of Big
square Inner 2 2 4 4			# inside Big, away from its border
square Corner 0 0 4 4			# inside Big, in its corner: its border meets that of Big
square Twin 0 0 10 10			# the same area as Big
square Faraway 50 50 60 60
touch Pin
addattr -t mime BEOS:TYPE entity/x-vnd.sen-labs.entity.place Pin
addattr -t string dc:title Pin Pin
addattr -t double schema:latitude -- 5 Pin; addattr -t double schema:longitude -- 5 Pin		# a point inside Big

# expect <place> <relation> "<label name>|<label name>..." : what the navigator finds for the place, as a set
expect() {
	place=$1; relation=$2; wanted=$3
	found=$($NAV $TYPE.$relation "$T/$place" 2>/dev/null | grep '^  [a-z]' | awk '{ label=$1; for (i=2; i<NF; i++) label=label " " $i; print label "@" $NF }' | sed 's/ *@ */ /' | sort | tr '\n' '|' | sed 's/|$//')
	expected=$(echo "$wanted" | tr '|' '\n' | sort | tr '\n' '|' | sed 's/|$//')
	if [ "$found" = "$expected" ]; then echo "ok   $place $relation: ${expected:-nothing}"; else echo "FAIL $place $relation: found '$found', expected '$expected'"; failed=1; fi
}

expect Big within "contains Inner|contains Pin"
expect Big covered-by "covers Corner"
expect Big touch "touches Neighbour"
expect Big overlap "overlaps Crossing"
expect Big equal "equal to Twin"
expect Inner within "within Big|within Twin"
expect Corner covered-by "covered by Big|covered by Twin|covers Inner"
expect Pin within "within Big|within Twin"
expect Pin overlap ""
expect Neighbour touch "touches Big|touches Twin"
expect Neighbour overlap "overlaps Crossing"
expect Faraway within ""
expect Faraway touch ""

rm -rf "$T"
[ $failed -eq 0 ] && echo "all passed" || echo "FAILED"
exit $failed
