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

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "msg.h"
#include "sms.h"
#include "emimsg.h"
#include "dlr.h"

typedef struct privdata {
    List	*outgoing_queue;
    long	receiver_thread;
    long	sender_thread;
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
    time_t	sendtime[100];	/* When we sent out a message with a given
				 * TRN. Is 0 if the TRN is currently free. */
    int		sendtype[100];	/* OT of message, undefined if time == 0 */
    Msg		*sendmsg[100]; 	/* Corresponding message for OT == 51 */
    int		keepalive; 	/* Seconds to send a Keepalive Command (OT=31) */
    int		flowcontrol;	/* 0=Windowing, 1=Stop-and-Wait */
    int		waitack;	/* Seconds to wait to ack */
} PrivData;


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
	    info(0, "smsc_emi2: waiting for %d minutes before trying to "
		 "connect again", wait);
	    gwthread_sleep(wait * 60);
	    wait = wait > 5 ? 10 : wait * 2;
	}
	else
	    wait = 1;

	server = conn_open_tcp_with_port(privdata->host, privdata->port,
					 privdata->our_port);
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
	    emimsg_send(server, emimsg);
	    emimsg_destroy(emimsg);
	    result = wait_for_ack(privdata, server, 60, 30);
	    if (result == -2) {
		/* Are SMSCs going to return any temporary errors? If so,
		 * testing for those error codes should be added here. */
		error(0, "smsc_emi2: Server rejected our login, giving up");
		conn->why_killed = SMSCCONN_KILLED_WRONG_PASSWORD;
		conn_destroy(server);
		return NULL;
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

	if (privdata->username) {
	    emimsg = make_emi31(privdata, 0);
	    emimsg_send(server, emimsg);
	    emimsg_destroy(emimsg);
	    result = wait_for_ack(privdata, server, 31, 30);
	    /* XXX In here we can test if smsc doesn't know alert command, and we
	     * could try to use other command, like 61 
	     */
	    if (result == -2) {
		error(0, "smsc_emi2: Server rejected our alert, disabling keepalive");
		privdata->keepalive = 0;
	    }
	    else if (result == 0) {
		error(0, "smsc_emi2: Got no reply to alert attempt "
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
    if(msg->sms.dlr_mask) {
    	emimsg->fields[E50_NRQ] = octstr_create("1");
	emimsg->fields[E50_NT] = octstr_create("3");
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
	if (emimsg_send(server, reply) < 0) {
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
	if (emimsg_send(server, reply) < 0) {
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
		msg = NULL;
		break;
	case 2: /* not delivered */
		msg = dlr_find(octstr_get_cstr(conn->id), 
			octstr_get_cstr(emimsg->fields[E50_SCTS]), /* timestamp */
			octstr_get_cstr(emimsg->fields[E50_OADC]), /* destination */
			DLR_FAIL);
		break;
	}
	if(msg != NULL) {           
	    bb_smscconn_receive(conn, msg);
	}
	reply = emimsg_create_reply(53, emimsg->trn, 1);
	if (emimsg_send(server, reply) < 0) {
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

static void clear_sent(PrivData *privdata)
{
    int i;

    for (i = 0; i < 100; i++) {
	if (privdata->sendtime[i] && privdata->sendtype[i] == 51)
	    list_produce(privdata->outgoing_queue, privdata->sendmsg[i]);
	privdata->sendtime[i] = 0;
    }
    privdata->unacked = 0;
}


static void emi2_send_loop(SMSCConn *conn, Connection *server)
{
    PrivData *privdata = conn->data;
    int i, nexttrn = 0;
    struct emimsg *emimsg;
    Octstr	*str;
    Msg		*msg;
    time_t	current_time, check_time, keepalive_time=0;
    int write = 1; /* write=1, read=0, for stop-and-wait flow control */

    /* Initialize keepalive time counter */
    if ( privdata->keepalive > 0 )
	keepalive_time = time(NULL);

    check_time = time(NULL);
    while (1) {
	/* Send messages if there's room in the sending window */
	while ((write || !privdata->flowcontrol) && privdata->unacked < 100
	       && !privdata->shutdown &&
	       (msg = list_extract_first(privdata->outgoing_queue)) != NULL) {
	    while (privdata->sendtime[nexttrn % 100] != 0)
		nexttrn++; /* pick unused TRN */
	    nexttrn %= 100;
	    emimsg = msg_to_emimsg(msg, nexttrn);
	    privdata->sendmsg[nexttrn] = msg;
	    privdata->sendtype[nexttrn] = 51;
	    privdata->sendtime[nexttrn++] = time(NULL);
	    privdata->unacked++;
	    if (emimsg_send(server, emimsg) == -1) {
		emimsg_destroy(emimsg);
		return;
	    }
	    emimsg_destroy(emimsg);

	    if ( privdata->keepalive > 0 )
		keepalive_time = time(NULL);

	    write = 0;
	}

	/* Send keepalive if there's room in the sending window */
	if ((write || !privdata->flowcontrol) && privdata->keepalive > 0 
	    && time(NULL) > keepalive_time + privdata->keepalive &&
	    privdata->unacked < 100 && !privdata->shutdown ) {
	    while (privdata->sendtime[nexttrn % 100] != 0)
		nexttrn++; /* pick unused TRN */
	    nexttrn %= 100;
	    emimsg = make_emi31(privdata, nexttrn);
	    privdata->sendtype[nexttrn]= 31;
	    privdata->sendtime[nexttrn++] = time(NULL);
	    privdata->unacked++;
	    if (emimsg_send(server, emimsg) == -1) {
		emimsg_destroy(emimsg);
		return;
	    }
	    emimsg_destroy(emimsg);
	    if (privdata->keepalive)
		keepalive_time = time(NULL);
	    write = 0;
	}

	/* Read acks/nacks from the server */
	while ((str = conn_read_packet(server, 2, 3))) {
	    debug("smsc.emi2", 0, "Got packet from the main socket");
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
			return; /* Connection broke */
		}
		else
		    info(0, "Ignoring operation from main socket "
			 "because the connection is stopped.");
	    }
	    else {   /* Already checked to be 'O' or 'R' */
		if (!privdata->sendtime[emimsg->trn] ||
		    emimsg->ot != privdata->sendtype[emimsg->trn])
		    error(0, "Emi2: Got ack, don't remember sending O?");
		else {
		    write = 1;
		    privdata->sendtime[emimsg->trn] = 0;
		    privdata->unacked--;
		    if (emimsg->ot == 51) {
			if (octstr_get_char(emimsg->fields[0], 0) == 'A')
			{
			    /* we got an ack back. We might have to store the */
			    /* timestamp for delivery notifications now */
			    Octstr *ts, *adc;
			    int	i;
			    Msg *m;
			  
			    ts = octstr_duplicate(emimsg->fields[2]);
			    i = octstr_search_char(ts,':',0);
			    if (i>0)
			    {
			    	octstr_delete(ts,0,i+1);
			        adc = octstr_duplicate(emimsg->fields[2]);
			        octstr_truncate(adc,i);
			        
			        m = privdata->sendmsg[emimsg->trn];
			        if(m == NULL)
 				        info(0,"uhhh m is NULL, very bad");
			        else if(m->sms.dlr_mask)
			        {
			    	    dlr_add(octstr_get_cstr(conn->id), 
			            	octstr_get_cstr(ts),
			            	octstr_get_cstr(adc),
			             	octstr_get_cstr(m->sms.dlr_keyword),
			             	octstr_get_cstr(m->sms.dlr_id),
			             	m->sms.dlr_mask);
			        }
				octstr_destroy(ts);
				octstr_destroy(adc);
			    }
			    else
			    	octstr_destroy(ts);
			    bb_smscconn_sent(conn,
					     privdata->sendmsg[emimsg->trn]);
			}
			else
			    bb_smscconn_send_failed(conn,
						privdata->sendmsg[emimsg->trn],
						SMSCCONN_FAILED_REJECTED);
		    }
		    else if (emimsg->ot == 31)
			;
			/* We don't use the data in the reply */
		    else
			panic(0, "Bug, ACK handler missing for sent packet");
		}
	    }
	    emimsg_destroy(emimsg);
	}

	if (conn_read_error(server)) {
	    error(0, "emi2: Error trying to read ACKs from SMSC");
	    return;
	}
	if (conn_eof(server)) {
	    info(0, "emi2: Main connection closed by SMSC");
	    return;
	}
	/* Check whether there are messages the server hasn't acked in a
	 * reasonable time */
	current_time = time(NULL);
	if (privdata->unacked && current_time > check_time + 30) {
	    check_time = current_time;
	    for (i = 0; i < 100; i++)
		if (privdata->sendtime[i]
		    && privdata->sendtime[i] < current_time - privdata->waitack) {
		    privdata->sendtime[i] = 0;
		    privdata->unacked--;
		    if (privdata->sendtype[i] == 51) {
			warning(0, "smsc_emi2: received neither ACK nor NACK for message %d " 
				"in %d seconds, resending message", i, privdata->waitack);
			list_produce(privdata->outgoing_queue,
				     privdata->sendmsg[i]);
			if (privdata->flowcontrol) write=1;
			/* Wake up this same thread to send again
			 * (simpler than avoiding sleep) */
			gwthread_wakeup(privdata->sender_thread);
		    }
		    else if (privdata->sendtype[i] == 31)
			warning(0, "smsc_emi2: Alert (operation 31) was not "
				"ACKed within %d seconds", privdata->waitack);
		    else
			panic(0, "Bug, no timeout handler for sent packet");
		}
	}

	/* During shutdown, wait until we know whether the messages we just
	 * sent were accepted by the SMSC */
	if (privdata->shutdown && privdata->unacked == 0)
	    break;

	/* If the server doesn't ack our messages, wake up to resend them */
	if (privdata->flowcontrol && write && list_len(privdata->outgoing_queue))
	    ;
	else if (privdata->unacked == 0) {
	    if (privdata->keepalive > 0)
		conn_wait(server, privdata->keepalive + 1);
	    else
		conn_wait(server, -1);
	} else 
	    if (privdata->keepalive > 0 && privdata->keepalive < 40)
		conn_wait(server, privdata->keepalive + 1);
	    else
		conn_wait(server, 40);
	if (conn_read_error(server)) {
	    warning(0, "emi2: Error reading from the main connection");
	    return;
	}
	if (conn_eof(server)) {
	    info(0, "emi2: Main connection closed by SMSC");
	    return;
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
	emi2_send_loop(conn, server);
	clear_sent(privdata);
	conn_destroy(server);
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

    if ( (s = make_server_socket(privdata->rport)) == -1) {
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
	server = conn_wrap_fd(s);
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
    long portno, our_port, keepalive, flowcontrol, waitack; 
    	/* has to be long because of cfg_get_integer */
    int i;

    privdata = gw_malloc(sizeof(PrivData));
    privdata->outgoing_queue = list_create();
    privdata->listening_socket = -1;

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

    if (privdata->username && cfg_get_integer(&keepalive, cfg, octstr_imm("keepalive")) < 0)
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

    for (i = 0; i < 100; i++)
	privdata->sendtime[i] = 0;
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
