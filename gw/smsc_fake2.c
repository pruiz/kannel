/*
 * smsc_fake2.c - interface to fakesmsc2.c
 *
 * Uoti Urpala 2001
 */

/* Doesn't support multi-send
 * Doesn't warn about unrecognized configuration variables */

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "msg.h"

typedef struct privdata {
    List	*outgoing_queue;
    long	connection_thread;

    int		shutdown;

    int		listening_socket;

    int port;
} PrivData;


static int fake2_open_connection(PrivData *privdata)
{
    int s;

    if ( (s = make_server_socket(privdata->port)) == -1) {
	error(0, "Fake2: could not create listening socket in port %d",
	      privdata->port);
	return -1;
    }
    if (socket_set_blocking(s, 0) == -1) {
	error(0, "Fake2: couldn't make listening socket port %d non-blocking",
	      privdata->port);
	return -1;
    }
    privdata->listening_socket = s;
    return 0;
}


/*--------------------------------------------------------------------
 * TODO: WAP WDP functions!
 */


static PrivData *fake2_smsc_open(ConfigGroup *grp)
{
    PrivData *privdata;
    char *port;

    int portno;

    port = config_get(grp, "port");
    portno = (port != NULL ? atoi(port) : 0);

    if (portno == 0) {
	error(0, "'port' invalid in 'fake2' record.");
	return NULL;
    }
    privdata = gw_malloc(sizeof(PrivData));
    privdata->listening_socket = -1;

    privdata->port = portno;

    if (fake2_open_connection(privdata) < 0) {
	gw_free(privdata);
	return NULL;
    }
    info(0, "Fake2 open successfully done");

    return privdata;
}


static int sms_to_client(Connection *client, Msg *msg)
{
    char ten = 10;

    debug("bb.sms", 0, "smsc_fake2: sending message to client");

    if (conn_write(client, msg->sms.sender) == -1 ||
	conn_write_data(client, " ", 1) == -1 ||
	conn_write(client, msg->sms.receiver) == -1 ||
	conn_write_data(client, " ", 1) == -1 ||
	conn_write(client, msg->sms.msgdata) == -1 ||
	conn_write_data(client, &ten, 1) == - 1)
	return -1;
    return 1;
}


static void msg_to_bb(SMSCConn *conn, Octstr *line)
{
    long p, p2;
    Msg *msg;

    msg = msg_create(sms);
    p = octstr_search_char(line, ' ', 0);
    if (p == -1) {
	msg->sms.sender = octstr_duplicate(line);
	msg->sms.receiver = octstr_create("");
	msg->sms.msgdata = octstr_create("");
    }
    else {
	msg->sms.sender = octstr_copy(line, 0, p);
	p2 = octstr_search_char(line, ' ', p + 1);
	if (p2 == -1) {
	    msg->sms.receiver = octstr_copy(line, p + 1, LONG_MAX);
	    msg->sms.msgdata = octstr_create("");
	}
	else {
	    msg->sms.receiver = octstr_copy(line, p + 1, p2 - p - 1);
	    msg->sms.msgdata = octstr_copy(line, p2 + 1, LONG_MAX);
	}
    }
    octstr_destroy(line);
    time(&msg->sms.time);
    msg->sms.smsc_id = octstr_duplicate(conn->id);

    debug("bb.sms", 0, "fake2: new message received");
    counter_increase(conn->received);
    bb_smscconn_receive(conn, msg);
}


static void main_connection_loop(SMSCConn *conn, Connection *client)
{
    PrivData *privdata = conn->data;
    Octstr	*line;
    Msg		*msg;

    while (1) {
	while (!conn->is_stopped && !privdata->shutdown &&
	       (line = conn_read_line(client)))
	    msg_to_bb(conn, line);
	if (conn_read_error(client))
	    goto error;
	if (conn_eof(client))
	    goto eof;

	while ( (msg = list_extract_first(privdata->outgoing_queue)) != NULL) {
	    if (sms_to_client(client, msg) == 1) {
		/* Actually no quarantee of it having been really sent,
		   but I suppose that doesn't matter since this interface
		   is just for debugging anyway */
		counter_increase(conn->sent);
		bb_smscconn_sent(conn, msg);
	    }
	    else {
		counter_increase(conn->failed);
		bb_smscconn_send_failed(conn, msg,
					SMSCCONN_FAILED_REJECTED);
		goto error;
	    }
	}
	if (privdata->shutdown)
	    return;
	conn_wait(client, -1);
	if (conn_read_error(client))
	    goto error;
	if (conn_eof(client))
	    goto eof;
    }
error:
    info(0, "IO error to fake2 client. Closing connection.");
    conn_destroy(client);
    return;
eof:
    info(0, "EOF from fake2 client. Closing connection.");
    conn_destroy(client);
    return;
}


static void fake2_connection(void *arg)
{
    SMSCConn	*conn = arg;
    PrivData	*privdata = conn->data;
    struct sockaddr_in client_addr;
    socklen_t	client_addr_len;
    Connection	*client;
    int 	s, ret;
    Msg		*msg;

    while (1) {
	client_addr_len = sizeof(client_addr);
	ret = gwthread_pollfd(privdata->listening_socket, POLLIN, -1);
	if (ret == -1) {	/* This should be very unlikely? */
	    error(0, "Poll for fake2 connections failed, shutting down");
	    break;
	}
	if (privdata->shutdown)
	    break;
	if (ret == 0) /* This thread was woken up from elsewhere, but
			 if we're not shutting down nothing to do here. */
	    continue;
	s = accept(privdata->listening_socket, &client_addr, &client_addr_len);
	if (s == -1) {
	    warning(errno, "fake2_connection: accept() failed, retrying...");
	    continue;
	}
	client = conn_wrap_fd(s);
	if (client == NULL) {
	    error(0, "fake2_connection: conn_wrap_fd failed on accept()ed fd");
	    close(s);
	    continue;
	}
	conn_claim(client);
	info(0, "Fake2 SMSC client connected");
	mutex_lock(conn->flow_mutex);
	conn->status = SMSCCONN_ACTIVE;
	conn->connect_time = time(NULL);
	mutex_unlock(conn->flow_mutex);

	main_connection_loop(conn, client);

	if (privdata->shutdown)
	    break;
	mutex_lock(conn->flow_mutex);
	conn->status = SMSCCONN_RECONNECTING;
	mutex_unlock(conn->flow_mutex);
    }
    if (close(privdata->listening_socket) == -1)
	warning(errno, "Fake2: couldn't close listening socket at shutdown");
    mutex_lock(conn->flow_mutex);

    conn->status = SMSCCONN_DEAD;

    while((msg = list_extract_first(privdata->outgoing_queue)) != NULL) {
	bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN);
    }
    list_destroy(privdata->outgoing_queue, NULL);
    /* !@# Check to see whether there will be more resources to free
       from privdata later */
    gw_free(privdata);
    conn->data = NULL;

    mutex_unlock(conn->flow_mutex);
    debug("bb.sms", 0, "Fake2 SMSC connection has completed shutdown.");
    bb_smscconn_killed();
}


static int add_msg_cb(SMSCConn *conn, Msg *sms)
{
    PrivData *privdata = conn->data;
    Msg *copy;

    copy = msg_duplicate(sms);
    list_produce(privdata->outgoing_queue, copy);
    gwthread_wakeup(privdata->connection_thread);

    return 0;
}


static int shutdown_cb(SMSCConn *conn, int finish_sending)
{
    PrivData *privdata = conn->data;

    debug("bb.sms", 0, "Shutting down SMSCConn FAKE2, %s",
	  finish_sending ? "slow" : "instant");

    if (finish_sending == 0) {
	Msg *msg;
	while((msg = list_extract_first(privdata->outgoing_queue))!=NULL) {
	    bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN);
	}
    }

    /* Documentation claims this would have been done by smscconn.c,
       but isn't when this code is being written. */
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    privdata->shutdown = 1; /* Separate from why_killed to avoid locking, as
			   why_killed may be changed from outside? */
    gwthread_wakeup(privdata->connection_thread);
    return 0;
}


static void start_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;

    /* in case there are messages in the buffer already */
    gwthread_wakeup(privdata->connection_thread);
    debug("bb.sms", 0, "FAKE2: start called");
}


static long queued_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;
    long ret = list_len(privdata->outgoing_queue);

    /* use internal queue as load, maybe something else later */

    conn->load = ret;
    return ret;
}


int smsc_fake2_create(SMSCConn *conn, ConfigGroup *cfg)
{
    PrivData *privdata;

    conn->send_msg = add_msg_cb;

    if ( (privdata = fake2_smsc_open(cfg)) == NULL)
	goto error;

    conn->data = privdata;

    conn->name = octstr_format("FAKE2:%d", privdata->port);

    privdata->outgoing_queue = list_create();
    privdata->shutdown = 0;

    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = time(NULL);

    if ( (privdata->connection_thread = gwthread_create(fake2_connection, conn))
	 == -1)
	goto error;

    conn->shutdown = shutdown_cb;
    conn->queued = queued_cb;
    conn->start_conn = start_cb;

    return 0;

error:
    error(0, "Failed to create fake2 smsc connection");
    if (privdata != NULL) {
	list_destroy(privdata->outgoing_queue, NULL);
	if (close(privdata->listening_socket == -1)) {
	    error(errno, "Fake2: closing listening socket port %d failed",
		  privdata->listening_socket);
	}
    }
    gw_free(privdata);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    return -1;
}
