/*
 * smsc_emi2.c - interface to EMI SMS centers
 *
 * Uoti Urpala 2001
 */

/* Doesn't warn about unrecognized configuration variables */
/* The EMI specification doesn't document how connections should be
 * opened/used. The way they currently work might need to be changed. */

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <float.h>

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "msg.h"
#include "sms.h"
#include "emimsg.h"
#include "dlr.h"

#define EMI2_MAX_TRN 100

typedef struct privdata {
    List	*outgoing_queue;
    long	receiver_thread;
    long	sender_thread;
    int		retry;	  	/* Retry always to connect to smsc */
    int		shutdown;	  /* Internal signal to shut down */
    int		listening_socket; /* File descriptor */
    int		send_socket;
    int		port;		  /* SMSC port */
    int		our_port;	  /* Optional local port number in which to
				   * bind our end of send connection */
    int		rport;		  /* Receive-port to listen */
    Octstr	*allow_ip, *deny_ip;
    Octstr	*host, *username, *password;
    int		unacked;	/* Sent messages not acked */
    struct {
	time_t	sendtime;	/* When we sent out a message with a given
				 * TRN. Is 0 if the TRN slot is currently free. */
	int     sendtype;	/* OT of message, undefined if time == 0 */
	int	dlr;            /* dlr = DLR_SMSC_SUCCESS || DLR_SMSC_FAIL */
	Msg     *sendmsg; 	/* Corresponding message for OT == 51 */
    } slots[EMI2_MAX_TRN];
    int		keepalive; 	/* Seconds to send a Keepalive Command (OT=31) */
    int		flowcontrol;	/* 0=Windowing, 1=Stop-and-Wait */
    int		waitack;	/* Seconds to wait to ack */
    int		throughput;	/* Messages per second */
    int		window;		/* In windowed flow-control, the window size */
    int         can_write;      /* write = 1, read = 0, for stop-and-wait flow control */
    int         priv_nexttrn;   /* next TRN, this should never be accessed directly.
				 * use int emi2_next_trn (SMSCConn *conn) instead.
				 */
    time_t	last_activity_time; /* the last time something was sent over the main
				     * SMSC connection
				     */
    time_t      check_time;
    int         idle_timeout;   /* Seconds a Main connection to the SMSC is allowed to be idle.
				   If 0, no idle timeout is in effect */
} PrivData;

typedef enum {
    EMI2_SENDREQ,  /* somebody asked this driver to send a SMS message */
    EMI2_SMSCREQ,  /* the SMSC wants something from us */
    EMI2_CONNERR,  /* an error condition in the SMSC main connection */
    EMI2_TIMEOUT,  /* timeout on the SMSC main connection */
} EMI2Event;

#define PRIVDATA(conn) ((PrivData *)((conn)->data))

#define SLOTBUSY(conn,i) (PRIVDATA(conn)->slots[(i)].sendtime != 0)

#define CONNECTIONIDLE(conn)								\
((PRIVDATA(conn)->unacked == 0) &&							\
 (PRIVDATA(conn)->idle_timeout ?							\
  (PRIVDATA(conn)->last_activity_time + PRIVDATA(conn)->idle_timeout) <= time(0):0))

#define emi2_can_send(conn)					\
((PRIVDATA(conn)->can_write || !PRIVDATA(conn)->flowcontrol) &&	\
 (PRIVDATA(conn)->unacked < PRIVDATA(conn)->window) &&		\
 (!PRIVDATA(conn)->shutdown))

#define emi2_needs_keepalive(conn)							\
(emi2_can_send(conn) &&									\
 (PRIVDATA(conn)->keepalive > 0) &&							\
 (time(NULL) > (PRIVDATA(conn)->last_activity_time + PRIVDATA(conn)->keepalive)))

/*
 * Send an EMI message and update the last_activity_time field.
 */
static int emi2_emimsg_send(SMSCConn *conn, Connection *server, struct emimsg *emimsg)
{
    int result = emimsg_send(server, emimsg);

    if (result >= 0) {
	PRIVDATA(conn)->last_activity_time = time (NULL);
    }

    return result;
}

/* Wait for a message of type 'ot', sent with TRN 0, to be acked.
 * Timeout after 't' seconds. Any other packets received are ignored.
 * This function is meant for initial login packet(s) and testing.
 * Return 1 for positive ACK, 0 for timeout, -1 for broken/closed connection,
 * -2 for negative NACK. */
static int wait_for_ack(PrivData *privdata, Connection *server, int ot, int t)
{
    time_t timeout_time;
    int time_left;
    Octstr *str;
    struct emimsg *emimsg;

    timeout_time = time(NULL) + t;
    while (1) {
	str = conn_read_packet(server, 2, 3);
	if (conn_eof(server)) {
	    error(0, "emi2: connection closed in wait_for_ack");
	    return -1;
	}
	if (conn_read_error(server)) {
	    error(0, "emi2: connection error in wait_for_ack");
	    return -1;
	}
	if (str) {
	    emimsg = get_fields(str);
	    if (emimsg == NULL) {
		octstr_destroy(str);
		continue;
	    }
	    if (emimsg->ot == ot && emimsg->trn == 0 && emimsg->or == 'R') {
		octstr_destroy(str);
		break;
	    }
	    warning(0, "Emi2: ignoring message %s while waiting for ack to"
			"ot:%d trn:%d", octstr_get_cstr(str), ot, 0);
	    emimsg_destroy(emimsg);
	    octstr_destroy(str);
	}
	time_left = timeout_time - time(NULL);
	if (time_left < 0 || privdata->shutdown)
	    return 0;
	conn_wait(server, time_left);
    }
    if (octstr_get_char(emimsg->fields[0], 0) == 'N') {
	emimsg_destroy(emimsg);
	return -2;
    }
    emimsg_destroy(emimsg);
    return 1;
}


static struct emimsg *make_emi31(PrivData *privdata, int trn)
{
    struct emimsg *emimsg;

    emimsg = emimsg_create_op(31, trn);
    emimsg->fields[0] = octstr_duplicate(privdata->username);
    emimsg->fields[1] = octstr_create("0539");
    return emimsg;
}


static struct emimsg *make_emi60(PrivData *privdata)
{
    struct emimsg *emimsg;

    emimsg = emimsg_create_op(60, 0);
    emimsg->fields[E60_OADC] = octstr_duplicate(privdata->username);
    emimsg->fields[E60_OTON] = octstr_create("6");
    emimsg->fields[E60_ONPI] = octstr_create("5");
    emimsg->fields[E60_STYP] = octstr_create("1");
    emimsg->fields[E60_PWD] = octstr_duplicate(privdata->password);
    octstr_binary_to_hex(emimsg->fields[E60_PWD], 1);
    emimsg->fields[E60_VERS] = octstr_create("0100");
    return emimsg;
}


static Connection *open_send_connection(SMSCConn *conn)
{
    PrivData *privdata = conn->data;
    int result, wait;
    struct emimsg *emimsg;
    Connection *server;
    Msg *msg;

    wait = 0;
    while (!privdata->shutdown) {
	/* Change status only if the first attempt to form a
	 * connection fails, as it's possible that the SMSC closed the
	 * connection because of idle timeout and a new one will be
	 * created quickly. */
	if (wait) {
	    if (conn->status == SMSCCONN_ACTIVE) {
		mutex_lock(conn->flow_mutex);
		conn->status = SMSCCONN_RECONNECTING;
		mutex_unlock(conn->flow_mutex);
	    }
	    while ((msg = list_extract_first(privdata->outgoing_queue))) {
		bb_smscconn_send_failed(conn, msg,
					SMSCCONN_FAILED_TEMPORARILY);
	    }
	    info(0, "smsc_emi2: waiting for %d %s before trying to "
		 "connect again", (wait < 60 ? wait : wait/60), 
		 (wait < 60 ? "seconds" : "minutes"));
	    gwthread_sleep(wait);
	    wait = wait > (privdata->retry ? 3600 : 600) ? 
		(privdata->retry ? 3600 : 600) : wait * 2;
	}
	else
	    wait = 15;

	server = conn_open_tcp_with_port(privdata->host, privdata->port,
					 privdata->our_port, NULL
					 /* privdata->our_host */);
	if (privdata->shutdown) {
	    conn_destroy(server);
	    return NULL;
	}
	if (server == NULL) {
	    error(0, "smsc_emi2: opening TCP connection to %s failed",
		  octstr_get_cstr(privdata->host));
	    continue;
	}

	if (privdata->username && privdata->password) {
	    emimsg = make_emi60(privdata);
	    emi2_emimsg_send(conn, server, emimsg);
	    emimsg_destroy(emimsg);
	    result = wait_for_ack(privdata, server, 60, 30);
	    if (result == -2) {
		/* Are SMSCs going to return any temporary errors? If so,
		 * testing for those error codes should be added here. */
		error(0, "smsc_emi2: Server rejected our login, giving up");
		conn->why_killed = SMSCCONN_KILLED_WRONG_PASSWORD;
		conn_destroy(server);
		if(! privdata->retry) 
		    return NULL;
		else
		    continue;
	    }
	    else if (result == 0) {
		error(0, "smsc_emi2: Got no reply to login attempt "
		      "within 30 s");
		conn_destroy(server);
		continue;
	    }
	    else if (result == -1) { /* Broken connection, already logged */
		conn_destroy(server);
		continue;
	    }
	}

	if (conn->status != SMSCCONN_ACTIVE) {
	    mutex_lock(conn->flow_mutex);
	    conn->status = SMSCCONN_ACTIVE;
	    conn->connect_time = time(NULL);
	    mutex_unlock(conn->flow_mutex);
	    bb_smscconn_connected(conn);
	}
	return server;
    }
    return NULL;
}


static void pack_7bit(Octstr *str)
{
    Octstr *result;
    int len, i;
    int numbits, value;

    result = octstr_create("0");
    len = octstr_len(str);
    value = 0;
    numbits = 0;
    for (i = 0; i < len; i++) {
	value += octstr_get_char(str, i) << numbits;
	numbits += 7;
	if (numbits >= 8) {
	    octstr_append_char(result, value & 0xff);
	    value >>= 8;
	    numbits -= 8;
	}
    }
    if (numbits > 0)
	octstr_append_char(result, value);
    octstr_set_char(result, 0, (len * 7 + 3) / 4);
    octstr_delete(str, 0, LONG_MAX);
    octstr_append(str, result);
    octstr_binary_to_hex(str, 1);
    octstr_destroy(result);
}


static struct emimsg *msg_to_emimsg(Msg *msg, int trn)
{
    Octstr *str;
    struct emimsg *emimsg;
    int dcs;
    struct tm tm;
    char p[20];

    emimsg = emimsg_create_op(51, trn);
    str = octstr_duplicate(msg->sms.sender);
    if(octstr_get_char(str,0) == '+') {
    	/* either alphanum or international */
    	if (!octstr_check_range(str, 1, 256, gw_isdigit)) {
	    /* alphanumeric sender address with + in front*/
	    charset_latin1_to_gsm(str);
	    octstr_truncate(str, 11); /* max length of alphanumeric OaDC */
	    emimsg->fields[E50_OTOA] = octstr_create("5039");
	    pack_7bit(str);
	}
	else {
	    /* international number. Set format and remove + */
	    emimsg->fields[E50_OTOA] = octstr_create("1139");
	    octstr_delete(str, 0, 1);
	    octstr_truncate(str, 22);  /* max length of numeric OaDC */
	}
    }
    else {
	if (!octstr_check_range(str, 0, 256, gw_isdigit)) {
	    /* alphanumeric sender address */
            charset_latin1_to_gsm(str);
	    octstr_truncate(str, 11); /* max length of alphanumeric OaDC */
	    emimsg->fields[E50_OTOA] = octstr_create("5039");
	    pack_7bit(str);
	}
    }
 
    emimsg->fields[E50_OADC] = str;

    str = octstr_duplicate(msg->sms.receiver);
    if(octstr_get_char(str,0) == '+') {
    	 /* international number format */
    	 /* EMI doesnt understand + so we have to replace it with something useful */
    	 /* we try 00 here. Should really be done in the config instead so this */
    	 /* is only a workaround to make wrong configs work  */
	 octstr_delete(str, 0, 1);
	 octstr_insert_data(str, 0, "00",2);
    }
    octstr_truncate(str, 16); /* max length of ADC */
    emimsg->fields[E50_ADC] = str;
   
    emimsg->fields[E50_XSER] = octstr_create("");

    /* XSer1: UDH */
    if (octstr_len(msg->sms.udhdata)) {
	str = octstr_create("");
	octstr_append_char(str, 1);
	octstr_append_char(str, octstr_len(msg->sms.udhdata));
	octstr_append(str, msg->sms.udhdata);
	octstr_binary_to_hex(str, 1);
	octstr_append(emimsg->fields[E50_XSER],str);
        octstr_destroy(str);
    }
	
    /* XSer2: DCS */
    if ((dcs = fields_to_dcs(msg, 0))) {
   	str = octstr_create("");
	octstr_append_char(str, 2); 
	octstr_append_char(str, 1); /* len 01 */
	octstr_append_char(str, dcs);
	octstr_binary_to_hex(str, 1);
	octstr_append(emimsg->fields[E50_XSER],str);
	octstr_destroy(str);
    }

    if (msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2) {
	emimsg->fields[E50_MT] = octstr_create("4");
	emimsg->fields[E50_MCLS] = octstr_create("1");
	str = octstr_duplicate(msg->sms.msgdata);
	emimsg->fields[E50_NB] =
	    octstr_format("%04d", 8 * octstr_len(str));
	octstr_binary_to_hex(str, 1);
	emimsg->fields[E50_TMSG] = str;
    }
    else {
	emimsg->fields[E50_MT] = octstr_create("3");
	str = octstr_duplicate(msg->sms.msgdata);
	charset_latin1_to_gsm(str);
	/* Could still be too long after truncation if there's an UDH part,
	 * but this is only to notice errors elsewhere (should never happen).*/
	if (charset_gsm_truncate(str, 160))
	    error(0, "emi2: Message to send is longer "
		  "than 160 gsm characters");
	octstr_binary_to_hex(str, 1);
	emimsg->fields[E50_AMSG] = str;
    }

    if (msg->sms.validity) {
	tm = gw_localtime(time(NULL) + msg->sms.validity * 60);
	sprintf(p, "%02d%02d%02d%02d%02d",
	    tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100, 
	    tm.tm_hour, tm.tm_min);
	str = octstr_create(p);
	emimsg->fields[E50_VP] = str;
    }
    if (msg->sms.deferred) {
	str = octstr_create("1");
	emimsg->fields[E50_DD] = str;
	tm = gw_localtime(time(NULL) + msg->sms.deferred * 60);
	sprintf(p, "%02d%02d%02d%02d%02d",
	    tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100, 
	    tm.tm_hour, tm.tm_min);
	str = octstr_create(p);
	emimsg->fields[E50_DDT] = str;
    }

    /* if delivery reports are asked, lets ask for them too */
    /* even the sender might not be interested in delivery or non delivery */
    /* we still need them back to clear out the memory after the message */
    /* has been delivered or non delivery has been confirmed */
    if (msg->sms.dlr_mask & 0x07) {
    	emimsg->fields[E50_NRQ] = octstr_create("1");
	emimsg->fields[E50_NT] = octstr_create("");
	octstr_append_decimal(emimsg->fields[E50_NT], 3 + (msg->sms.dlr_mask & 0x04)); 
    }
    return emimsg;
}


/* Return -1 if the connection broke, 0 if the request couldn't be handled
 * (unknown type), or 1 if everything was successful */
static int handle_operation(SMSCConn *conn, Connection *server,
			   struct emimsg *emimsg)
{
    struct emimsg *reply;
    Octstr *tempstr, *xser;
    int type, len;
    Msg *msg = NULL;
    struct universaltime unitime;
    int st_code;

  
    switch(emimsg->ot) {
    case 01:
	msg = msg_create(sms);
	if (emimsg->fields[E01_AMSG] == NULL)
	    emimsg->fields[E01_AMSG] = octstr_create("");
	else if (octstr_hex_to_binary(emimsg->fields[E01_AMSG]) == -1)
	    warning(0, "emi2: Couldn't decode message text");

	if (emimsg->fields[E01_MT] == NULL) {
	    warning(0, "emi2: required field MT missing");
	    /* This guess could be incorrect, maybe the message should just
	       be dropped */
	    emimsg->fields[E01_MT] = octstr_create("3");
	}

	if (octstr_get_char(emimsg->fields[E01_MT], 0) == '3') {
	    msg->sms.msgdata = emimsg->fields[E01_AMSG];
	    emimsg->fields[E01_AMSG] = NULL; /* So it's not freed */
	    charset_gsm_to_latin1(msg->sms.msgdata);
	}
	else {
	    error(0, "emi2: MT == %s isn't supported for operation type 01",
		  octstr_get_cstr(emimsg->fields[E01_MT]));
	    msg->sms.msgdata = octstr_create("");
	}

	msg->sms.sender = octstr_duplicate(emimsg->fields[E01_OADC]);
	if (msg->sms.sender == NULL) {
	    warning(0, "Empty sender field in received message");
	    msg->sms.sender = octstr_create("");
	}

	msg->sms.receiver = octstr_duplicate(emimsg->fields[E01_ADC]);
	if (msg->sms.sender == NULL) {
	    warning(0, "Empty receiver field in received message");
	    msg->sms.receiver = octstr_create("");
	}

	/* Operation type 01 doesn't have a time stamp field */
	time(&msg->sms.time);

	msg->sms.smsc_id = octstr_duplicate(conn->id);
	bb_smscconn_receive(conn, msg);
	reply = emimsg_create_reply(01, emimsg->trn, 1);
	if (emi2_emimsg_send(conn, server, reply) < 0) {
	    emimsg_destroy(reply);
	    return -1;
	}
	emimsg_destroy(reply);
	return 1;

    case 52:
	msg = msg_create(sms);
	/* AMSG is the same field as TMSG */
	if (emimsg->fields[E50_AMSG] == NULL)
	    emimsg->fields[E50_AMSG] = octstr_create("");
	else if (octstr_hex_to_binary(emimsg->fields[E50_AMSG]) == -1)
	    warning(0, "emi2: Couldn't decode message text");

	xser = emimsg->fields[E50_XSER];
	while (octstr_len(xser) > 0) {
	    tempstr = octstr_copy(xser, 0, 4);
	    if (octstr_hex_to_binary(tempstr) == -1)
		error(0, "Invalid XSer");
	    type = octstr_get_char(tempstr, 0);
	    len = octstr_get_char(tempstr, 1);
	    octstr_destroy(tempstr);
	    if (len < 0) {
		error(0, "Malformed emi XSer field");
		break;
	    }
	    if (type != 1 && type != 2)
		warning(0, "Unsupported EMI XSer field %d", type);
	    else {
		if (type == 1) {
		    tempstr = octstr_copy(xser, 4, len * 2);
		    if (octstr_hex_to_binary(tempstr) == -1)
			error(0, "Invalid UDH contents");
		    msg->sms.udhdata = tempstr;
		}
		if (type == 2) {
		    int dcs;
		    tempstr = octstr_copy(xser, 4, 2);
		    octstr_hex_to_binary(tempstr);
		    dcs = octstr_get_char(tempstr, 0);
		    octstr_destroy(tempstr);
		    if (! dcs_to_fields(&msg, dcs)) {
			error(0, "emi2: invalid dcs received");
			/* XXX Should we discard message ? */
			dcs_to_fields(&msg, 0);
		    }
		}
	    }
	    octstr_delete(xser, 0, 2 * len + 4);
	}

	if (emimsg->fields[E50_MT] == NULL) {
	    warning(0, "emi2: required field MT missing");
	    /* This guess could be incorrect, maybe the message should just
	       be dropped */
	    emimsg->fields[E50_MT] = octstr_create("3");
	}
	if (octstr_get_char(emimsg->fields[E50_MT], 0) == '3') {
	    msg->sms.msgdata = emimsg->fields[E50_AMSG];
	    emimsg->fields[E50_AMSG] = NULL; /* So it's not freed */
	    charset_gsm_to_latin1(msg->sms.msgdata);
	}
	else if (octstr_get_char(emimsg->fields[E50_MT], 0) == '4') {
	    msg->sms.msgdata = emimsg->fields[E50_TMSG];
	    emimsg->fields[E50_TMSG] = NULL;
	}
	else {
	    error(0, "emi2: MT == %s isn't supported yet",
		  octstr_get_cstr(emimsg->fields[E50_MT]));
	    msg->sms.msgdata = octstr_create("");
	}

	msg->sms.sender = octstr_duplicate(emimsg->fields[E50_OADC]);
	if (msg->sms.sender == NULL) {
	    warning(0, "Empty sender field in received message");
	    msg->sms.sender = octstr_create("");
	}

	msg->sms.receiver = octstr_duplicate(emimsg->fields[E50_ADC]);
	if (msg->sms.sender == NULL) {
	    warning(0, "Empty receiver field in received message");
	    msg->sms.receiver = octstr_create("");
	}

	tempstr = emimsg->fields[E50_SCTS]; /* Just a shorter name */
	if (tempstr == NULL) {
	    warning(0, "Received EMI message doesn't have required timestamp");
	    goto notime;
	}
	if (octstr_len(tempstr) != 12) {
	    warning(0, "EMI SCTS field must have length 12, now %ld",
		  octstr_len(tempstr));
	    goto notime;
	}
	if (octstr_parse_long(&unitime.second, tempstr, 10, 10) != 12 ||
	    (octstr_delete(tempstr, 10, 2),
	     octstr_parse_long(&unitime.minute, tempstr, 8, 10) != 10) ||
	    (octstr_delete(tempstr, 8, 2),
	     octstr_parse_long(&unitime.hour, tempstr, 6, 10) != 8) ||
	    (octstr_delete(tempstr, 6, 2),
	     octstr_parse_long(&unitime.year, tempstr, 4, 10) != 6) ||
	    (octstr_delete(tempstr, 4, 2),
	     octstr_parse_long(&unitime.month, tempstr, 2, 10) != 4) ||
	    (octstr_delete(tempstr, 2, 2),
	     octstr_parse_long(&unitime.day, tempstr, 0, 10) != 2)) {
	    error(0, "EMI delivery time stamp looks malformed");
	notime:
	    time(&msg->sms.time);
	}
	else {
	    unitime.year += 2000; /* Conversion function expects full year */
	    msg->sms.time = date_convert_universal(&unitime);
	}

	msg->sms.smsc_id = octstr_duplicate(conn->id);
	counter_increase(conn->received);
	bb_smscconn_receive(conn, msg);
	reply = emimsg_create_reply(52, emimsg->trn, 1);
	if (emi2_emimsg_send(conn, server, reply) < 0) {
	    emimsg_destroy(reply);
	    return -1;
	}
	emimsg_destroy(reply);
	return 1;
    case 53: /* delivery notification */	    
	st_code = atoi(octstr_get_cstr(emimsg->fields[E50_DST]));
	switch(st_code)
	{
	case 0: /* delivered */
		msg = dlr_find(octstr_get_cstr(conn->id), 
			octstr_get_cstr(emimsg->fields[E50_SCTS]), /* timestamp */
			octstr_get_cstr(emimsg->fields[E50_OADC]), /* destination */
			DLR_SUCCESS);
		break;
	case 1: /* buffered */
		msg = dlr_find(octstr_get_cstr(conn->id), 
			octstr_get_cstr(emimsg->fields[E50_SCTS]), /* timestamp */
			octstr_get_cstr(emimsg->fields[E50_OADC]), /* destination */
			DLR_BUFFERED);
		break;
	case 2: /* not delivered */
		msg = dlr_find(octstr_get_cstr(conn->id), 
			octstr_get_cstr(emimsg->fields[E50_SCTS]), /* timestamp */
			octstr_get_cstr(emimsg->fields[E50_OADC]), /* destination */
			DLR_FAIL);
		break;
	}
	if(msg != NULL) {     
	    int idx; 
	    int len;     
	    Octstr *reply;
	    reply = octstr_create("");
	    octstr_append(reply, emimsg->fields[E50_AMSG]);
	    octstr_hex_to_binary(reply);
	    /* having a / in the text breaks it so lets replace it with a space */
	    len = octstr_len(reply);
	    for(idx=0;idx<len;idx++)
	    	if(octstr_get_char(reply,idx)=='/')
	    		octstr_set_char(reply,idx,'.');
	    octstr_append_char(reply, '/');
	    octstr_insert(msg->sms.msgdata, reply, 0);
	    octstr_destroy(reply);
	    bb_smscconn_receive(conn, msg);
	}
	reply = emimsg_create_reply(53, emimsg->trn, 1);
	if (emi2_emimsg_send(conn, server, reply) < 0) {
	    emimsg_destroy(reply);
	    return -1;
	}
	emimsg_destroy(reply);
	return 1;

    default:
	error(0, "I don't know how to handle operation type %d", emimsg->ot);
	return 0;
    }
}

/*
 * get all unacknowledged messages from the ringbuffer and queue them
 * for retransmission.
 */
static void clear_sent(PrivData *privdata)
{
    int i;
    debug("smsc.emi2", 0, "clear_sent called");
    for (i = 0; i < EMI2_MAX_TRN; i++) {
	if (privdata->slots[i].sendtime && privdata->slots[i].sendtype == 51)
	    list_produce(privdata->outgoing_queue, privdata->slots[i].sendmsg);
	privdata->slots[i].sendtime = 0;
    }
    privdata->unacked = 0;
}

/*
 * wait seconds seconds for something to happen (a send SMS request, activity
 * on the SMSC main connection, an error or timeout) and tell the caller
 * what happened.
 */
static EMI2Event emi2_wait (SMSCConn *conn, Connection *server, double seconds)
{
    if (list_len(PRIVDATA(conn)->outgoing_queue)) {
	return EMI2_SENDREQ;
    }
    
    if (server != NULL) {
	switch (conn_wait(server, seconds)) {
	case 1: return list_len(PRIVDATA(conn)->outgoing_queue) ? EMI2_SENDREQ : EMI2_TIMEOUT;
	case 0: return EMI2_SMSCREQ;
	default: return EMI2_CONNERR;
	}
    } else {
	gwthread_sleep(seconds);
	return list_len(PRIVDATA(conn)->outgoing_queue) ? EMI2_SENDREQ : EMI2_TIMEOUT;
    }
}

/*
 * obtain the next free TRN.
 */
static int emi2_next_trn (SMSCConn *conn)
{
#define INC_TRN(x) ((x)=((x) + 1) % EMI2_MAX_TRN)
    int result;
    
    while (SLOTBUSY(conn,PRIVDATA(conn)->priv_nexttrn))
	INC_TRN(PRIVDATA(conn)->priv_nexttrn); /* pick unused TRN */
    
    result = PRIVDATA(conn)->priv_nexttrn;
    INC_TRN(PRIVDATA(conn)->priv_nexttrn);

    return result;
#undef INC_TRN
}

/*
 * send an EMI type 31 message when required.
 */
static int emi2_keepalive_handling (SMSCConn *conn, Connection *server)
{
    struct emimsg *emimsg;
    int nexttrn = emi2_next_trn (conn);
    
    emimsg = make_emi31(PRIVDATA(conn), nexttrn);
    PRIVDATA(conn)->slots[nexttrn].sendtype= 31;
    PRIVDATA(conn)->slots[nexttrn].sendtime = time(NULL);
    PRIVDATA(conn)->unacked++;
	
    if (emi2_emimsg_send(conn, server, emimsg) == -1) {
	emimsg_destroy(emimsg);
	return -1;
    }
    emimsg_destroy(emimsg);
	
    PRIVDATA(conn)->can_write = 0;

    return 0;
}


/*
 * the actual send logic: Send all queued messages in a burst.
 */
static int emi2_do_send (SMSCConn *conn, Connection *server)
{
    struct emimsg *emimsg;
    Msg           *msg;
    double         delay = 0;

    if (PRIVDATA(conn)->throughput) {
	delay = 1.0 / PRIVDATA(conn)->throughput;
    }
    
    /* Send messages if there's room in the sending window */
    while (emi2_can_send (conn) &&
	   (msg = list_extract_first(PRIVDATA(conn)->outgoing_queue)) != NULL) {
	int nexttrn = emi2_next_trn (conn);

	if (PRIVDATA(conn)->throughput)
	    gwthread_sleep(delay);

	/* convert the generic Kannel message into an EMI type message */
	emimsg = msg_to_emimsg(msg, nexttrn);

	/* remember the message for retransmission or DLR */
	PRIVDATA(conn)->slots[nexttrn].sendmsg = msg;
	PRIVDATA(conn)->slots[nexttrn].sendtype = 51;
	PRIVDATA(conn)->slots[nexttrn].sendtime = time(NULL);

	/* send the message */
	if (emi2_emimsg_send(conn, server, emimsg) == -1) {
	    emimsg_destroy(emimsg);
	    return -1;
	}

	/* report the submission to the DLR code */
	if (msg->sms.dlr_mask & 0x18) {
	    Octstr *ts;
	    ts = octstr_create("");
	    octstr_append(ts, conn->id);
	    octstr_append_char(ts, '-');
	    octstr_append_decimal(ts, nexttrn);

	    dlr_add(octstr_get_cstr(conn->id), 
		    octstr_get_cstr(ts),
		    octstr_get_cstr(emimsg->fields[E50_ADC]),
		    octstr_get_cstr(msg->sms.service),
		    octstr_get_cstr(msg->sms.dlr_url),
		    msg->sms.dlr_mask);
	    
	    octstr_destroy(ts);
	    PRIVDATA(conn)->slots[nexttrn].dlr = 1;
	} else {
	    PRIVDATA(conn)->slots[nexttrn].dlr = 0;
	}

	/* we just sent a message */
	PRIVDATA(conn)->unacked++;

	emimsg_destroy(emimsg);

	/*
	 * remember that there is an open request for stop-wait flow control
	 * FIXME: couldn't this be done with the unacked field as well? After
	 * all stop-wait is just a window of size 1.
	 */
	PRIVDATA(conn)->can_write = 0;
    }

    return 0;
}

static int emi2_handle_smscreq (SMSCConn *conn, Connection *server)
{
    Octstr	  *str;
    struct emimsg *emimsg;
    
    /* Read acks/nacks/ops from the server */
    while ((str = conn_read_packet(server, 2, 3))) {
	debug("smsc.emi2", 0, "Got packet from the main socket");

	/* parse the msg */
	emimsg = get_fields(str);
	octstr_destroy(str);
	
	if (emimsg == NULL) {
	    continue; /* The parse functions logged errors */
	}
	
	if (emimsg->or == 'O') {
	    /* If the SMSC wants to send operations through this
	     * socket, we'll have to read them because there
	     * might be ACKs too. We just drop them while stopped,
	     * hopefully the SMSC will resend them later. */
	    if (!conn->is_stopped) {
		if (handle_operation(conn, server, emimsg) < 0)
		    return -1; /* Connection broke */
	    } else {
		info(0, "Ignoring operation from main socket "
		     "because the connection is stopped.");
	    }
	} else {   /* Already checked to be 'O' or 'R' */
	    if (!SLOTBUSY(conn,emimsg->trn) ||
		emimsg->ot != PRIVDATA(conn)->slots[emimsg->trn].sendtype) {
		error(0, "Emi2: Got ack for TRN %d, don't remember sending O?", emimsg->trn);
	    } else {
		PRIVDATA(conn)->can_write = 1;
		PRIVDATA(conn)->slots[emimsg->trn].sendtime = 0;
		PRIVDATA(conn)->unacked--;
		
		if (emimsg->ot == 51) {
		    if (PRIVDATA(conn)->slots[emimsg->trn].dlr) {
			Msg *dlrmsg;
			Octstr *ts;
			Msg *origmsg;

			origmsg = PRIVDATA(conn)->slots[emimsg->trn].sendmsg;

			ts = octstr_create("");
			octstr_append(ts, conn->id);
			octstr_append_char(ts, '-');
			octstr_append_decimal(ts, emimsg->trn);

			dlrmsg = dlr_find(octstr_get_cstr(conn->id), 
					  octstr_get_cstr(ts), /* timestamp */
					  octstr_get_cstr(origmsg->sms.receiver), /* destination */
					  (octstr_get_char(emimsg->fields[0], 0) == 'A' ? 
					   DLR_SMSC_SUCCESS : DLR_SMSC_FAIL));

			octstr_destroy(ts);
			if (dlrmsg != NULL) {
			    Octstr *moretext;

			    moretext = octstr_create("");
			    if (octstr_get_char(emimsg->fields[0], 0) == 'N') {
				octstr_append(moretext, emimsg->fields[1]);
				octstr_append_char(moretext, '-');
				octstr_append(moretext, emimsg->fields[2]);
			    }
			    octstr_append_char(moretext, '/');
			    octstr_insert(dlrmsg->sms.msgdata, moretext, 0);
			    octstr_destroy(moretext);

			    bb_smscconn_receive(conn, dlrmsg);
			}
		    }

		    if (octstr_get_char(emimsg->fields[0], 0) == 'A') {
			/* we got an ack back. We might have to store the */
			/* timestamp for delivery notifications now */
			Octstr *ts, *adc;
			int	i;
			Msg *m;
			  
			ts = octstr_duplicate(emimsg->fields[2]);
			if (octstr_len(ts)) {
			    i = octstr_search_char(ts,':',0);
			    if (i>0) {
				octstr_delete(ts,0,i+1);
				adc = octstr_duplicate(emimsg->fields[2]);
				octstr_truncate(adc,i);
			        
				m = PRIVDATA(conn)->slots[emimsg->trn].sendmsg;
				if(m == NULL) {
				    info(0,"uhhh m is NULL, very bad");
				} else if (m->sms.dlr_mask & 0x7) {
				    dlr_add(octstr_get_cstr(conn->id), 
					    octstr_get_cstr(ts),
					    octstr_get_cstr(adc),
					    octstr_get_cstr(m->sms.service),
					    octstr_get_cstr(m->sms.dlr_url),
					    m->sms.dlr_mask);
				}
				octstr_destroy(ts);
				octstr_destroy(adc);
			    } else {
				octstr_destroy(ts);
			    }

			    
			}
			/*
			 * report the successful transmission to the generic bb code.
			 */
			bb_smscconn_sent(conn,
					 PRIVDATA(conn)->slots[emimsg->trn].sendmsg);
		    } else {
			bb_smscconn_send_failed(conn,
						PRIVDATA(conn)->slots[emimsg->trn].sendmsg,
						SMSCCONN_FAILED_REJECTED);
		    }
		} else if (emimsg->ot == 31) {
		    /* We don't use the data in the reply */
		    ;
		} else {
		    panic(0, "Bug, ACK handler missing for sent packet");
		}
	    }
	}
	emimsg_destroy(emimsg);
    }

    if (conn_read_error(server)) {
	error(0, "emi2: Error trying to read ACKs from SMSC");
	return -1;
    }
    
    if (conn_eof(server)) {
	info(0, "emi2: Main connection closed by SMSC");
	return -1;
    }

    return 0;
}

static void emi2_idleprocessing(SMSCConn *conn)
{
    time_t current_time;
    int i;
    
    /*
     * Check whether there are messages the server hasn't acked in a
     * reasonable time
     */
    current_time = time(NULL);
    
    if (PRIVDATA(conn)->unacked && (current_time > (PRIVDATA(conn)->check_time + 30))) {
	PRIVDATA(conn)->check_time = current_time;
	for (i = 0; i < EMI2_MAX_TRN; i++) {
	    if (SLOTBUSY(conn,i)
		&& PRIVDATA(conn)->slots[i].sendtime < (current_time - PRIVDATA(conn)->waitack)) {
		PRIVDATA(conn)->slots[i].sendtime = 0;
		PRIVDATA(conn)->unacked--;
		if (PRIVDATA(conn)->slots[i].sendtype == 51) {
		    warning(0, "smsc_emi2: received neither ACK nor NACK for message %d " 
			    "in %d seconds, resending message", i, PRIVDATA(conn)->waitack);
		    list_produce(PRIVDATA(conn)->outgoing_queue,
				 PRIVDATA(conn)->slots[i].sendmsg);
		    if (PRIVDATA(conn)->flowcontrol) PRIVDATA(conn)->can_write=1;
		    /* Wake up this same thread to send again
		     * (simpler than avoiding sleep) */
		    gwthread_wakeup(PRIVDATA(conn)->sender_thread);
		} else if (PRIVDATA(conn)->slots[i].sendtype == 31) {
		    warning(0, "smsc_emi2: Alert (operation 31) was not "
			    "ACKed within %d seconds", PRIVDATA(conn)->waitack);
		    if (PRIVDATA(conn)->flowcontrol) PRIVDATA(conn)->can_write=1;
		} else {
		    panic(0, "Bug, no timeout handler for sent packet");
		}
	    }
	}
    }
}

static void emi2_idletimeout_handling (SMSCConn *conn, Connection **server)
{
    /*
     * close the connection if there was no activity.
     */
    if ((*server != NULL) && CONNECTIONIDLE(conn)) {
	info(0, "emi2: closing idle connection.");
	conn_destroy(*server);
	*server = NULL;
    }
}

/*
 * this function calculates the new timeouttime.
 */
static double emi2_get_timeouttime (SMSCConn *conn, Connection *server)
{
    double ka_timeouttime = PRIVDATA(conn)->keepalive ? PRIVDATA(conn)->keepalive + 1 : DBL_MAX;
    double idle_timeouttime = (PRIVDATA(conn)->idle_timeout && server) ? PRIVDATA(conn)->idle_timeout : DBL_MAX;
    double result = ka_timeouttime < idle_timeouttime ? ka_timeouttime : idle_timeouttime;

    if (result == DBL_MAX)
	result = 30;

    return result;
}

/*
 * the main event processing loop.
 */
static void emi2_send_loop(SMSCConn *conn, Connection **server)
{
    for (;;) {
	double timeouttime = emi2_get_timeouttime (conn, *server);
	
	EMI2Event event = emi2_wait (conn, *server, timeouttime);
	
	switch (event) {
	case EMI2_CONNERR:
	    return;
	    
	case EMI2_SENDREQ:
	    if (*server == NULL) {
		return; /* reopen the connection */
	    }
	    
	    if (emi2_do_send (conn, *server) < 0) {
		return; /* reopen the connection */
	    }
	    break;
	    
	case EMI2_SMSCREQ:
	    if (emi2_handle_smscreq (conn, *server) < 0) {
		return; /* reopen the connection */
	    }
	    break;
	    
	case EMI2_TIMEOUT:
	    break;
	}
        if ((*server !=NULL) && (emi2_handle_smscreq (conn, *server) < 0)) {
            return; /* reopen the connection */
        }
	
	if (emi2_needs_keepalive (conn)) {
	    if (*server == NULL) {
		return; /* reopen the connection */
	    }
	    
	    emi2_keepalive_handling (conn, *server);
	}
	
	emi2_idleprocessing (conn);
	emi2_idletimeout_handling (conn, server);

	if (PRIVDATA(conn)->shutdown && (PRIVDATA(conn)->unacked == 0)) {
	    /* shutdown and no open messages */
	    break;
	}

	if (*server != NULL) {
	    if (conn_read_error(*server)) {
		warning(0, "emi2: Error reading from the main connection");
		break;
	    }
	
	    if (conn_eof(*server)) {
		info(0, "emi2: Main connection closed by SMSC");
		break;
	    }
	}
    }
}

static void emi2_sender(void *arg)
{
    SMSCConn *conn = arg;
    PrivData *privdata = conn->data;
    Msg *msg;
    Connection *server;

    while (!privdata->shutdown) {
	if ((server = open_send_connection(conn)) == NULL) {
	    privdata->shutdown = 1;
	    if (privdata->rport > 0)
		gwthread_wakeup(privdata->receiver_thread);
	    break;
	}
	emi2_send_loop(conn, &server);
	clear_sent(privdata);

	if (server != NULL) {
	    conn_destroy(server);
	}
    }

    while((msg = list_extract_first(privdata->outgoing_queue)) != NULL)
	bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN);
    if (privdata->rport > 0)
	gwthread_join(privdata->receiver_thread);
    mutex_lock(conn->flow_mutex);

    conn->status = SMSCCONN_DEAD;

    list_destroy(privdata->outgoing_queue, NULL);
    octstr_destroy(privdata->allow_ip);
    octstr_destroy(privdata->deny_ip);
    octstr_destroy(privdata->host);
    octstr_destroy(privdata->username);
    octstr_destroy(privdata->password);
    gw_free(privdata);
    conn->data = NULL;

    mutex_unlock(conn->flow_mutex);
    debug("bb.sms", 0, "smsc_emi2 connection has completed shutdown.");
    bb_smscconn_killed();
}


static void emi2_receiver(SMSCConn *conn, Connection *server)
{
    PrivData *privdata = conn->data;
    Octstr *str;
    struct emimsg *emimsg;

    while (1) {
	if (conn_eof(server)) {
	    info(0, "emi2: receive connection closed by SMSC");
	    return;
	}
	if (conn_read_error(server)) {
	    error(0, "emi2: receive connection broken");
	    return;
	}
	if (conn->is_stopped)
	    str = NULL;
	else
	    str = conn_read_packet(server, 2, 3);
	if (str) {
	    debug("smsc.emi2", 0, "Got packet from the receive connection.");
	    if ( (emimsg = get_fields(str)) ) {
		if (emimsg->or == 'O') {
		    if (handle_operation(conn, server, emimsg) < 0) {
			emimsg_destroy(emimsg);
			return;
		    }
		}
		else
		    error(0, "emi2: No ACKs expected on receive connection!");
		emimsg_destroy(emimsg);
	    }
	    octstr_destroy(str);
	}
	else
	    conn_wait(server, -1);
	if (privdata->shutdown)
	    break;
    }
    return;
}


static int emi2_open_listening_socket(PrivData *privdata)
{
    int s;

    if ( (s = make_server_socket(privdata->rport, NULL)) == -1) {
	    /* XXX add interface_name if required */
	error(0, "smsc_emi2: could not create listening socket in port %d",
	      privdata->rport);
	return -1;
    }
    if (socket_set_blocking(s, 0) == -1) {
	error(0, "smsc_emi2: couldn't make listening socket port %d "
		 "non-blocking", privdata->rport);
	close(s);
	return -1;
    }
    privdata->listening_socket = s;
    return 0;
}


static void emi2_listener(void *arg)
{
    SMSCConn	*conn = arg;
    PrivData	*privdata = conn->data;
    struct sockaddr_in server_addr;
    socklen_t	server_addr_len;
    Octstr	*ip;
    Connection	*server;
    int 	s, ret;

    while (!privdata->shutdown) {
	server_addr_len = sizeof(server_addr);
	ret = gwthread_pollfd(privdata->listening_socket, POLLIN, -1);
	if (ret == -1) {
	    if (errno == EINTR)
		continue;
	    error(0, "Poll for emi2 smsc connections failed, shutting down");
	    break;
	}
	if (privdata->shutdown)
	    break;
	if (ret == 0) /* This thread was woken up from elsewhere, but
			 if we're not shutting down nothing to do here. */
	    continue;
	s = accept(privdata->listening_socket, (struct sockaddr *)&server_addr,
		   &server_addr_len);
	if (s == -1) {
	    warning(errno, "emi2_listener: accept() failed, retrying...");
	    continue;
	}
	ip = host_ip(server_addr);
	if (!is_allowed_ip(privdata->allow_ip, privdata->deny_ip, ip)) {
	    info(0, "Emi2 smsc connection tried from denied host <%s>,"
		 " disconnected", octstr_get_cstr(ip));
	    octstr_destroy(ip);
	    close(s);
	    continue;
	}
	server = conn_wrap_fd(s, 0);
	if (server == NULL) {
	    error(0, "emi2_listener: conn_wrap_fd failed on accept()ed fd");
	    octstr_destroy(ip);
	    close(s);
	    continue;
	}
	conn_claim(server);
	info(0, "Emi2: smsc connected from %s", octstr_get_cstr(ip));
	octstr_destroy(ip);

	emi2_receiver(conn, server);
	conn_destroy(server);
    }
    if (close(privdata->listening_socket) == -1)
	warning(errno, "smsc_emi2: couldn't close listening socket "
		"at shutdown");
    gwthread_wakeup(privdata->sender_thread);
}


static int add_msg_cb(SMSCConn *conn, Msg *sms)
{
    PrivData *privdata = conn->data;
    Msg *copy;

    copy = msg_duplicate(sms);
    list_produce(privdata->outgoing_queue, copy);
    gwthread_wakeup(privdata->sender_thread);

    return 0;
}


static int shutdown_cb(SMSCConn *conn, int finish_sending)
{
    PrivData *privdata = conn->data;

    debug("bb.sms", 0, "Shutting down SMSCConn EMI2, %s",
	  finish_sending ? "slow" : "instant");

    /* Documentation claims this would have been done by smscconn.c,
       but isn't when this code is being written. */
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    privdata->shutdown = 1; /* Separate from why_killed to avoid locking, as
			   why_killed may be changed from outside? */

    if (finish_sending == 0) {
	Msg *msg;
	while((msg = list_extract_first(privdata->outgoing_queue)) != NULL) {
	    bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN);
	}
    }

    if (privdata->rport > 0)
	gwthread_wakeup(privdata->receiver_thread);
    return 0;
}


static void start_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;

    /* in case there are messages in the buffer already */
    if (privdata->rport > 0)
	gwthread_wakeup(privdata->receiver_thread);
    debug("smsc.emi2", 0, "smsc_emi2: start called");
}


static long queued_cb(SMSCConn *conn)
{
    PrivData *privdata = conn->data;
    long ret = list_len(privdata->outgoing_queue);

    /* use internal queue as load, maybe something else later */

    conn->load = ret;
    return ret;
}


int smsc_emi2_create(SMSCConn *conn, CfgGroup *cfg)
{
    PrivData *privdata;
    Octstr *allow_ip, *deny_ip, *host;
    long portno, our_port, keepalive, flowcontrol, waitack, throughput, idle_timeout; 
    long window;
    	/* has to be long because of cfg_get_integer */
    int i;
    int retry;

    privdata = gw_malloc(sizeof(PrivData));
    privdata->outgoing_queue = list_create();
    privdata->listening_socket = -1;
    privdata->can_write = 1;
    privdata->priv_nexttrn = 0;
    privdata->last_activity_time = 0;

    
    if (cfg_get_integer(&idle_timeout, cfg, octstr_imm("idle-timeout")) == -1)
	idle_timeout = 0;
    
    privdata->idle_timeout = idle_timeout;

    if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1)
	portno = 0;
    privdata->port = portno;
    if (cfg_get_integer(&our_port, cfg, octstr_imm("our-port")) == -1)
	privdata->our_port = 0; /* 0 means use any port */
    else
	privdata->our_port = our_port;
    if (cfg_get_integer(&portno, cfg, octstr_imm("receive-port")) < 0)
	portno = 0;
    privdata->rport = portno;
    allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    host = cfg_get(cfg, octstr_imm("host"));
    if (allow_ip)
	deny_ip = octstr_create("*.*.*.*");
    else
	deny_ip = NULL;
    privdata->username = cfg_get(cfg, octstr_imm("smsc-username"));
    privdata->password = cfg_get(cfg, octstr_imm("smsc-password"));

    cfg_get_bool(&privdata->retry, cfg, octstr_imm("retry"));

    if (privdata->username == NULL || cfg_get_integer(&keepalive, cfg, octstr_imm("keepalive")) < 0)
	privdata->keepalive = 0;
    else
	privdata->keepalive = keepalive;

    if (cfg_get_integer(&flowcontrol, cfg, octstr_imm("flow-control")) < 0)
	privdata->flowcontrol = 0;
    else
	privdata->flowcontrol = flowcontrol;
    if (privdata->flowcontrol < 0 || privdata->flowcontrol > 1) {
	error(0, "'flow-control' invalid in emi2 configuration.");
	goto error;
    }

    if (cfg_get_integer(&throughput, cfg, octstr_imm("throughput")) < 0)
	privdata->throughput = 0;
    else
	privdata->throughput = throughput;

    if (cfg_get_integer(&window, cfg, octstr_imm("window")) < 0)
	privdata->window = EMI2_MAX_TRN;
    else
	privdata->window = window;
    if (privdata->window > EMI2_MAX_TRN) {
	warning(0, "Value of 'window' should be lesser or equal to %d..", EMI2_MAX_TRN);
	privdata->window = EMI2_MAX_TRN;
    }

    if (cfg_get_integer(&waitack, cfg, octstr_imm("wait-ack")) < 0)
	privdata->waitack = 60;
    else
	privdata->waitack = waitack;
    if (privdata->waitack < 30 ) {
	error(0, "'wait-ack' invalid in emi2 configuration.");
	goto error;
    }

    if (privdata->port <= 0 || privdata->port > 65535) {
	error(0, "'port' missing/invalid in emi2 configuration.");
	goto error;
    }
    if (privdata->rport < 0 || privdata->rport > 65535) {
	error(0, "'receive-port' missing/invalid in emi2 configuration.");
	goto error;
    }
    if (host == NULL) {
	error(0, "'host' missing in emi2 configuration.");
	goto error;
    }

    privdata->allow_ip = allow_ip;
    privdata->deny_ip = deny_ip;
    privdata->host = host;

    if (privdata->rport > 0 && emi2_open_listening_socket(privdata) < 0) {
	gw_free(privdata);
	privdata = NULL;
	goto error;
    }

    conn->data = privdata;

    conn->name = octstr_format("EMI2:%d", privdata->port);

    privdata->shutdown = 0;

    for (i = 0; i < EMI2_MAX_TRN; i++)
	privdata->slots[i].sendtime = 0;
    privdata->unacked = 0;

    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = time(NULL);

    if ( privdata->rport > 0 && (privdata->receiver_thread =
	  gwthread_create(emi2_listener, conn)) == -1)
	  goto error;

    if ((privdata->sender_thread = gwthread_create(emi2_sender, conn)) == -1) {
	privdata->shutdown = 1;
	if (privdata->rport > 0) {
	    gwthread_wakeup(privdata->receiver_thread);
	    gwthread_join(privdata->receiver_thread);
	}
	goto error;
    }

    conn->shutdown = shutdown_cb;
    conn->queued = queued_cb;
    conn->start_conn = start_cb;
    conn->send_msg = add_msg_cb;

    return 0;

error:
    error(0, "Failed to create emi2 smsc connection");
    if (privdata != NULL) {
	list_destroy(privdata->outgoing_queue, NULL);
    }
    gw_free(privdata);
    octstr_destroy(allow_ip);
    octstr_destroy(deny_ip);
    octstr_destroy(host);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    info(0, "exiting");
    return -1;
}
