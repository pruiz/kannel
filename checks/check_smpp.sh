#!/bin/sh
#
# Use `test/drive_smpp' to test SMPP driver.

set -e

times=1000

test/drive_smpp -v 4 -m $times -l check_smpp_drive.log &

sleep 1

gw/bearerbox -v 4 test/drive_smpp.conf &
bbpid=$!

running=yes
while [ $running = yes ]
do
    sleep 1
    if grep "ESME has submitted all messages to SMSC." check_smpp_drive.log \
    	>/dev/null
    then
        running=no
    fi
done

kill $bbpid
wait

if grep 'WARNING:|ERROR:|PANIC:' check_smpp*.log >/dev/null
then
        echo check_smpp.sh failed 1>&2
        echo See check_smpp*.log for info 1>&2
        exit 1
fi

rm check_smpp_drive.log check_smpp_bb.log
