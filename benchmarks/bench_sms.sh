#!/bin/sh
#
# Use `test/test_smsc' to test SMS speed.

set -e

times=100

rm -f bench_sms_smsc.log bench_sms_bb.log bench_sms_sb.log

test/test_smsc -r $times 2> bench_sms_smsc.log &
sleep 1
gw/bearerbox -v 4 benchmarks/bench_sms.conf & # XXX logfile name
sleep 1
gw/smsbox -v 4 benchmarks/bench_sms.conf &

wait

if grep 'WARNING:|ERROR:|PANIC:' bench_sms_*.log >/dev/null
then
        echo bench_sms.sh failed 1>&2
        echo See bench_sms*.log for info 1>&2
        exit 1
fi


awk '/INFO: Event .*, type submit/ { print $NF, $(NF-2) }' bench_sms_smsc.log |
uniq -c | 
awk '
    NR == 1 { first = $2 }
    { print $2 - first, $1 }
' > bench_sms.dat

gnuplot <<EOF > benchmarks/bench_sms.png
set terminal png
set xlabel "time (s)"
set ylabel "requests/s (Hz)"
plot "bench_sms.dat" notitle with lines
EOF

gnuplot <<EOF > benchmarks/bench_sms.ps
set terminal postscript eps color
set xlabel "time (s)"
set ylabel "requests/s (Hz)"
plot "bench_sms.dat" notitle with lines
EOF

sed "s/#TIMES#/$times/g" benchmarks/bench_sms.txt

rm -f bench_sms_smsc.log bench_sms_bb.log bench_sms_sb.log
rm -f bench_sms.dat
