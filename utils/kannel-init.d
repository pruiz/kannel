#!/bin/sh
# Start/stop the Kannel boxes: One bearer box and one WAP box.

# This is the default init.d script for Kannel.  Its configuration is
# appropriate for a small site running Kannel on one machine.

# Make sure that the Kannel binaries can be found in $BOXPATH or somewhere
# else along $PATH.  run_kannel_box has to be in $BOXPATH.

BOXPATH=/usr/bin
PIDFILES=/var/run
CONF=/etc/kannel/kannel.conf

PATH=$BOXPATH:$PATH

# On Debian, the most likely reason for the bearerbox not being available
# is that the package is in the "removed" or "unconfigured" state, and the
# init.d script is still around because it's a conffile.  This is normal,
# so don't generate any output.
test -x $BOXPATH/bearerbox || exit 0

case "$1" in
  start)
    echo -n "Starting WAP gateway: bearerbox"
    start-stop-daemon --start --quiet --pidfile $PIDFILES/kannel_bearerbox.pid --exec $BOXPATH/run_kannel_box -- --pidfile $PIDFILES/kannel_bearerbox.pid bearerbox -- $CONF
    echo -n " wapbox"
    start-stop-daemon --start --quiet --pidfile $PIDFILES/kannel_wapbox.pid --exec $BOXPATH/run_kannel_box -- --pidfile $PIDFILES/kannel_wapbox.pid wapbox -- $CONF
    echo "."
    ;;

  stop)
    echo -n "Stopping WAP gateway: wapbox"
    start-stop-daemon --stop --quiet --pidfile $PIDFILES/kannel_wapbox.pid --exec $BOXPATH/run_kannel_box
    echo -n " bearerbox"
    start-stop-daemon --stop --quiet --pidfile $PIDFILES/kannel_bearerbox.pid --exec $BOXPATH/run_kannel_box
    echo "."
    ;;

  reload)
    # We don't have support for this yet.
    exit 1
    ;;

  restart|force-reload)
    $0 stop
    sleep 1
    $0 start
    ;;

  *)
    echo "Usage: $0 {start|stop|reload|restart|force-reload}"
    exit 1

esac

exit 0
