/*
 * smsc_fake.c - interface to fakesmsc.c
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
#include <limits.h>

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "msg.h"
#include "sms.h"

typedef struct privdata {
    List	*outgoing_queue;
    long	connection_thread;
    int		shutdown; /* Signal to the connection thread to shut down */
    int		listening_socket; /* File descriptor */
    int		port;		  /* Port number to listen */
    Octstr	*allow_ip, *deny_ip;
} PrivData;


static int fake_open_connection(PrivData *privdata)
{
    int s;

    if ((s = make_server_socket(privdata->port, NULL)) == -1) {
	    /* XXX add interface_name if required */
        error(0, "smsc_fake: could not create listening socket in port %d",
	          privdata->port);
        return -1;
    }
    if (socket_set_blocking(s, 0) == -1) {
        error(0, "smsc_fake: couldn't make listening socket port %d non-blocking",
	          privdata->port);
        return -1;
    }
    privdata->listening_socket = s;
    return 0;
}


static int sms_to_client(Connection *client, Msg *msg)
{
    Octstr *line;
    Octstr *msgdata = NULL; /* NULL to allow octstr_destroy */
    char *contents;
    int len;

    debug("bb.sms", 0, "smsc_fake: sending message to client");

    line = octstr_duplicate(msg->sms.sender);
    octstr_append_char(line, ' ');
    octstr_append(line, msg->sms.receiver);
    if (octstr_len(msg->sms.udhdata)) {
        octstr_append(line, octstr_imm(" udh "));
        msgdata = octstr_duplicate(msg->sms.udhdata);
        octstr_url_encode(msgdata);
        octstr_append(line, msgdata);
        octstr_destroy(msgdata);
        octstr_append_char(line, ' ');
        msgdata = octstr_duplicate(msg->sms.msgdata);
        octstr_url_encode(msgdata);
        octstr_append(line, msgdata);
    } else {
        contents = octstr_get_cstr(msg->sms.msgdata);
        len = octstr_len(msg->sms.msgdata);
        while (len > 0) {
            len--;
            if (contents[len] < 32 || contents[len] > 126) {
                octstr_append(line, octstr_imm(" data "));
                msgdata = octstr_duplicate(msg->sms.msgdata);
                octstr_url_encode(msgdata);
                octstr_append(line, msgdata);
                goto notelse; /* C lacks "else" clause for while loops */
            }
        }
        octstr_append(line, octstr_imm(" text "));
        octstr_append(line, msg->sms.msgdata);
    }

notelse:
    octstr_append_char(line, 10);

    if (conn_write(client, line) == -1) {
        octstr_destroy(msgdata);
        octstr_destroy(line);
        return -1;
    }
    octstr_destroy(msgdata);
    octstr_destroy(line);
    return 1;
}


static void msg_to_bb(SMSCConn *conn, Octstr *line)
{
    long p, p2;
    Msg *msg;
    Octstr *type = NULL; /* might be destroyed after error before created */

    msg = msg_create(sms);
    p = octstr_search_char(line, ' ', 0);
    if (p == -1)
        goto error;
    msg->sms.sender = octstr_copy(line, 0, p);
    p2 = octstr_search_char(line, ' ', p + 1);
    if (p2 == -1)
        goto error;
    msg->sms.receiver = octstr_copy(line, p + 1, p2 - p - 1);
    p = octstr_search_char(line, ' ', p2 + 1);
    if (p == -1)
        goto error;
    type = octstr_copy(line, p2 + 1, p - p2 - 1);
    if (!octstr_compare(type, octstr_imm("text")))
        msg->sms.msgdata = octstr_copy(line, p + 1, LONG_MAX);
    else if (!octstr_compare(type, octstr_imm("data"))) {
        msg->sms.msgdata = octstr_copy(line, p + 1, LONG_MAX);
        if (octstr_url_decode(msg->sms.msgdata) == -1)
            warning(0, "smsc_fake: urlcoded data from client looks malformed");
    }
    else if (!octstr_compare(type, octstr_imm("udh"))) {
        p2 = octstr_search_char(line, ' ', p + 1);
        if (p2 == -1)
            goto error;
        msg->sms.udhdata = octstr_copy(line, p + 1, p2 - p - 1);
        msg->sms.msgdata = octstr_copy(line, p2 + 1, LONG_MAX);
        if (msg->sms.coding == DC_UNDEF)
            msg->sms.coding = DC_8BIT;;
        if (octstr_url_decode(msg->sms.msgdata) == -1 ||
            octstr_url_decode(msg->sms.udhdata) == -1)
            warning(0, "smsc_fake: urlcoded data from client looks malformed");
    } else
        goto error;
    octstr_destroy(line);
    octstr_destroy(type);
    time(&msg->sms.time);
    msg->sms.smsc_id = octstr_duplicate(conn->id);

    debug("bb.sms", 0, "smsc_fake: new message received");
    bb_smscconn_receive(conn, msg);
    return;
error:
    warning(0, "smsc_fake: invalid message syntax from client, ignored");
    msg_destroy(msg);
    octstr_destroy(line);
    octstr_destroy(type);
    return;
}


static void main_connection_loop(SMSCConn *conn, Connection *client)
{
    PrivData *privdata = conn->data;
    Octstr *line;
    Msg	*msg;

    while (1) {
        while (!conn->is_stopped && !privdata->shutdown &&
                (line = conn_read_line(client)))
            msg_to_bb(conn, line);
        if (conn_read_error(client))
            goto error;
        if (conn_eof(client))
            goto eof;

        while ((msg = list_extract_first(privdata->outgoing_queue)) != NULL) {
            if (sms_to_client(client, msg) == 1) {
                /* 
                 * Actually no quarantee of it having been really sent,
                 * but I suppose that doesn't matter since this interface
                 * is just for debugging anyway 
                 */
                bb_smscconn_sent(conn, msg);
            } else {
                bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_REJECTED);
                goto error;
            }
        }
        if (privdata->shutdown) {
            debug("bb.sms", 0, "smsc_fake shutting down, closing client socket");
            conn_destroy(client);
            return;
        }
        conn_wait(client, -1);
        if (conn_read_error(client))
            goto error;
        if (conn_eof(client))
            goto eof;
    }
error:
    info(0, "IO error to fakesmsc client. Closing connection.");
    conn_destroy(client);
    return;
eof:
    info(0, "EOF from fakesmsc client. Closing connection.");
    conn_destroy(client);
    return;
}


static void fake_listener(void *arg)
{
    SMSCConn *conn = arg;
    PrivData *privdata = conn->data;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    Octstr *ip;
    Connection *client;
    int s, ret;
    Msg	*msg;

    while (1) {
        client_addr_len = sizeof(client_addr);
        ret = gwthread_pollfd(privdata->listening_socket, POLLIN, -1);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            error(0, "Poll for fakesmsc connections failed, shutting down");
            break;
        }
        if (privdata->shutdown)
            break;
        if (ret == 0) 
            /* 
             * This thread was woken up from elsewhere, but
             * if we're not shutting down nothing to do here. 
             */
            continue;
        s = accept(privdata->listening_socket, (struct sockaddr *)&client_addr,
                   &client_addr_len);
        if (s == -1) {
            warning(errno, "fake_listener: accept() failed, retrying...");
            continue;
        }
        ip = host_ip(client_addr);
        if (!is_allowed_ip(privdata->allow_ip, privdata->deny_ip, ip)) {
            info(0, "Fakesmsc connection tried from denied host <%s>,"
                 " disconnected", octstr_get_cstr(ip));
            octstr_destroy(ip);
            close(s);
            continue;
        }
        client = conn_wrap_fd(s, 0);
        if (client == NULL) {
            error(0, "fake_listener: conn_wrap_fd failed on accept()ed fd");
            octstr_destroy(ip);
            close(s);
            continue;
        }
        conn_claim(client);
        info(0, "Fakesmsc client connected from %s", octstr_get_cstr(ip));
        octstr_destroy(ip);
        mutex_lock(conn->flow_mutex);
        conn->status = SMSCCONN_ACTIVE;
        conn->connect_time = time(NULL);
        mutex_unlock(conn->flow_mutex);
        bb_smscconn_connected(conn);

        main_connection_loop(conn, client);

        if (privdata->shutdown)
            break;
        mutex_lock(conn->flow_mutex);
        conn->status = SMSCCONN_RECONNECTING;
        mutex_unlock(conn->flow_mutex);
        while ((msg = list_extract_first(privdata->outgoing_queue)) != NULL) {
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_TEMPORARILY);
        }
    }
    if (close(privdata->listening_socket) == -1)
        warning(errno, "smsc_fake: couldn't close listening socket at shutdown");
    mutex_lock(conn->flow_mutex);

    conn->status = SMSCCONN_DEAD;

    while ((msg = list_extract_first(privdata->outgoing_queue)) != NULL) {
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN);
    }
    list_destroy(privdata->outgoing_queue, NULL);
    octstr_destroy(privdata->allow_ip);
    octstr_destroy(privdata->deny_ip);
    gw_free(privdata);
    conn->data = NULL;

    mutex_unlock(conn->flow_mutex);
    debug("bb.sms", 0, "smsc_fake connection has completed shutdown.");
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

    debug("bb.sms", 0, "Shutting down SMSCConn FAKE, %s",
          finish_sending ? "slow" : "instant");

    /* 
     * Documentation claims this would have been done by smscconn.c,
     * but isn't when this code is being written. 
     */
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    privdata->shutdown = 1; 
    /*
     * Separate from why_killed to avoid locking, as
     * why_killed may be changed from outside? 
     */

    if (finish_sending == 0) {
        Msg *msg;
        while((msg = list_extract_first(privdata->outgoing_queue)) != NULL) {
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN);
        }
    }

    gwthread_wakeup(privdata->connection_thread);
    return 0;
}


static void start_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;

    /* in case there are messages in the buffer already */
    gwthread_wakeup(privdata->connection_thread);
    debug("bb.sms", 0, "smsc_fake: start called");
}


static long queued_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;
    long ret = list_len(privdata->outgoing_queue);

    /* use internal queue as load, maybe something else later */

    conn->load = ret;
    return ret;
}


int smsc_fake_create(SMSCConn *conn, CfgGroup *cfg)
{
    PrivData *privdata = NULL;
    Octstr *allow_ip, *deny_ip;
    long portno;   /* has to be long because of cfg_get_integer */

    if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1)
        portno = 0;
    allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    if (allow_ip)
        deny_ip = octstr_create("*.*.*.*");
    else
        deny_ip = NULL;

    if (portno == 0) {
        error(0, "'port' invalid in 'fake' record.");
        goto error;
    }
    privdata = gw_malloc(sizeof(PrivData));
    privdata->listening_socket = -1;

    privdata->port = portno;
    privdata->allow_ip = allow_ip;
    privdata->deny_ip = deny_ip;

    if (fake_open_connection(privdata) < 0) {
        gw_free(privdata);
        privdata = NULL;
        goto error;
    }

    conn->data = privdata;

    conn->name = octstr_format("FAKE:%d", privdata->port);

    privdata->outgoing_queue = list_create();
    privdata->shutdown = 0;

    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = time(NULL);

    if ((privdata->connection_thread = gwthread_create(fake_listener, conn)) == -1)
        goto error;

    conn->shutdown = shutdown_cb;
    conn->queued = queued_cb;
    conn->start_conn = start_cb;
    conn->send_msg = add_msg_cb;

    return 0;

error:
    error(0, "Failed to create fake smsc connection");
    if (privdata != NULL) {
        list_destroy(privdata->outgoing_queue, NULL);
        if (close(privdata->listening_socket == -1)) {
            error(errno, "smsc_fake: closing listening socket port %d failed",
                  privdata->listening_socket);
        }
    }
    gw_free(privdata);
    octstr_destroy(allow_ip);
    octstr_destroy(deny_ip);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    return -1;
}
