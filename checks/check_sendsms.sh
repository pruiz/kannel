#!/bin/sh
#
# Use `test/fakesmsc' to test sendsms in smsbox.

set -e

times=10
interval=0
loglevel=0
sendsmsport=13013
username=tester
password=foobar

url="http://localhost:$sendsmsport/cgi-bin/sendsms?from=123&to=234&\
text=test&username=$username&password=$password"

gw/bearerbox -v $loglevel gw/smskannel.conf > check_sendsms_bb.log 2>&1 &
bbpid=$!

sleep 2

test/fakesmsc -i $interval -m $times '123 234 text nop' \
    > check_sendsms_smsc.log 2>&1 &

sleep 1
gw/smsbox -v $loglevel gw/smskannel.conf > check_sendsms_sms.log 2>&1 &

sleep 2

i=0
while [ $i -lt $times ]
do
    test/test_http $url >> check_sendsms.log 2>&1
    i=`expr $i + 1`
done

sleep 5

kill -INT $bbpid
wait

if grep 'WARNING:|ERROR:|PANIC:' check_sendsms*.log >/dev/null ||
   [ $times -ne `grep -c 'Got message .*: <123 234 text test>' \
    check_sendsms_smsc.log` ]
then
	echo check_sendsms.sh failed 1>&2
	echo See check_sendsms*.log for info 1>&2
	exit 1
fi

rm check_sendsms*.log
