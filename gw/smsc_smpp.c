/*
 * smsc_smpp.c - SMPP v3.3 and v3.4 implementation
 *
 * Lars Wirzenius
 */
 
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



/***********************************************************************
 * Implementation of the actual SMPP protocol: reading and writing
 * PDUs in the correct order.
 */


enum { MAX_PENDING_SUBMITS = 10 };


struct SMPP {
    Connection *transmission;
    Connection *reception;
    List *msgs_to_send;
    List *received_msgs;
    Counter *message_id_counter;
    Octstr *host;
    Octstr *username;
    Octstr *password;
    int transmit_port;
    int receive_port;
    int quitting;
    long transmit_reader;
    long receive_reader;
    long writer;
    Semaphore *send_semaphore;
};


static SMPP *smpp_create(Octstr *host, int transmit_port, int receive_port,
    	    	    	 Octstr *username, Octstr *password)
{
    SMPP *smpp;
    
    smpp = gw_malloc(sizeof(*smpp));
    smpp->transmission = NULL;
    smpp->reception = NULL;
    smpp->msgs_to_send = list_create();
    list_add_producer(smpp->msgs_to_send);
    smpp->received_msgs = list_create();
    smpp->message_id_counter = counter_create();
    smpp->host = octstr_duplicate(host);
    smpp->username = octstr_duplicate(username);
    smpp->password = octstr_duplicate(password);
    smpp->transmit_port = transmit_port;
    smpp->receive_port = receive_port;
    smpp->quitting = 0;
    smpp->transmit_reader = -1;
    smpp->receive_reader = -1;
    smpp->writer = -1;
    smpp->send_semaphore = semaphore_create(0);
    
    return smpp;
}


static void smpp_destroy(SMPP *smpp)
{
    if (smpp != NULL) {
	conn_destroy(smpp->transmission);
	conn_destroy(smpp->reception);
	list_destroy(smpp->msgs_to_send, msg_destroy_item);
	list_destroy(smpp->received_msgs, msg_destroy_item);
	counter_destroy(smpp->message_id_counter);
	octstr_destroy(smpp->host);
	octstr_destroy(smpp->username);
	octstr_destroy(smpp->password);
	semaphore_destroy(smpp->send_semaphore);
	gw_free(smpp);
    }
}


static void smpp_wakeup_for_quit(SMPP *smpp)
{
    smpp->quitting = 1;

    if (smpp->transmit_reader != -1)
    	gwthread_wakeup(smpp->transmit_reader);
    if (smpp->receive_reader != -1)
    	gwthread_wakeup(smpp->receive_reader);
    list_remove_producer(smpp->msgs_to_send);
    semaphore_up(smpp->send_semaphore);

    if (smpp->transmit_reader != -1)
    	gwthread_join(smpp->transmit_reader);
    if (smpp->receive_reader != -1)
    	gwthread_join(smpp->receive_reader);
    if (smpp->writer != -1)
    	gwthread_join(smpp->writer);

    smpp->quitting = 0;
    smpp->transmit_reader = -1;
    smpp->receive_reader = -1;
    smpp->writer = -1;
    list_add_producer(smpp->msgs_to_send);
    semaphore_destroy(smpp->send_semaphore);
    smpp->send_semaphore = semaphore_create(0);
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


struct reader_arg {
    SMPP *smpp;
    Connection *conn;
};


static struct reader_arg *create_reader_arg(SMPP *smpp, Connection *conn)
{
    struct reader_arg *arg;
    
    arg = gw_malloc(sizeof(*arg));
    arg->smpp = smpp;
    arg->conn = conn;
    return arg;
}


static void destroy_reader_arg(struct reader_arg *arg)
{
    gw_free(arg);
}


static void read_thread(void *pointer)
{
    long len;
    Octstr *os_resp;
    SMPP_PDU *pdu;
    SMPP_PDU *resp;
    int ret;
    SMPP *smpp;
    Connection *conn;
    struct reader_arg *arg;
    long i;
    
    arg = pointer;
    smpp = arg->smpp;
    conn = arg->conn;
    destroy_reader_arg(arg);

    len = 0;
    resp = NULL;
    
    while (!smpp->quitting) {
	ret = conn_wait(conn, -1.0);
	if (ret == -1)
	    break;

    	while ((ret = read_pdu(conn, &len, &pdu)) == 1) {
	    dump_pdu("Got PDU:", pdu);
	    switch (pdu->type) {
	    case deliver_sm:
    	    	/* XXX UDH */
		list_produce(smpp->received_msgs, pdu_to_msg(pdu));
		
		resp = smpp_pdu_create(deliver_sm_resp, 
					pdu->u.deliver_sm.sequence_number);
		os_resp = smpp_pdu_pack(resp);
		gw_assert(os_resp != NULL);
		conn_write(conn, os_resp);
		octstr_destroy(os_resp);
		break;
		
    	    case enquire_link:
	    	resp = smpp_pdu_create(enquire_link_resp, 
		    	    	       pdu->u.enquire_link.sequence_number);
    	    	os_resp = smpp_pdu_pack(resp);
		gw_assert(os_resp != NULL);
		conn_write(conn, os_resp);
		octstr_destroy(os_resp);
	    	break;

    	    case enquire_link_resp:
	    	break;

	    case submit_sm_resp:
		if (pdu->u.submit_sm_resp.command_status != 0) {
		    error(0, "SMPP: SMSC returned error code 0x%08lu "
		    	     "in response to submit_sm.",
			     pdu->u.submit_sm_resp.command_status);
		}
		semaphore_up(smpp->send_semaphore);
		break;
    
	    case bind_transmitter_resp:
		if (pdu->u.bind_transmitter_resp.command_status != 0) {
		    error(0, "SMPP: SMSC rejected login to transmit, "
		    	     "code 0x%08lx.",
			     pdu->u.bind_transmitter_resp.command_status);
		} else {
		    for (i = 0; i < MAX_PENDING_SUBMITS; ++i)
			semaphore_up(smpp->send_semaphore);
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
	    
	    smpp_pdu_destroy(pdu);
	    smpp_pdu_destroy(resp);
	    resp = NULL;
	}

	if (ret == -1)
	    break;
    }
}


static void write_enquire_link_thread(void *arg)
{
    SMPP *smpp;
    SMPP_PDU *pdu;
    Octstr *os;
    
    smpp = arg;

    while (!smpp->quitting) {
	pdu = smpp_pdu_create(enquire_link, 
			      counter_increase(smpp->message_id_counter));
    	dump_pdu("Sending enquire link:", pdu);
    	os = smpp_pdu_pack(pdu);
	conn_write(smpp->transmission, os);
	octstr_destroy(os);
	smpp_pdu_destroy(pdu);
	gwthread_sleep(60.0);
    }
}


static void write_thread(void *arg)
{
    Msg *msg;
    SMPP_PDU *pdu;
    Octstr *os;
    SMPP *smpp;
    long child;

    smpp = arg;
    child = gwthread_create(write_enquire_link_thread, smpp);

    while (!smpp->quitting) {
	semaphore_down(smpp->send_semaphore);
	if (smpp->quitting)
	    break;

    	msg = list_consume(smpp->msgs_to_send);
	if (msg == NULL)
	    break;

	pdu = smpp_pdu_create(submit_sm, 
	    	    	       counter_increase(smpp->message_id_counter));
	pdu->u.submit_sm.source_addr = msg->sms.sender;
	msg->sms.sender = NULL;
	pdu->u.submit_sm.destination_addr = msg->sms.receiver;
	msg->sms.receiver = NULL;
    	if (msg->sms.flag_udh) {
	    pdu->u.submit_sm.short_message =
	    	octstr_format("%S%S", msg->sms.udhdata, msg->sms.msgdata);
	    pdu->u.submit_sm.esm_class = SMPP_ESM_CLASS_UDH_INDICATOR;
	    pdu->u.submit_sm.data_coding = SMPP_DATA_CODING_FOR_UDH;
	} else {
	    pdu->u.submit_sm.short_message = msg->sms.msgdata;
    	    charset_latin1_to_gsm(pdu->u.submit_sm.short_message);
	    msg->sms.msgdata = NULL;
	}
	msg_destroy(msg);
	
	os = smpp_pdu_pack(pdu);
	conn_write(smpp->transmission, os);
	octstr_destroy(os);

    	dump_pdu("Sent PDU:", pdu);
	smpp_pdu_destroy(pdu);
    }

    gwthread_wakeup(child);
    gwthread_join(child);
}


static void smpp_quit(SMPP *smpp)
{
    smpp_wakeup_for_quit(smpp);

    conn_destroy(smpp->transmission);
    conn_destroy(smpp->reception);
    smpp->transmission = NULL;
    smpp->reception = NULL;
}


static int smpp_reconnect(SMPP *smpp)
{
    SMPP_PDU *bind;
    Octstr *os;

    smpp_quit(smpp);

    smpp->transmission = conn_open_tcp(smpp->host, smpp->transmit_port);
    if (smpp->transmission == NULL) {
    	error(0, "SMPP: Couldn't connect to server.");
	return -1;
    }

    smpp->reception = conn_open_tcp(smpp->host, smpp->receive_port);
    if (smpp->reception == NULL) {
    	error(0, "SMPP: Couldn't connect to server.");
    	conn_destroy(smpp->transmission);
	smpp->transmission = NULL;
	return -1;
    }
    
    bind = smpp_pdu_create(bind_transmitter,
    	    	    	    counter_increase(smpp->message_id_counter));
    bind->u.bind_transmitter.system_id = octstr_duplicate(smpp->username);
    bind->u.bind_transmitter.password = octstr_duplicate(smpp->password);
    bind->u.bind_transmitter.system_type = octstr_create("VMA");
    bind->u.bind_transmitter.interface_version = 0x34;
    dump_pdu("Sending:", bind);
    os = smpp_pdu_pack(bind);
    conn_write(smpp->transmission, os);
    smpp_pdu_destroy(bind);
    octstr_destroy(os);

    bind = smpp_pdu_create(bind_receiver,
    	    	    	    counter_increase(smpp->message_id_counter));
    bind->u.bind_receiver.system_id = octstr_duplicate(smpp->username);
    bind->u.bind_receiver.password = octstr_duplicate(smpp->password);
    bind->u.bind_receiver.system_type = octstr_create("VMA");
    bind->u.bind_receiver.interface_version = 0x34;
    dump_pdu("Sending:", bind);
    os = smpp_pdu_pack(bind);
    conn_write(smpp->reception, os);
    smpp_pdu_destroy(bind);
    octstr_destroy(os);

    smpp->transmit_reader = 
    	gwthread_create(read_thread, 
	    	    	create_reader_arg(smpp, smpp->transmission));
    smpp->receive_reader = 
    	gwthread_create(read_thread, 
	    	    	create_reader_arg(smpp, smpp->reception));
    smpp->writer = gwthread_create(write_thread, smpp);
    
    return 0;
}


/***********************************************************************
 * Public interface. This version is suitable for the Kannel bearerbox
 * SMSC interface from the summer of 1999.
 */


SMSCenter *smpp_open(char *host, int port, char *system_id, char *password,
    	    	     char *system_type, char *address_range,
		     int receive_port)
{
    SMSCenter *smsc;
    Octstr *os_host, *os_username, *os_password;
    
    smsc = smscenter_construct();
    smsc->type = SMSC_TYPE_SMPP_IP;
    sprintf(smsc->name, "SMPP:%s:%i/%i:%s:%s", host, port,
	    (receive_port ? receive_port : port), system_id, system_type);

    os_host = octstr_create(host);
    os_username = octstr_create(system_id);
    os_password = octstr_create(password);
    smsc->smpp = smpp_create(os_host, port, receive_port, os_username, 
    	    	       os_password);
    octstr_destroy(os_host);
    octstr_destroy(os_username);
    octstr_destroy(os_password);
    
    if (smpp_reconnect(smsc->smpp) == -1) {
	smpp_destroy(smsc->smpp);
    	smscenter_destruct(smsc);
	return NULL;
    }

    return smsc;
}


int smpp_reopen(SMSCenter *smsc)
{
    return smpp_reconnect(smsc->smpp);
}


int smpp_close(SMSCenter *smsc)
{
    smpp_quit(smsc->smpp);
    smpp_destroy(smsc->smpp);
    smscenter_destruct(smsc);
    return 0;
}


int smpp_submit_msg(SMSCenter *smsc, Msg *msg)
{
    list_produce(smsc->smpp->msgs_to_send, msg_duplicate(msg));
    return 0;
}


int smpp_receive_msg(SMSCenter *smsc, Msg **msg)
{
    *msg = list_consume(smsc->smpp->received_msgs);
    if (*msg == NULL)
    	return -1;
    return 1;
}


int smpp_pending_smsmessage(SMSCenter *smsc)
{
    return list_len(smsc->smpp->received_msgs) > 0;
}
