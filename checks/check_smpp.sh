#!/bin/sh
#
# Use `test/drive_smpp' to test SMPP driver.

set -e
#set -x

times=10

test/drive_smpp -v 0 -m $times 2> check_smpp_drive.log & 
sleep 1

gw/bearerbox -v 0 test/drive_smpp.conf 2> check_smpp_bb.log &  
bbpid=$!

running=yes
while [ $running = yes ]
do
    sleep 1
    if grep "ESME has submitted all messages to SMSC." check_smpp_drive.log > /dev/null  
    then
        running=no
    fi
done

kill $bbpid
sleep 5

if grep 'WARNING:|ERROR:|PANIC:' check_smpp*.log >/dev/null
then
        echo check_smpp.sh failed 1>&2
        echo See check_smpp*.log for info 1>&2
        exit 1
fi

rm -f check_smpp_drive.log check_smpp_bb.log

exit 0







