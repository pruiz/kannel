#!/bin/sh
#
# Use `test/fakewap' to test the bearerbox and the wapbox.

set -e

times=1
url="http://www.wapit.com/~liw/hello.wml"
loglevel=1

gw/bearerbox -v $loglevel gw/wapkannel.conf > check_bb.log 2>&1 &
bbpid=$!

sleep 5

gw/wapbox -v $loglevel gw/wapkannel.conf > check_wap.log 2>&1 &
wappid=$!

sleep 5

test/fakewap -m $times $url > check_fake.log 2>&1
ret=$?

kill -SIGINT $bbpid $wappid
wait

if [ "$ret" != 0 ]
then
	echo check_fakewap failed 1>&2
	echo See check_bb.log, check_wap.log, check_fake.log for info 1>&2
	exit 1
fi

rm check_bb.log check_wap.log check_fake.log
