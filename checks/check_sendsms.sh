#!/bin/sh
#
# Use `test/fakesmsc' to test sendsms in smsbox.

set -e

times=10
interval=0.001
loglevel=0
sendsmsport=13013
username=tester
password=foobar

test/fakesmsc -i $interval -m $times '123 234 nop' \
    > check_sendsms_smsc.log 2>&1 &

gw/bearerbox -v $loglevel gw/smskannel.conf > check_sendsms_bb.log 2>&1 &
bbpid=$!

gw/smsbox -v $loglevel gw/smskannel.conf > check_sendsms_sms.log 2>&1 &

i=0
while [ $i -lt $times ]
do
    test/test_http "http://localhost:$sendsmsport/cgi-bin/sendsms?from=123&to=234&text=test&username=$username&password=$password" \
    	>> check_sendsms.log 2>&1
    i=`expr $i + 1`
done

kill -INT $bbpid

kill -INT $bbpid
wait

if grep 'WARNING:|ERROR:|PANIC:' check_sendsms*.log >/dev/null ||
   [ $times -ne `grep -c 'got message .*: <123 234 test>' check_sendsms_smsc.log` ]
then
	echo check_sendsms.sh failed 1>&2
	echo See check_sendsms*.log for info 1>&2
	exit 1
fi

# rm check_sendsms*.log
