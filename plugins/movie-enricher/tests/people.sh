#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Tests the writing of a movie and the making of its people (PeopleTest.cpp) in a Haiku VM: with the SEN server under test ($SERVER) and without one. It needs the tests of sen-core
# ($CORE_TESTS: TestSupport.h) and stops the server that runs; it leaves it stopped.
SRC=${SRC:-$(dirname "$0")}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
CORE_TESTS=${CORE_TESTS:-/Develop/SEN/sen-core/tests/vm}
HEADERS=/boot/home/config/non-packaged/develop/headers
PLUGIN=$SRC/..
OUT=${OUT:-/tmp/movie-people.out}
DONE=${DONE:-/tmp/movie-people.done}
rm -f "$DONE" "$OUT"
stop() { launch_roster stop x-vnd.sen-labs.sen-server > /dev/null 2>&1; for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done; sleep 2; }
{
g++ -std=c++20 -Wall -Wno-multichar -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -I$HEADERS -I$CORE_TESTS -I$PLUGIN/core -o /tmp/movie-people-test $SRC/PeopleTest.cpp \
	$PLUGIN/MoviePeople.cpp $PLUGIN/MovieWriter.cpp $PLUGIN/core/MovieTmdb.cpp $PLUGIN/../../common/ThumbnailUtil.cpp -lbe -ltranslation -lspdlog -lfmt 2>&1 | grep -E "error" | head -20
if [ -x /tmp/movie-people-test ]; then
	stop
	$SERVER > /tmp/sen_server.log 2>&1 &
	sleep 4
	/tmp/movie-people-test server
	stop
	/tmp/movie-people-test noserver
else
	echo "FAIL the test did not compile"
fi
} > "$OUT" 2>&1
touch "$DONE"
