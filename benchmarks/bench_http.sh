#!/bin/sh
#
# Run a simple HTTP benchmark.
#
# Lars Wirzenius

set -e

times=100000
port=8080

rm -f bench_http.log
test/test_http_server -v 4 -l bench_http.log -p $port &
sleep 1
test/test_http -q -v 2 -r $times http://localhost:$port/foo
test/test_http -q -v 2 http://localhost:$port/quit
wait

awk '/DEBUG: Request for/ { print $1, $2 }' bench_http.log  |
test/timestamp | uniq -c | 
awk '
    NR == 1 { first = $2 }
    { print $2 - first, $1 }
' > bench_http.dat

gnuplot <<EOF > benchmarks/bench_http.png
set terminal png
set xlabel "time (s)"
set ylabel "requests/s (Hz)"
plot "bench_http.dat" notitle with lines
EOF

gnuplot <<EOF > benchmarks/bench_http.ps
set terminal postscript eps color
set xlabel "time (s)"
set ylabel "requests/s (Hz)"
plot "bench_http.dat" notitle with lines
EOF

sed "s/#TIMES#/$times/g" benchmarks/bench_http.txt

rm -f bench_http.log
rm -f bench_http.dat
