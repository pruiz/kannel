#!/bin/sh
#
# Use `test/test_http{,_server}' to test gwlib/http.c.

set -e

times=1000
port="8080"
url="http://localhost:$port/foo.txt"
loglevel=1

test/test_http_server -p $port -v $loglevel > check_http_server.log 2>&1 &
serverpid=$!

test/test_http -r $times $url > check_http.log 2>&1
ret=$?

kill $serverpid
wait

if [ "$ret" != 0 ] || \
   grep ERROR: check_http.log check_http_server.log > /dev/null 
then
	echo check_http failed 1>&2
	echo See check_http.log and check_http_server.log for info 1>&2
	exit 1
fi

rm check_http.log check_http_server.log
