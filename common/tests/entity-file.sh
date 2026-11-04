#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Tests common/EntityFile.h in a Haiku VM: with the SEN server under test ($SERVER) and without one. It stops the server that runs and leaves it stopped.
SRC=${SRC:-$(dirname "$0")}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
HEADERS=/boot/home/config/non-packaged/develop/headers
OUT=${OUT:-/tmp/entity-file.out}
DONE=${DONE:-/tmp/entity-file.done}
rm -f "$DONE" "$OUT"
stop() { launch_roster stop x-vnd.sen-labs.sen-server > /dev/null 2>&1; for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done; sleep 2; }
{
g++ -std=c++20 -Wall -Wno-multichar -I$HEADERS -o /tmp/entity-file-test $SRC/EntityFileTest.cpp -lbe 2>&1 | grep -E "error" | head -20
if [ -x /tmp/entity-file-test ]; then
	stop
	$SERVER > /tmp/sen_server.log 2>&1 &
	sleep 4
	/tmp/entity-file-test server
	stop
	/tmp/entity-file-test noserver
else
	echo "FAIL the test did not compile"
fi
} > "$OUT" 2>&1
touch "$DONE"
