/*
 * smsc_smpp.c - SMPP v3.3 and v3.4 implementation
 *
 * Lars Wirzenius
 */
 
/* XXX check SMSCConn conformance */
/* XXX UDH reception */
/* XXX check UDH sending fields esm_class and data_coding from GSM specs */
/* XXX charset conversions on incoming messages (didn't work earlier, 
       either) */
/* XXX numbering plans and type of number: check spec */
 
#include "gwlib/gwlib.h"
#include "msg.h"
#include "smsc_p.h"
#include "smpp_pdu.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "sms.h"
#include "dlr.h"

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
    Octstr *system_type;
    Octstr *username;
    Octstr *password;
    Octstr *address_range;
    Octstr *our_host;
    int source_addr_ton;
    int source_addr_npi;
    int dest_addr_ton;
    int dest_addr_npi;
    int transmit_port;
    int receive_port;
    int quitting;
    SMSCConn *conn;
} SMPP;


static SMPP *smpp_create(SMSCConn *conn, Octstr *host, int transmit_port, 
    	    	    	 int receive_port, Octstr *system_type, 
                         Octstr *username, Octstr *password,
    	    	    	 Octstr *address_range, Octstr *our_host, 
                         int source_addr_ton, int source_addr_npi, 
                         int dest_addr_ton, int dest_addr_npi)
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
    smpp->system_type = octstr_duplicate(system_type);
    smpp->username = octstr_duplicate(username);
    smpp->password = octstr_duplicate(password);
    smpp->address_range = octstr_duplicate(address_range);
    smpp->source_addr_ton = source_addr_ton;
    smpp->source_addr_npi = source_addr_npi;
    smpp->dest_addr_ton = dest_addr_ton;
    smpp->dest_addr_npi = dest_addr_npi;
    smpp->our_host = octstr_duplicate(our_host);
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
	octstr_destroy(smpp->system_type);
	octstr_destroy(smpp->address_range);
	octstr_destroy(smpp->our_host);
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
 
    /* Check for manual override of source ton and npi values */
    if(smpp->source_addr_ton > -1 && smpp->source_addr_npi > -1) {
        pdu->u.submit_sm.source_addr_ton = smpp->source_addr_ton;
        pdu->u.submit_sm.source_addr_npi = smpp->source_addr_npi;
        debug("bb.sms.smpp", 0, "Manually forced source addr ton = %d, source add npi = %d",
            smpp->source_addr_ton, smpp->source_addr_npi);
    } else {
        /* setup default values */
        pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */
        pdu->u.submit_sm.source_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */

        /* lets see if its international or alphanumeric sender */
        if (octstr_get_char(pdu->u.submit_sm.source_addr,0) == '+') {
            if (!octstr_check_range(pdu->u.submit_sm.source_addr, 1, 256, gw_isdigit)) {
                pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC; /* alphanum */
                pdu->u.submit_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN;    /* short code */
            } else {
               /* numeric sender address with + in front -> international (remove the +) */
               octstr_delete(pdu->u.submit_sm.source_addr, 0, 1);
               pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_INTERNATIONAL;
    	    }
        } else {
            if (!octstr_check_range(pdu->u.submit_sm.source_addr,0, 256, gw_isdigit)) {
                pdu->u.submit_sm.source_addr_ton = GSM_ADDR_TON_ALPHANUMERIC;
                pdu->u.submit_sm.source_addr_npi = GSM_ADDR_NPI_UNKNOWN;
            }
        }
    }

    /* Check for manual override of destination ton and npi values */
    if (smpp->dest_addr_ton > -1 && smpp->dest_addr_npi > -1) {
        pdu->u.submit_sm.dest_addr_ton = smpp->dest_addr_ton;
        pdu->u.submit_sm.dest_addr_npi = smpp->dest_addr_npi;
        debug("bb.sms.smpp", 0, "Manually forced dest addr ton = %d, source add npi = %d",
            smpp->dest_addr_ton, smpp->dest_addr_npi);
    } else {
        pdu->u.submit_sm.dest_addr_ton = GSM_ADDR_TON_NATIONAL; /* national */
        pdu->u.submit_sm.dest_addr_npi = GSM_ADDR_NPI_E164; /* ISDN number plan */
    }

    /*
     * if its a international number starting with +, lets remove the
     * '+' and set number type to international instead 
     */
    if( octstr_get_char(pdu->u.submit_sm.destination_addr,0) == '+') {
    	octstr_delete(pdu->u.submit_sm.destination_addr, 0,1);
    	pdu->u.submit_sm.dest_addr_ton = GSM_ADDR_TON_INTERNATIONAL;
    }

    pdu->u.submit_sm.data_coding = fields_to_dcs(msg, 0);

    if (octstr_len(msg->sms.udhdata)) {
        pdu->u.submit_sm.short_message =
	       octstr_format("%S%S", msg->sms.udhdata, msg->sms.msgdata);
        pdu->u.submit_sm.esm_class = SMPP_ESM_CLASS_UDH_INDICATOR;
    } else {
        pdu->u.submit_sm.short_message = octstr_duplicate(msg->sms.msgdata);
        if (pdu->u.submit_sm.data_coding == 0 ) /* no reencoding for unicode! */
            charset_latin1_to_gsm(pdu->u.submit_sm.short_message);		
    }
    /* ask for the delivery reports if needed */
    if (msg->sms.dlr_mask & (DLR_SUCCESS|DLR_FAIL))
        pdu->u.submit_sm.registered_delivery = 1; 

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


static int send_pdu(Connection *conn, SMPP_PDU *pdu)
{
    Octstr *os;
    int ret;
    
    dump_pdu("Sending PDU:", pdu);
    os = smpp_pdu_pack(pdu);
    ret = conn_write(conn, os);   /* Caller checks for write errors later */
    octstr_destroy(os);
    return ret;
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

    conn = conn_open_tcp(smpp->host, smpp->transmit_port, smpp->our_host );
    if (conn == NULL) {
    	error(0, "SMPP: Couldn't connect to server.");
	return NULL;
    }
    
    bind = smpp_pdu_create(bind_transmitter,
			   counter_increase(smpp->message_id_counter));
    bind->u.bind_transmitter.system_id = octstr_duplicate(smpp->username);
    bind->u.bind_transmitter.password = octstr_duplicate(smpp->password);
    if (smpp->system_type == NULL)
	bind->u.bind_transmitter.system_type = octstr_create("VMA");
    else
	bind->u.bind_transmitter.system_type = 
	    octstr_duplicate(smpp->system_type);
    bind->u.bind_transmitter.interface_version = 0x34;
    bind->u.bind_transmitter.address_range = 
    	octstr_duplicate(smpp->address_range);
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

    conn = conn_open_tcp(smpp->host, smpp->receive_port, smpp->our_host );
    if (conn == NULL) {
    	error(0, "SMPP: Couldn't connect to server.");
	return NULL;
    }
    
    bind = smpp_pdu_create(bind_receiver,
			   counter_increase(smpp->message_id_counter));
    bind->u.bind_receiver.system_id = octstr_duplicate(smpp->username);
    bind->u.bind_receiver.password = octstr_duplicate(smpp->password);
    if (smpp->system_type == NULL)
	bind->u.bind_receiver.system_type = octstr_create("VMA");
    else
	bind->u.bind_receiver.system_type = 
	    octstr_duplicate(smpp->system_type);
    bind->u.bind_receiver.interface_version = 0x34;
    bind->u.bind_receiver.address_range = 
    	octstr_duplicate(smpp->address_range);
    send_pdu(conn, bind);
    smpp_pdu_destroy(bind);

    return conn;
}


static void handle_pdu(SMPP *smpp, Connection *conn, SMPP_PDU *pdu, 
    	    	       long *pending_submits)
{
    SMPP_PDU *resp;
    Octstr *os;
    Msg *msg, *dlrmsg=NULL;
    long reason;
    int idx; 
    int len;
    resp = NULL;

    switch (pdu->type) {
    case deliver_sm:
	/* XXX UDH */
	/* bb_smscconn_receive can fail, but we ignore that since we
	   have no way to usefull tell the SMS center about this
	   (no suitable error code for the deliver_sm_resp is defined) */
         /* got a deliver ack? */
         if ((pdu->u.deliver_sm.esm_class == 0x02 || pdu->u.deliver_sm.esm_class == 0x04))
         {
 	    Octstr *reply, *respstr;    	
 	    Octstr *msgid = NULL;
 	    Octstr *stat = NULL;
 	    int dlrstat;
 	    long curr=0, vpos=0;
     		
     	    debug("smsc_smpp.handle_pdu",0,"**********>>>>>>>>>>>>>>  SMPP handle_pdu Got DELIVER REPORT\n");
     					
 	    respstr = pdu->u.deliver_sm.short_message;
 		
 	    /* get server message id */
   	    if ((curr = octstr_search(respstr, octstr_imm("id:"), 0)) != -1)
     	    {   
	        vpos = octstr_search_char(respstr, ' ',curr );
    	        if ((vpos-curr >0) && (vpos != -1))
 	           msgid = octstr_copy(respstr, curr+3, vpos-curr-3);
 	    }
 	    else
 	    {
 	        msgid = NULL;
 	    }  		
 	    /* get err & status code */
 	    if ((curr = octstr_search(respstr, octstr_imm("stat:"), 0)) != -1)
 	    {  
 	        vpos = octstr_search_char(respstr, ' ',curr );
 	        if ((vpos-curr >0) && (vpos != -1))
 		    stat = octstr_copy(respstr, curr+5, vpos-curr-5);
 	    }
 	    else
 	    {
 	        stat = NULL;
 	    }	
 	     /* we get the following status: DELIVRD, ACCEPTD, 
 	     EXPIRED, DELETED, UNDELIV, UNKNOWN, REJECTD */
 		
 	    if ((stat != NULL) && ((octstr_compare(stat,octstr_imm("DELIVRD"))==0)
 	        || (octstr_compare(stat,octstr_imm("ACCEPTD"))==0)))
	        dlrstat = DLR_SUCCESS;
 	    else
 	        dlrstat = DLR_FAIL;
 			
 	    if (msgid !=NULL)
 	    {
 	        Octstr *tmp;
 	        tmp = octstr_format("%ld",strtol(octstr_get_cstr(msgid),NULL,10));
 	        dlrmsg = dlr_find(octstr_get_cstr(smpp->conn->id), 
 		    octstr_get_cstr(tmp), /* smsc message id */
 		    octstr_get_cstr(pdu->u.deliver_sm.destination_addr), /* destination */
 		    dlrstat);
                octstr_destroy(tmp);
 	    }
 	    if (dlrmsg != NULL)
 	    {
 	        reply = octstr_duplicate(respstr);
 	        /* having a / in the text breaks it so lets replace it with a space */
	        len = octstr_len(reply);
		for(idx=0;idx<len;idx++)
	    	if(octstr_get_char(reply,idx)=='/')
	    	    octstr_set_char(reply,idx,'.');
 	        octstr_append_char(reply, '/');
 	        octstr_insert(dlrmsg->sms.msgdata, reply, 0);
 	        octstr_destroy(reply);
 	        bb_smscconn_receive(smpp->conn, dlrmsg);
	    }
 	    else
	    {
	    	error(0,"Got DELIV REPORT but couldnt find message or was not interested in it");    	
 	    }		
     	    resp = smpp_pdu_create(deliver_sm_resp, 
            pdu->u.deliver_sm.sequence_number);
 					       
 	    if (msgid != NULL)
 	    	octstr_destroy(msgid);	    
 	    if (stat != NULL)
 	    	octstr_destroy(stat);
 	    
 	}
 	else /* MO-SMS */
 	{
        /* ensure the smsc-id is set */
        msg = pdu_to_msg(pdu);
        time(&msg->sms.time);
        msg->sms.smsc_id = octstr_duplicate(smpp->conn->id);
	    (void) bb_smscconn_receive(smpp->conn, msg);
	    resp = smpp_pdu_create(deliver_sm_resp, 
		                       pdu->u.deliver_sm.sequence_number);
	}
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
	} else if (pdu->u.submit_sm_resp.command_status != 0)
	{
	    error(0, "SMPP: SMSC returned error code 0x%08lu "
		     "in response to submit_sm.",
		     pdu->u.submit_sm_resp.command_status);
	    reason = smpp_status_to_smscconn_failure_reason(
			pdu->u.submit_sm.command_status);

 	    /* gen DLR_SMSC_FAIL */		
 	    if (msg->sms.dlr_mask & (DLR_SMSC_FAIL|DLR_FAIL))
 	    {
 		Octstr *reply;
 		
 		reply = octstr_format("0x%08lu",pdu->u.submit_sm_resp.command_status);
 		/* generate DLR */
 		info(0,"creating DLR message");
 		dlrmsg = msg_create(sms);
 		dlrmsg->sms.service = octstr_duplicate(msg->sms.service);
 		dlrmsg->sms.dlr_mask = DLR_SMSC_FAIL;
 		dlrmsg->sms.sms_type = report;
 		dlrmsg->sms.smsc_id = octstr_duplicate(smpp->conn->id);
 		dlrmsg->sms.sender = octstr_duplicate(msg->sms.receiver);
 		dlrmsg->sms.receiver = octstr_create("000");
 		dlrmsg->sms.msgdata = octstr_duplicate(msg->sms.dlr_url);
 		time(&msg->sms.time);
 			
 		octstr_append_char(reply, '/');
 		octstr_insert(dlrmsg->sms.msgdata, reply, 0);
 		octstr_destroy(reply);
 			
 		info(0,"DLR = %s",octstr_get_cstr(dlrmsg->sms.msgdata));
 			bb_smscconn_receive(smpp->conn, dlrmsg);
 	    }
 	    else
 	    {
	        bb_smscconn_send_failed(smpp->conn, msg, reason);
	    }
	    --(*pending_submits);
	}
 	else 
 	{ 
	    Octstr *tmp;
	
	    /* deliver gives mesg id in decimal, submit_sm in hex.. */
	    tmp = octstr_format("%ld",strtol(octstr_get_cstr(pdu->u.submit_sm_resp.message_id),NULL,16));
	    /* SMSC ACK.. now we have the message id. */
 				
	    if (msg->sms.dlr_mask & (DLR_SMSC_SUCCESS|DLR_SUCCESS|DLR_FAIL|DLR_BUFFERED))
 		dlr_add(octstr_get_cstr(smpp->conn->id),
	    	octstr_get_cstr(tmp),
	    octstr_get_cstr(msg->sms.receiver),
            octstr_get_cstr(msg->sms.service),
            octstr_get_cstr(msg->sms.dlr_url),
            msg->sms.dlr_mask);
 
 	    /* gen DLR_SMSC_SUCCESS */
 	    if (msg->sms.dlr_mask & DLR_SMSC_SUCCESS)
 	    {
 		Octstr *reply;
 		
 		reply = octstr_format("0x%08lu",pdu->u.submit_sm_resp.command_status);
 
 		dlrmsg = dlr_find(octstr_get_cstr(smpp->conn->id), 
 		    octstr_get_cstr(tmp), /* smsc message id */
 		    octstr_get_cstr(msg->sms.receiver), /* destination */
 		    (DLR_SMSC_SUCCESS|((msg->sms.dlr_mask & (DLR_SUCCESS|DLR_FAIL))?DLR_BUFFERED:0)));
 			
 		if (dlrmsg != NULL)
 		{
 		    octstr_append_char(reply, '/');
 		    octstr_insert(dlrmsg->sms.msgdata, reply, 0);
 		    octstr_destroy(reply);
 		    bb_smscconn_receive(smpp->conn, dlrmsg);
 		}
 		else
 		    error(0,"Got SMSC_ACK but couldnt find message");
 	    }
 	    octstr_destroy(tmp);
	    bb_smscconn_sent(smpp->conn, msg);
	    --(*pending_submits);
	} /* end if for SMSC ACK */
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
    double timeout;

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
    	for (;;) {
	    timeout = last_enquire_sent + SMPP_ENQUIRE_LINK_INTERVAL 
	    	    	    - date_universal_now();
    	    if (smpp->quitting || conn_wait(conn, timeout) == -1)
	    	break;

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
    gwthread_wakeup(smpp->transmitter);
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
    Octstr *address_range;
    long source_addr_ton;
    long source_addr_npi;
    long dest_addr_ton;
    long dest_addr_npi;
    Octstr *our_host;
    SMPP *smpp;
    int ok;
    
    host = cfg_get(grp, octstr_imm("host"));
    if (cfg_get_integer(&port, grp, octstr_imm("port")) == -1)
    	port = 0;
    if (cfg_get_integer(&receive_port, grp, octstr_imm("receive-port")) == -1)
    	receive_port = 0;
    username = cfg_get(grp, octstr_imm("smsc-username"));
    password = cfg_get(grp, octstr_imm("smsc-password"));
    system_type = cfg_get(grp, octstr_imm("system-type"));
    address_range = cfg_get(grp, octstr_imm("address-range"));
    our_host = cfg_get(grp, octstr_imm("our-host"));
    
    system_id = cfg_get(grp, octstr_imm("system-id"));
    if (system_id != NULL) {
	warning(0, "SMPP: obsolete system-id variable is set, "
	    	   "use smsc-username instead.");
    	if (username == NULL) {
	    warning(0, "SMPP: smsc-username not set, using system-id instead");
	    username = system_id;
	} else
	    octstr_destroy(system_id);
    }
    
    /* Check that config is OK */
    ok = 1;
    if (host == NULL) {
        error(0,"SMPP: Configuration file doesn't specify host");
        ok = 0;
    }    
    if (username == NULL) {
	   error(0, "SMPP: Configuration file doesn't specify username.");
	   ok = 0;
    }
    if (password == NULL) {
	   error(0, "SMPP: Configuration file doesn't specify password.");
	   ok = 0;
    }
    if (!ok)
    	return -1;

    /* if the ton and npi values are forced, set them, else set them to -1 */
    if (cfg_get_integer(&source_addr_ton, grp, octstr_imm("source-addr-ton")) == -1)
       source_addr_ton = -1;
    if (cfg_get_integer(&source_addr_npi, grp, octstr_imm("source-addr-npi")) == -1)
       source_addr_npi = -1;
    if (cfg_get_integer(&dest_addr_ton, grp, octstr_imm("dest-addr-ton")) == -1)
       dest_addr_ton = -1;
    if (cfg_get_integer(&dest_addr_npi, grp, octstr_imm("dest-addr-npi")) == -1)
       dest_addr_npi = -1;

    smpp = smpp_create(conn, host, port, receive_port, system_type, 
    	    	       username, password, address_range, our_host,
                       source_addr_ton, source_addr_npi, dest_addr_ton, 
                       dest_addr_npi);

    conn->data = smpp;
    conn->name = octstr_format("SMPP:%S:%d/%d:%S:%S", 
    	    	    	       host, port,
			       (receive_port ? receive_port : port), 
			       username, system_type);

    octstr_destroy(host);
    octstr_destroy(username);
    octstr_destroy(password);
    octstr_destroy(system_type);
    octstr_destroy(address_range);
    octstr_destroy(our_host);

    conn->status = SMSCCONN_CONNECTING;
      
    /*
     * I/O threads are only started if the corresponding ports
     * have been configured with positive numbers. Use 0 to 
     * disable the creation of the corresponding thread.
     */
    if (port != 0)
        smpp->transmitter = gwthread_create(io_thread, io_arg_create(smpp, 1));
    if (receive_port != 0)
        smpp->receiver = gwthread_create(io_thread, io_arg_create(smpp, 0));
    
    if ((port != 0 && smpp->transmitter == -1) || 
        (receive_port != 0 && smpp->receiver == -1)) {
    	error(0, "SMPP: Couldn't start I/O threads.");
	smpp->quitting = 1;
	if (smpp->transmitter != -1) {
	    gwthread_wakeup(smpp->transmitter);
	    gwthread_join(smpp->transmitter);
	}
	if (smpp->receiver != -1) {
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
