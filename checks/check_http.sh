#!/bin/sh 
# 
# Use `test/test_http{,_server}' to test gwlib/http.c. 
# Incuding the SSL client and server componentes. 
 
set -e 
 
times=2 
port="8040" 
port_ssl="8041" 
url="http://localhost:$port/foo.txt" 
url_ssl="https://localhost:$port_ssl/foo.txt" 
quiturl="http://localhost:$port/quit" 
quiturl_ssl="https://localhost:$port_ssl/quit" 
ssl_cert="gw/cert.pem" 
ssl_key="gw/key.pem" 
ssl_clientcert="/tmp/clientcert.pem" 
loglevel=0 
 
cat $ssl_cert $ssl_key > $ssl_clientcert 
 
test/test_http_server -p $port -v $loglevel > check_http_server.log 2>&1 & 
serverpid=$! 
 
sleep 1 
 
test/test_http_server -p $port_ssl -v $loglevel -s -c $ssl_cert -k $ssl_key > check_https_server.log 2>&1 & 
serverpid_ssl=$! 
 
sleep 1 
 
test/test_http -r $times $url > check_http.log 2>&1 
ret=$?

test/test_http -r $times -s -c $ssl_clientcert $url_ssl > check_https.log 2>&1 
ret=$? 
 
test/test_http -r 1 $quiturl >> check_http.log 2>&1
test/test_http -r 1 -s -c $ssl_clientcert $quiturl_ssl >> check_https.log 2>&1 
wait

if grep 'ERROR:|PANIC:' check_http*.log check_http*_server.log  > /dev/null  
then 
	echo check_http failed 1>&2 
	echo See check_http[s].log and check_http[s]_server.log for info 1>&2 
	exit 1 
fi 

rm check_http*.log
rm $ssl_clientcert
