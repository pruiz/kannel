/*
 * smsc_smpp.c - SMPP v3.3 and v3.4 implementation
 *
 * Lars Wirzenius
 */
 
/* XXX SMSCConn status setting needs thinking */
/* XXX UDH reception */
/* XXX check UDH sending fields esm_class and data_coding from GSM specs */
/* XXX some _resp pdus have semi-optional body field: used when status != 0 */
/* XXX write out unbind pdus at quit time */
/* XXX charset conversions on incoming messages (didn't work earlier, 
       either) */
/* XXX numbering plans and type of number: check spec */
/* XXX notice that link is down if responses to enquire_link aren't received */
 
#include "gwlib/gwlib.h"
#include "msg.h"
#include "smsc_p.h"
#include "smpp_pdu.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "sms.h"


/*
 * Select these based on whether you want to dump SMPP PDUs as they are 
 * sent and received or not. Not dumping should be the default in at least
 * stable releases.
 */

#if 1
/* This version doesn't dump. */
static void dump_pdu(const char *msg, SMPP_PDU *pdu)
{
}
#else
/* This version does dump. */
static void dump_pdu(const char *msg, SMPP_PDU *pdu)
{
    debug("bb.sms.smpp", 0, "SMPP: %s", msg);
    smpp_pdu_dump(pdu);
}
#endif



/* 
 * Some constants.
 */

#define SMPP_ENQUIRE_LINK_INTERVAL  30.0
#define SMPP_MAX_PENDING_SUBMITS    10
#define SMPP_RECONNECT_DELAY	    10.0



/***********************************************************************
 * Implementation of the actual SMPP protocol: reading and writing
 * PDUs in the correct order.
 */


typedef struct {
    long transmitter;
    long receiver;
    List *msgs_to_send;
    Dict *sent_msgs;
    List *received_msgs;
    Counter *message_id_counter;
    Octstr *host;
    Octstr *username;
    Octstr *password;
    int transmit_port;
    int receive_port;
    int quitting;
    SMSCConn *conn;
} SMPP;


static SMPP *smpp_create(SMSCConn *conn, Octstr *host, int transmit_port, 
    	    	    	 int receive_port, Octstr *username, Octstr *password)
{
    SMPP *smpp;
    
    smpp = gw_malloc(sizeof(*smpp));
    smpp->transmitter = -1;
    smpp->receiver = -1;
    smpp->msgs_to_send = list_create();
    smpp->sent_msgs = dict_create(16, NULL);
    list_add_producer(smpp->msgs_to_send);
    smpp->received_msgs = list_create();
    smpp->message_id_counter = counter_create();
    smpp->host = octstr_duplicate(host);
    smpp->username = octstr_duplicate(username);
    smpp->password = octstr_duplicate(password);
    smpp->transmit_port = transmit_port;
    smpp->receive_port = receive_port;
    smpp->quitting = 0;
    smpp->conn = conn;
    
    return smpp;
}


static void smpp_destroy(SMPP *smpp)
{
    if (smpp != NULL) {
	list_destroy(smpp->msgs_to_send, msg_destroy_item);
	dict_destroy(smpp->sent_msgs);
	list_destroy(smpp->received_msgs, msg_destroy_item);
	counter_destroy(smpp->message_id_counter);
	octstr_destroy(smpp->host);
	octstr_destroy(smpp->username);
	octstr_destroy(smpp->password);
	gw_free(smpp);
    }
}


/*
 * Try to read an SMPP PDU from a Connection. Return -1 for error (caller
 * should close the connection), 0 for no PDU to ready yet, or 1 for PDU
 * read and unpacked. Return a pointer to the PDU in `*pdu'. Use `*len'
 * to store the length of the PDU to read (it may be possible to read the
 * length, but not the rest of the PDU - we need to remember the lenght
 * for the next call). `*len' should be zero at the first call.
 */
static int read_pdu(Connection *conn, long *len, SMPP_PDU **pdu)
{
    Octstr *os;

    if (*len == 0) {
	*len = smpp_pdu_read_len(conn);
	if (*len == -1) {
	    error(0, "SMPP: Server sent garbage, ignored.");
	    return -1;
	} else if (*len == 0) {
	    if (conn_eof(conn) || conn_read_error(conn))
		return -1;
	    return 0;
	}
    }
    
    os = smpp_pdu_read_data(conn, *len);
    if (os == NULL) {
	if (conn_eof(conn) || conn_read_error(conn))
	    return -1;
	return 0;
    }
    *len = 0;
    
    *pdu = smpp_pdu_unpack(os);
    if (*pdu == NULL) {
	error(0, "SMPP: PDU unpacking failed.");
	debug("bb.sms.smpp", 0, "SMPP: Failed PDU follows.");
	octstr_dump(os, 0);
	octstr_destroy(os);
	return -1;
    }
    
    octstr_destroy(os);
    return 1;
}


static Msg *pdu_to_msg(SMPP_PDU *pdu)
{
    Msg *msg;

    gw_assert(pdu->type == deliver_sm);
    
    msg = msg_create(sms);
    msg->sms.sender = pdu->u.deliver_sm.source_addr;
    pdu->u.deliver_sm.source_addr = NULL;
    msg->sms.receiver = pdu->u.deliver_sm.destination_addr;
    pdu->u.deliver_sm.destination_addr = NULL;
    msg->sms.msgdata = pdu->u.deliver_sm.short_message;
    pdu->u.deliver_sm.short_message = NULL;
    charset_gsm_to_latin1(msg->sms.msgdata);

    return msg;
}


static long smpp_status_to_smscconn_failure_reason(long status)
{
    enum {
	ESME_RMSGQFUL = 0x00000014
    };

    if (status == ESME_RMSGQFUL)
	return SMSCCONN_FAILED_TEMPORARILY;

    return SMSCCONN_FAILED_REJECTED;
}


static SMPP_PDU *msg_to_pdu(SMPP *smpp, Msg *msg)
{
    SMPP_PDU *pdu;
    
    pdu = smpp_pdu_create(submit_sm, 
    	    	    	  counter_increase(smpp->message_id_counter));
    pdu->u.submit_sm.source_addr = octstr_duplicate(msg->sms.sender);
    pdu->u.submit_sm.destination_addr = octstr_duplicate(msg->sms.receiver);
    if (msg->sms.flag_udh) {
	pdu->u.submit_sm.short_message =
	    octstr_format("%S%S", msg->sms.udhdata, msg->sms.msgdata);
	pdu->u.submit_sm.esm_class = SMPP_ESM_CLASS_UDH_INDICATOR;
    } else {
	pdu->u.submit_sm.short_message = octstr_duplicate(msg->sms.msgdata);
	charset_latin1_to_gsm(pdu->u.submit_sm.short_message);
    }
    if (msg->sms.flag_8bit)
        pdu->u.submit_sm.data_coding = DCS_OCTET_DATA;
    else
        pdu->u.submit_sm.data_coding = DCS_GSM_TEXT;
    
    return pdu;
}


static void send_enquire_link(SMPP *smpp, Connection *conn, long *last_sent)
{
    SMPP_PDU *pdu;
    Octstr *os;

    if (date_universal_now() - *last_sent < SMPP_ENQUIRE_LINK_INTERVAL)
    	return;
    *last_sent = date_universal_now();

    pdu = smpp_pdu_create(enquire_link, 
			  counter_increase(smpp->message_id_counter));
    dump_pdu("Sending enquire link:", pdu);
    os = smpp_pdu_pack(pdu);
    conn_write(conn, os); /* Write errors checked by caller. */
    octstr_destroy(os);
    smpp_pdu_destroy(pdu);
}


static void send_pdu(Connection *conn, SMPP_PDU *pdu)
{
    Octstr *os;
    
    dump_pdu("Sending PDU:", pdu);
    os = smpp_pdu_pack(pdu);
    conn_write(conn, os);   /* Caller checks for write errors later */
    octstr_destroy(os);
}


static void send_messages(SMPP *smpp, Connection *conn, long *pending_submits)
{
    Msg *msg;
    SMPP_PDU *pdu;
    Octstr *os;

    if (*pending_submits == -1)
    	return;

    while (*pending_submits < SMPP_MAX_PENDING_SUBMITS) {
    	/* Get next message, quit if none to be sent */
    	msg = list_extract_first(smpp->msgs_to_send);
	if (msg == NULL)
	    break;
	    
	/* Send PDU, record it as waiting for ack from SMS center */
	pdu = msg_to_pdu(smpp, msg);
	os = octstr_format("%ld", pdu->u.submit_sm.sequence_number);
	dict_put(smpp->sent_msgs, os, msg);
	octstr_destroy(os);
	send_pdu(conn, pdu);
    	dump_pdu("Sent PDU:", pdu);
	smpp_pdu_destroy(pdu);

	++(*pending_submits);
    }
}


/*
 * Open transmission connection to SMS center. Return NULL for error, 
 * open Connection for OK. Caller must set smpp->conn->status correctly 
 * before calling this.
 */
static Connection *open_transmitter(SMPP *smpp)
{
    SMPP_PDU *bind;
    Connection *conn;

    conn = conn_open_tcp(smpp->host, smpp->transmit_port);
    if (conn == NULL) {
    	error(0, "SMPP: Couldn't connect to server.");
	return NULL;
    }
    
    bind = smpp_pdu_create(bind_transmitter,
			   counter_increase(smpp->message_id_counter));
    bind->u.bind_transmitter.system_id = octstr_duplicate(smpp->username);
    bind->u.bind_transmitter.password = octstr_duplicate(smpp->password);
    bind->u.bind_transmitter.system_type = octstr_create("VMA");
    bind->u.bind_transmitter.interface_version = 0x34;
    send_pdu(conn, bind);
    smpp_pdu_destroy(bind);

    return conn;
}


/*
 * Open reception connection to SMS center. Return NULL for error, 
 * open Connection for OK. Caller must set smpp->conn->status correctly 
 * before calling this.
 */
static Connection *open_receiver(SMPP *smpp)
{
    SMPP_PDU *bind;
    Connection *conn;

    conn = conn_open_tcp(smpp->host, smpp->receive_port);
    if (conn == NULL) {
    	error(0, "SMPP: Couldn't connect to server.");
	return NULL;
    }
    
    bind = smpp_pdu_create(bind_receiver,
			   counter_increase(smpp->message_id_counter));
    bind->u.bind_receiver.system_id = octstr_duplicate(smpp->username);
    bind->u.bind_receiver.password = octstr_duplicate(smpp->password);
    bind->u.bind_receiver.system_type = octstr_create("VMA");
    bind->u.bind_receiver.interface_version = 0x34;
    send_pdu(conn, bind);
    smpp_pdu_destroy(bind);

    return conn;
}


static void handle_pdu(SMPP *smpp, Connection *conn, SMPP_PDU *pdu, 
    	    	       long *pending_submits)
{
    SMPP_PDU *resp;
    Octstr *os;
    Msg *msg;
    long reason;

    resp = NULL;

    switch (pdu->type) {
    case deliver_sm:
	/* XXX UDH */
	/* XXX handle error return */
	bb_smscconn_receive(smpp->conn, pdu_to_msg(pdu));
	resp = smpp_pdu_create(deliver_sm_resp, 
			       pdu->u.deliver_sm.sequence_number);
	break;
	
    case enquire_link:
	resp = smpp_pdu_create(enquire_link_resp, 
			       pdu->u.enquire_link.sequence_number);
	break;

    case enquire_link_resp:
	break;

    case submit_sm_resp:
	os = octstr_format("%ld", pdu->u.submit_sm.sequence_number);
	msg = dict_remove(smpp->sent_msgs, os);
	octstr_destroy(os);
	if (msg == NULL) {
	    warning(0, "SMPP: SMSC sent submit_sm_resp "
		       "with wrong sequence number 0x%08lx", 
		       pdu->u.submit_sm.sequence_number);
	} else if (pdu->u.submit_sm_resp.command_status != 0) {
	    error(0, "SMPP: SMSC returned error code 0x%08lu "
		     "in response to submit_sm.",
		     pdu->u.submit_sm_resp.command_status);
	    reason = smpp_status_to_smscconn_failure_reason(
			pdu->u.submit_sm.command_status);
	    bb_smscconn_send_failed(smpp->conn, msg, reason);
	    --(*pending_submits);
	} else {
	    bb_smscconn_sent(smpp->conn, msg);
	    --(*pending_submits);
	}
	break;

    case bind_transmitter_resp:
	if (pdu->u.bind_transmitter_resp.command_status != 0) {
	    error(0, "SMPP: SMSC rejected login to transmit, "
		     "code 0x%08lx.",
		     pdu->u.bind_transmitter_resp.command_status);
	} else {
	    *pending_submits = 0;
	    smpp->conn->status = SMSCCONN_ACTIVE;
	    smpp->conn->connect_time = time(NULL);
	    bb_smscconn_connected(smpp->conn);
	}
	break;

    case bind_receiver_resp:
	if (pdu->u.bind_transmitter_resp.command_status != 0) {
	    error(0, "SMPP: SMSC rejected login to receive, "
		     "code 0x%08lx.",
		     pdu->u.bind_transmitter_resp.command_status);
	}
	break;

    default:
	error(0, "SMPP: Unknown PDU type 0x%08lx, ignored.", 
		 pdu->type);
	break;
    }
    
    if (resp != NULL) {
    	send_pdu(conn, resp);
	smpp_pdu_destroy(resp);
    }
}


struct io_arg {
    SMPP *smpp;
    int transmitter;
};


static struct io_arg *io_arg_create(SMPP *smpp, int transmitter)
{
    struct io_arg *io_arg;
    
    io_arg = gw_malloc(sizeof(*io_arg));
    io_arg->smpp = smpp;
    io_arg->transmitter = transmitter;
    return io_arg;
}



/*
 * This is the main function for the background thread for doing I/O on
 * one SMPP connection (the one for transmitting or receiving messages).
 * It makes the initial connection to the SMPP server and re-connects
 * if there are I/O errors or other errors that require it.
 */
static void io_thread(void *arg)
{
    SMPP *smpp;
    struct io_arg *io_arg;
    int transmitter;
    Connection *conn;
    int ret;
    long last_enquire_sent;
    long pending_submits;
    long len;
    SMPP_PDU *pdu;

    io_arg = arg;
    smpp = io_arg->smpp;
    transmitter = io_arg->transmitter;
    gw_free(io_arg);

    conn = NULL;
    while (!smpp->quitting) {
	if (transmitter)
	    conn = open_transmitter(smpp);
	else
	    conn = open_receiver(smpp);
	if (conn == NULL) {
	    error(0, "SMPP: Couldn't connect to SMS center.");
	    gwthread_sleep(SMPP_RECONNECT_DELAY);
	    smpp->conn->status = SMSCCONN_RECONNECTING;
	    continue;
	}
	
	last_enquire_sent = date_universal_now();
	pending_submits = -1;
	len = 0;
	while (!smpp->quitting && conn_wait(conn, 1.0) != -1) { /* XXX 1.0 should be calc'd */
	    send_enquire_link(smpp, conn, &last_enquire_sent);
	    
	    while ((ret = read_pdu(conn, &len, &pdu)) == 1) {
    	    	/* Deal with the PDU we just got */
		dump_pdu("Got PDU:", pdu);
		handle_pdu(smpp, conn, pdu, &pending_submits);
		smpp_pdu_destroy(pdu);

    	    	/* Make sure we send enquire_link even if we read a lot */
		send_enquire_link(smpp, conn, &last_enquire_sent);

    	    	/* Make sure we send even if we read a lot */
		if (transmitter)
		    send_messages(smpp, conn, &pending_submits);
	    }
	    
	    if (ret == -1) {
		error(0, "SMPP: I/O error or other error. Re-connecting.");
		break;
	    }
	    
	    if (transmitter)
		send_messages(smpp, conn, &pending_submits);
	}
	
	conn_destroy(conn);
	conn = NULL;
    }
    
    conn_destroy(conn);
}



/***********************************************************************
 * Functions called by smscconn.c via the SMSCConn function pointers.
 */
 

static long queued_cb(SMSCConn *conn)
{
    SMPP *smpp;

    smpp = conn->data;
    conn->load = list_len(smpp->msgs_to_send);
    return conn->load;
}


static int send_msg_cb(SMSCConn *conn, Msg *msg)
{
    SMPP *smpp;
    
    smpp = conn->data;
    list_produce(smpp->msgs_to_send, msg_duplicate(msg));
    return 0;
}


static int shutdown_cb(SMSCConn *conn, int finish_sending)
{
    SMPP *smpp;

    debug("bb.smpp", 0, "Shutting down SMSCConn %s (%s)",
    	  octstr_get_cstr(conn->name),
	  finish_sending ? "slow" : "instant");

    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;

    /* XXX implement finish_sending */

    smpp = conn->data;
    smpp->quitting = 1;
    gwthread_wakeup(smpp->transmitter);
    gwthread_wakeup(smpp->receiver);
    gwthread_join(smpp->transmitter);
    gwthread_join(smpp->receiver);
    smpp_destroy(smpp);
    
    debug("bb.smpp", 0, "SMSCConn %s shut down.", 
    	  octstr_get_cstr(conn->name));
    conn->status = SMSCCONN_DEAD;
    bb_smscconn_killed();
    return 0;
}


/***********************************************************************
 * Public interface. This version is suitable for the Kannel bearerbox
 * SMSCConn interface.
 */


int smsc_smpp_create(SMSCConn *conn, CfgGroup *grp)
{
    Octstr *host;
    long port;
    long receive_port;
    Octstr *username;
    Octstr *password;
    Octstr *system_id;
    Octstr *system_type;
    SMPP *smpp;
    
    host = cfg_get(grp, octstr_imm("host"));
    if (cfg_get_integer(&port, grp, octstr_imm("port")) == -1)
    	port = 0;
    if (cfg_get_integer(&receive_port, grp, octstr_imm("receive-port")) == -1)
    	receive_port = 0;
    username = cfg_get(grp, octstr_imm("smsc-username"));
    password = cfg_get(grp, octstr_imm("smsc-password"));
    system_id = cfg_get(grp, octstr_imm("system-id"));
    system_type = cfg_get(grp, octstr_imm("system-type"));
    
    /* XXX check that config is OK */
    /* XXX implement address-range */

    smpp = smpp_create(conn, host, port, receive_port, username, password);
    conn->data = smpp;
    conn->name = octstr_format("SMPP:%S:%d/%d:%S:%S", 
    	    	    	       host, port,
			       (receive_port ? receive_port : port), 
			       system_id, system_type);

    octstr_destroy(host);
    octstr_destroy(username);
    octstr_destroy(password);
    octstr_destroy(system_id);
    octstr_destroy(system_type);

    conn->status = SMSCCONN_CONNECTING;
    smpp->transmitter = gwthread_create(io_thread, io_arg_create(smpp, 1));
    smpp->receiver = gwthread_create(io_thread, io_arg_create(smpp, 0));
    
    if (smpp->transmitter == -1 || smpp->receiver == -1) {
    	error(0, "SMPP: Couldn't start I/O threads.");
	smpp->quitting = 1;
	if (smpp->transmitter != -1) {
	    gwthread_wakeup(smpp->transmitter);
	    gwthread_join(smpp->transmitter);
	}
	if (smpp->transmitter != -1) {
	    gwthread_wakeup(smpp->receiver);
	    gwthread_join(smpp->receiver);
	}
    	smpp_destroy(conn->data);
	return -1;
    }

    conn->shutdown = shutdown_cb;
    conn->queued = queued_cb;
    conn->send_msg = send_msg_cb;

    return 0;
}
