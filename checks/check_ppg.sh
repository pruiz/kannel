#!/bin/bash
#
# Use 'test/test_ppg' and 'test/test_http_server' to test PPG. It presumes
# using of http smsc.
#
# Note: Running this script can take quite a long time, if your input does have
# many files. Two (ok ip and sms control document) should be quite enough for 
# a general make check. Use more only if you are interested of detailed test-
# ing of ppg.

set -e

list_port=8082
server_port=8081
push_port=8080
loglevel=0
username="foo"
password="bar"
prefix="test"
contents="sl" 
# Kannel configuration file
conf_file="gw/pushkannel.conf"
# Push content. Use only sl, because compilers should be tested separately
content_file="$prefix/sl.txt"
# Ok ip control files
ip_control_files="$prefix/*iptest*"
# Ok sms control files
sms_control_files="$prefix/*smstest*"
# Erroneous ip control files
wrong_ip_files="$prefix/*witest*"
# Erroneous sms control files
wrong_sms_files="$prefix/*wstest*"

test/test_http_server -p $list_port > check_http_list.log 2>&1 & listid=$
error=no

# ok control files requesting an ip bearer. Names contain string 'ip'. Bearer-
# box should not use smsc (do a http fetch) when ip bearer is requested.

for control_file in $ip_control_files;
    do 
        if [ -e $control_file ]
        then
            gw/bearerbox -v $loglevel $conf_file > check_bb.tmp 2>&1 & bbpid=$!
            sleep 2 

            gw/wapbox -v $loglevel $conf_file > check_wap.tmp 2>&1 & wappid=$!
            sleep 2

            test/test_ppg -c $contents http://localhost:$push_port/cgi-bin/wap-push.cgi?username=$username'&'password=$password $content_file $control_file > check_ppg.tmp 2>&1 
            sleep 2

            if ! grep "and type push response" check_ppg.tmp > /dev/null
            then
                cat check_ppg.tmp >> check_ppg.log 2>&1
                error=yes
                echo "ppg failed"
            fi

            if ! grep "Connectionless push accepted" check_wap.tmp > /dev/null
            then
                cat check_wap.tmp >> check_wap.log 2>&1
                error="yes"
                echo "wap failed"
            fi
        
            if ! grep "got wdp from wapbox" check_bb.tmp > /dev/null
            then
                cat check_bb.tmp >> check_bb.log 2>&1
                error=yes
                echo "bb failed"
            fi

            kill -SIGINT $wappid
            sleep 2
            kill -SIGINT $bbpid
            sleep 2

# We can panic when we are going down, too
            if [ $error != yes ]
            then
                if grep 'WARNING:|ERROR:|PANIC:' check_bb.tmp > /dev/null
                then
                    cat check_bb.tmp >> check_bb.log 2>&1
                    error="yes"
                    echo "got errors in bb"
                fi

                if grep 'WARNING:|ERROR:|PANIC:' check_wap.tmp > /dev/null
                then
                    cat check_wap.tmp >> check_wap.log 2>&1
                    error="yes"
                    echo "got errors in wap"
                fi 

                if grep 'WARNING:|ERROR:|PANIC:' check_ppg.tmp > /dev/null
                then
                    cat check_ppg.tmp >> check_ppg.log 2>&1
                    error="yes"
                    echo "got errors in ppg"
                fi 
           fi
         
           rm -f check_bb.tmp check_wap.tmp check_ppg.tmp
        fi;
    done

# Erroneous control files requesting an ip bearer. Ppg should reject these and
# report pi. Names contain string 'wi'.

for control_file in $wrong_ip_files;
    do 
        if [ -e $control_file ]
        then
            gw/bearerbox -v $loglevel $conf_file > check_bb.tmp 2>&1 & bbpid=$!
            sleep 2 

            gw/wapbox -v $loglevel $conf_file > check_wap.tmp 2>&1 & wappid=$!
            sleep 2

            test/test_ppg -c $contents http://localhost:$push_port/cgi-bin/wap-push.cgi?username=$username'&'password=$password $content_file $control_file > check_ppg.tmp 2>&1
            sleep 2

            if ! grep "and type push response" check_ppg.tmp > /dev/null &&
               ! grep "and type bad message response" check_ppg.tmp > /dev/null
            then
                cat check_ppg.tmp >> check_ppg.log 2>&1
                error=yes
                echo "ppg failed"
            fi

            if grep "Connectionless push accepted" check_wap.tmp > /dev/null &&
               grep "WARNING" check_wap.tmp > /dev/null 
            then
                cat check_wap.tmp >> check_wap.log 2>&1
                error="yes"
                echo "wap failed"
            fi
        
            if grep "got wdp from wapbox" check_bb.tmp > /dev/null
            then
                cat check_bb.tmp >> check_bb.log 2>&1
                error=yes
                echo "bb failed"
            fi

            kill -SIGINT $wappid
            sleep 2
            kill -SIGINT $bbpid
            sleep 2

# We can panic when we are going down, too
            if [ $error != yes ]
            then
                if grep 'ERROR:|PANIC:' check_bb.tmp > /dev/null
                then
                    cat check_bb.tmp >> check_bb.log 2>&1
                    error="yes"
                    echo "got errors in bb"
                fi

                if grep 'ERROR:|PANIC:' check_wap.tmp > /dev/null
                then
                    cat check_wap.tmp >> check_wap.log 2>&1
                    error="yes"
                    echo "got errors in wap"
                fi 

                if grep 'ERROR:|PANIC:' check_ppg.tmp > /dev/null
                then
                    cat check_ppg.tmp >> check_ppg.log 2>&1
                    error="yes"
                    echo "got errors in ppg"
                fi 
           fi
         
           rm -f check_bb.tmp check_wap.tmp check_ppg.tmp
        fi;
    done

# Ok control files requesting a sms bearer. Names contain string 'sms'. Ppg
# should use smsc (do a http fetch).

for control_file in $sms_control_files;
    do 
        if [ -e $control_file ]
        then
            test/test_http_server -p $server_port > check_http_sim.tmp 2>&1 & simid=$
            sleep 1
            gw/bearerbox -v $loglevel $conf_file > check_bb.tmp 2>&1 & bbpid=$!
            sleep 2 
            gw/wapbox -v $loglevel $conf_file > check_wap.tmp 2>&1 & wappid=$!
            sleep 2
            test/test_ppg -c $contents http://localhost:$push_port/cgi-bin/wap-push.cgi?username=$username'&'password=$password $content_file $control_file > check_ppg.tmp 2>&1 
            sleep 2

            if ! grep "and type push response" check_ppg.tmp > /dev/null
            then
                cat check_ppg.tmp >> check_ppg.log 2>&1
                error=yes
                echo "ppg failed"
            fi

            if ! grep "Connectionless push accepted" check_wap.tmp > /dev/null
            then
                cat check_wap.tmp >> check_wap.log 2>&1
                error="yes"
                echo "wap failed"
            fi
        
            if ! grep "got sms from wapbox" check_bb.tmp > /dev/null
            then
                cat check_bb.tmp >> check_bb.log 2>&1
                error=yes
                echo "bb failed"
            fi

            #if ! grep "request headers were" check_http_sim.tmp > /dev/null
            #then
            #    cat check_http_sim.tmp >> check_http_sim.log 2>&1
            #    error=yes
            #fi

            kill -SIGINT $wappid
            kill -SIGINT $bbpid
            sleep 2
            test/test_http -qv 4 http://localhost:$server_port/quit
            sleep 1
# We can panic when we are going down, too
            if [ $error != yes ] 
            then
                if grep 'WARNING:|ERROR:|PANIC:' check_bb.tmp > /dev/null
                then
                    cat check_bb.tmp >> check_bb.log 2>&1
                    error="yes"
                    echo "got errors in bb"
                fi 

                if grep 'WARNING:|ERROR:|PANIC:' check_wap.tmp > /dev/null
                then
                    cat check_wap.tmp >> check_wap.log 2>&1
                    error="yes"
                    echo "got errors in wap"
                fi 

                if grep 'WARNING:|ERROR:|PANIC:' check_ppg.tmp > /dev/null
                then
                    cat check_ppg.tmp >> check_ppg.log 2>&1
                    error="yes"
                    echo "got errors in ppg"
                fi

                if grep 'WARNING:|ERROR:|PANIC:' check_http_sim.tmp > /dev/null
                then
                    cat check_sim.tmp >> check_sim.log 2>&1
                    error="yes"
                    echo "got errors in http_sim"
                fi 
            fi
         
            rm -f check_bb.tmp check_wap.tmp check_ppg.tmp check_http_sim.tmp
        fi;
    done

# Erroneous control documents requesting a sms bearer. Ppg should reject these
# and inform pi. Names contain the string 'ws'.

for control_file in $wrong_sms_files;
    do 
        if [ -e $control_file ]
        then
            test/test_http_server -p $server_port > check_http_sim.tmp 2>&1 & simid=$
            sleep 1
            gw/bearerbox -v $loglevel $conf_file > check_bb.tmp 2>&1 & bbpid=$!
            sleep 2 

            gw/wapbox -v $loglevel $conf_file > check_wap.tmp 2>&1 & wappid=$!
            sleep 2

            test/test_ppg -c $contents http://localhost:$push_port/cgi-bin/wap-push.cgi?username=$username'&'password=$password $content_file $control_file > check_ppg.tmp 2>&1
            sleep 2

            if ! grep "and type push response" check_ppg.tmp > /dev/null &&
               ! grep "and type bad message response" check_ppg.tmp > /dev/null
            then
                cat check_ppg.tmp >> check_ppg.log 2>&1
                error=yes
                echo "ppg failed"
            fi

            if grep "Connectionless push accepted" check_wap.tmp > /dev/null &&
               grep "WARNING" check_wap.tmp > /dev/null 
            then
                cat check_wap.tmp >> check_wap.log 2>&1
                error="yes"
                echo "wap failed"
            fi
        
            if grep "got sms from wapbox" check_bb.tmp > /dev/null
            then
                cat check_bb.tmp >> check_bb.log 2>&1
                error=yes
                echo "bb failed"
            fi

            kill -SIGINT $wappid
            sleep 2
            kill -SIGINT $bbpid
            sleep 2
            test/test_http -qv 4 http://localhost:$server_port/quit
            sleep 1

# We can panic when we are going down, too
            if [ $error != yes ]
            then
                if grep 'ERROR:|PANIC:' check_bb.tmp > /dev/null
                then
                    cat check_bb.tmp >> check_bb.log 2>&1
                    error="yes"
                    echo "got errors in bb"
                fi

                if grep 'ERROR:|PANIC:' check_wap.tmp > /dev/null
                then
                    cat check_wap.tmp >> check_wap.log 2>&1
                    error="yes"
                    echo "got errors in wap"
                fi 

                if grep 'ERROR:|PANIC:' check_ppg.tmp > /dev/null
                then
                    cat check_ppg.tmp >> check_ppg.log 2>&1
                    error="yes"
                    echo "got errors in ppg"
                fi 

                if grep 'ERROR:|PANIC:' check_http_sim.tmp > /dev/null
                then
                    cat check_http_sim.tmp >> check_http_sim.log 2>&1
                    error=yes
                    echo "got errors in http_sim"
                fi
            fi
         
            rm -f check_bb.tmp check_wap.tmp check_ppg.tmp
        fi;
done

test/test_http -qv 4 http://localhost:$list_port/quit
wait

if [ $error == yes ]
then
        echo "check_ppg failed" 1>&2
	echo "See check_bb.log, check_wap.log, check_ppg.log," 1>&2
	echo "check_http_list.log, check_http_sim.log for info" 1>&2
	exit 1 
fi

rm -f check_bb.log check_wap.log check_ppg.log check_http_list.log check_http_sim.log

exit 0







