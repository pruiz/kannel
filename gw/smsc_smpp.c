/*****************************************************************************
* smsc_smpp.c - Short Message Peer to Peer Protocol 3.3
* Mikael Gueck for WapIT Ltd.
*/

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "gwlib/gwlib.h"
#include "smsc.h"
#include "smsc_smpp.h"

static int actively_bind(SMSCenter*);
static int deliver_sm_to_msg(Msg**, smpp_pdu_deliver_sm*);
static int smpp_extract_message_udh(Octstr**, Octstr*);

SMSCenter *smpp_open(char *host, int port, char *system_id, char *password,
		     char* system_type, char *address_range, int receive_port) {

	SMSCenter *smsc = smscenter_construct();

	smsc->type = SMSC_TYPE_SMPP_IP;
	sprintf(smsc->name, "SMPP:%s:%i/%i:%s:%s", host, port,
		(receive_port ? receive_port : port), system_id, system_type);

	smsc->hostname = gw_strdup(host);
	smsc->port = port;
	smsc->receive_port = (receive_port ? receive_port : port);

	smsc->smpp_system_id = (system_id != NULL) ? gw_strdup(system_id) : NULL;
	smsc->smpp_system_type = (system_type != NULL) ? gw_strdup(system_type) : NULL;
	smsc->smpp_password = (password != NULL) ? gw_strdup(password) : NULL;
	smsc->smpp_address_range = 
		(address_range != NULL) ? gw_strdup(address_range) : NULL;

	/* Create FIFO stacks */
	smsc->unsent_mt    = list_create();
	smsc->sent_mt      = list_create();
	smsc->delivered_mt = list_create();
	smsc->received_mo  = list_create();
	smsc->fifo_t_in    = list_create();
	smsc->fifo_t_out   = list_create();
	smsc->fifo_r_in    = list_create();
	smsc->fifo_r_out   = list_create();

	/* Create buffers */
	smsc->data_t = data_new();
	smsc->data_r = data_new();

	/* Open the transmitter connection */
	smsc->fd_t = tcpip_connect_to_server(smsc->hostname, smsc->port);
	if(smsc->fd_t == -1) goto error;
	smsc->smpp_t_state = SMPP_STATE_CONNECTED;

	/* Open the receiver connection */
	smsc->fd_r = tcpip_connect_to_server(smsc->hostname, smsc->receive_port);
	if(smsc->fd_r == -1) goto error;
	smsc->smpp_r_state = SMPP_STATE_CONNECTED;

	actively_bind(smsc);

	/* Done, return */
	return smsc;

error:
	error(0, "smpp_open: could not open");

	/* Destroy FIFO stacks. */
	list_destroy(smsc->unsent_mt, NULL);
	list_destroy(smsc->sent_mt, NULL);
	list_destroy(smsc->delivered_mt, NULL);
	list_destroy(smsc->received_mo, NULL);
	list_destroy(smsc->fifo_t_in, NULL);
	list_destroy(smsc->fifo_t_out, NULL);
	list_destroy(smsc->fifo_r_in, NULL);
	list_destroy(smsc->fifo_r_out, NULL);

	/* Destroy buffers */
	data_free(smsc->data_t);
	data_free(smsc->data_r);

	smscenter_destruct(smsc);
	return NULL;
}

int smpp_reopen(SMSCenter *smsc) {

	gw_assert(smsc==NULL);

	/* Destroy buffers */
	data_free(smsc->data_t);
	data_free(smsc->data_r);

	/* Close sockets */
	close(smsc->fd_t);
	close(smsc->fd_r);

	/* Open the transmitter connection */
	smsc->fd_t = tcpip_connect_to_server(smsc->hostname, smsc->port);
	if(smsc->fd_t == -1) goto error;
	smsc->smpp_t_state = SMPP_STATE_CONNECTED;

	/* Open the receiver connection */
	smsc->fd_r = tcpip_connect_to_server(smsc->hostname, smsc->port);
	if(smsc->fd_r == -1) goto error;
	smsc->smpp_r_state = SMPP_STATE_CONNECTED;

	actively_bind(smsc);
	
	/* Done, return */
	return 1;

error:
	error(0, "smpp_reopen: could not open");

	/* Destroy FIFO stacks. */
	list_destroy(smsc->unsent_mt, NULL);
	list_destroy(smsc->sent_mt, NULL);
	list_destroy(smsc->delivered_mt, NULL);
	list_destroy(smsc->received_mo, NULL);
	list_destroy(smsc->fifo_t_in, NULL);
	list_destroy(smsc->fifo_t_out, NULL);
	list_destroy(smsc->fifo_r_in, NULL);
	list_destroy(smsc->fifo_r_out, NULL);

	/* Destroy buffers */
	data_free(smsc->data_t);
	data_free(smsc->data_r);

	smscenter_destruct(smsc);

	return -1;
}

static int actively_bind(SMSCenter *smsc) {

	struct smpp_pdu *pdu = NULL;
	struct smpp_pdu_bind_receiver *bind_receiver = NULL;
	struct smpp_pdu_bind_transmitter *bind_transmitter = NULL;

	/* Push a BIND_RECEIVER PDU on the [smsc->unsent] stack. */
	pdu = pdu_new();
	pdu->id = SMPP_BIND_RECEIVER;
	smsc->seq_r = 1;
	pdu->sequence_no = 1;
	bind_receiver = gw_malloc(sizeof(struct smpp_pdu_bind_receiver));
	memset(bind_receiver, 0, sizeof(struct smpp_pdu_bind_receiver));
	strncpy(bind_receiver->system_id, smsc->smpp_system_id, 16);
	strncpy(bind_receiver->password, smsc->smpp_password, 9);
	strncpy(bind_receiver->system_type, smsc->smpp_system_type, 13);
	strncpy(bind_receiver->address_range, smsc->smpp_address_range, 41);
	pdu->message_body = bind_receiver;
	list_produce(smsc->fifo_r_out, pdu);

	/* Push a BIND_TRANSMITTER PDU on the [smsc->unsent] stack. */
	pdu = pdu_new();
	pdu->id = SMPP_BIND_TRANSMITTER;
	smsc->seq_t = 1;
	pdu->sequence_no = 1;
	bind_transmitter = gw_malloc(sizeof(struct smpp_pdu_bind_transmitter));
	memset(bind_transmitter, 0, sizeof(struct smpp_pdu_bind_transmitter));
	strncpy(bind_transmitter->system_id, smsc->smpp_system_id, 16);
	strncpy(bind_transmitter->password, smsc->smpp_password, 9);
	strncpy(bind_transmitter->system_type, smsc->smpp_system_type, 13);
	strncpy(bind_transmitter->address_range, smsc->smpp_address_range, 41);
	pdu->message_body = bind_transmitter;
	list_produce(smsc->fifo_t_out, pdu);
	
	return 1;
}

int smpp_close(SMSCenter *smsc) {
	struct smpp_pdu *pdu = NULL;

	gw_assert(smsc != NULL);
	
	debug("bb.sms.smpp", 0, "smpp_close: closing");

	/* Push a UNBIND PDU on the [smsc->fifo_r_out] stack. */
	pdu = pdu_new();
	pdu->id = SMPP_UNBIND;
	pdu->length = 16;
	pdu->status = 0;
	pdu->sequence_no = 1;
	pdu->message_body = NULL;
	list_produce(smsc->fifo_r_out, pdu);

	/* Push a UNBIND PDU on the [smsc->fifo_t_out] stack. */
	pdu = pdu_new();
	pdu->id = SMPP_UNBIND;
	pdu->length = 16;
	pdu->status = 0;
	pdu->sequence_no = 1;
	pdu->message_body = NULL;
	list_produce(smsc->fifo_t_out, pdu);

	/* Write out the UNBIND PDUs. */
	smpp_pending_smsmessage(smsc);

	/* Close transmitter connection. */
	close(smsc->fd_t);

	/* Close receiver connection. */
	close(smsc->fd_r);

	/* Destroy FIFO stacks. */
	list_destroy(smsc->unsent_mt, NULL);
	list_destroy(smsc->sent_mt, NULL);
	list_destroy(smsc->delivered_mt, NULL);
	list_destroy(smsc->received_mo, NULL);
	list_destroy(smsc->fifo_t_in, NULL);
	list_destroy(smsc->fifo_t_out, NULL);
	list_destroy(smsc->fifo_r_in, NULL);
	list_destroy(smsc->fifo_r_out, NULL);

	/* Destroy buffers */
	data_free(smsc->data_t);
	data_free(smsc->data_r);

	return 0;
}

int smpp_submit_msg(SMSCenter *smsc, Msg *msg) {

	Octstr *msgtogether = NULL;
	struct smpp_pdu *pdu = NULL;
	struct smpp_pdu_submit_sm *submit_sm = NULL;

	/* Validate *msg. */
	gw_assert(smsc != NULL);
	gw_assert(msg  != NULL);
	gw_assert(msg_type(msg) == sms);

	/* If we cannot really send yet, push message to
	   smsc->unsent_mt where it will stay until
	   smpp_pdu_act_bind_transmitter_resp is called. */

	/* Push a SUBMIT_SM PDU on the smsc->fifo_t_out List. */
	pdu = pdu_new();
	memset(pdu, 0, sizeof(struct smpp_pdu));

	submit_sm = gw_malloc(sizeof(struct smpp_pdu_submit_sm));
	memset(submit_sm, 0, sizeof(struct smpp_pdu_submit_sm));

	octstr_get_many_chars(submit_sm->source_addr, msg->sms.sender, 0, 20);
	octstr_get_many_chars(submit_sm->dest_addr, msg->sms.receiver, 0, 20);

	if(msg->sms.flag_8bit == 1) {
		/* As per GSM 03.38. */
		submit_sm->esm_class = 67;
	} else {
		submit_sm->esm_class = 0;
	}

	if(msg->sms.flag_udh == 1) {
		/* As per GSM 03.38. */
		submit_sm->data_coding = 245;

		submit_sm->sm_length = octstr_len(msg->sms.udhdata) +
					octstr_len(msg->sms.msgdata);

		submit_sm->sm_length = submit_sm->sm_length > 140 ?
					140 : submit_sm->sm_length;
		
		msgtogether = octstr_cat(msg->sms.udhdata, msg->sms.msgdata);
		
		octstr_get_many_chars(submit_sm->short_message, 
			msgtogether, 0, submit_sm->sm_length);

		octstr_destroy(msgtogether);
	} else {

		submit_sm->data_coding = 0;
		
		submit_sm->sm_length = octstr_len(msg->sms.msgdata);
		
		submit_sm->sm_length = submit_sm->sm_length > 160 ?
					160 : submit_sm->sm_length;

		charset_latin1_to_gsm(msg->sms.msgdata);

		octstr_get_many_chars(submit_sm->short_message, 
				msg->sms.msgdata, 0, submit_sm->sm_length);
	}

	submit_sm->source_addr_npi = GSM_ADDR_NPI_UNKNOWN;
	submit_sm->source_addr_ton = GSM_ADDR_TON_UNKNOWN;

	submit_sm->dest_addr_npi = GSM_ADDR_NPI_E164;
	submit_sm->dest_addr_ton = GSM_ADDR_TON_INTERNATIONAL;

	pdu->id = SMPP_SUBMIT_SM;
	pdu->status = 0;
	pdu->sequence_no = smsc->seq_t++;
	pdu->message_body = submit_sm;
	pdu->length = 16 +
		strlen(submit_sm->service_type) + 1 +
		1 + 1 +
		strlen(submit_sm->source_addr) + 1 +
		1 + 1 +
		strlen(submit_sm->dest_addr) + 1 +
		1 + 1 + 1 +
		strlen(submit_sm->schedule_delivery_time) + 1 +
		strlen(submit_sm->validity_period) + 1 +
		1 + 1 + 1 + 1 + 1 +
		submit_sm->sm_length + 1;

	if( smsc->smpp_t_state == SMPP_STATE_BOUND ) {
		/* The message can be sent immediately. */
		list_produce(smsc->fifo_t_out, pdu);
	} else {
		/* The message has to be queued and sent
	   	   upon receiving a BIND_TRANSMITTER_RESP. */
		list_produce(smsc->unsent_mt, pdu);
	}

	return 1;
}

int smpp_receive_msg(SMSCenter *smsc, Msg **msg) {

	struct smpp_pdu *pdu = NULL;
	struct smpp_pdu_deliver_sm *deliver_sm = NULL;

	/* Pop a Msg message from the MSG_MO stack. */
	if ((pdu = list_extract_first(smsc->received_mo))) {

		deliver_sm = (struct smpp_pdu_deliver_sm*) pdu->message_body;
		if(deliver_sm==NULL) goto error;
		deliver_sm_to_msg(msg, deliver_sm);
		pdu_free(pdu);

		return 1;
	}

	return 0;
error:
	error(errno, "smpp_receive_msg: error");
	msg_destroy(*msg);
	return -1;
}

static int deliver_sm_to_msg(Msg **msg, smpp_pdu_deliver_sm *deliver_sm) {

	Octstr *udhdata = NULL;
	Octstr *msgdata = NULL;
	
	*msg = msg_create(sms);

	msgdata = octstr_create_from_data(deliver_sm->short_message,
			deliver_sm->sm_length);

	if((deliver_sm->esm_class & 0x40) == 0x40) {
		(*msg)->sms.flag_udh = 1;
		smpp_extract_message_udh(&udhdata, msgdata);
	} else {
		(*msg)->sms.flag_udh = 0;
		udhdata = octstr_create("");
	}

	if(	(deliver_sm->data_coding == 0xF5) || 
		(deliver_sm->data_coding == 0x02) || 
		(deliver_sm->data_coding == 0x04) ) 
	{
		(*msg)->sms.flag_8bit = 1;
	}
	else 
	{
		(*msg)->sms.flag_8bit = 0;
		/* This _should_ work according to the spec, but
		 * doesn't seem to work with a RL Telepath...
		if(deliver_sm->data_coding == 0x00) {
			octstr_dump(msgdata, 0);
			charset_gsm_to_latin1(msgdata);
			octstr_dump(msgdata, 0);
		}
		*/
		octstr_dump(msgdata, 0);
	}

	(*msg)->sms.sender = octstr_create(deliver_sm->source_addr);
	(*msg)->sms.receiver = octstr_create(deliver_sm->dest_addr);
	(*msg)->sms.msgdata = msgdata;
	(*msg)->sms.udhdata = udhdata;

	return 1;
}

static int smpp_extract_message_udh(Octstr **udh, Octstr *message) {

	long udhlen = octstr_get_char(message, 0) + 1;

	if(udhlen > octstr_len(message)) {
		goto error;
	}

	*udh = octstr_copy(message, 0, udhlen);
	octstr_delete(message, 0, udhlen);
	
	return 0;

error:
	error(0, "smsc.bb.smpp: got improperly formatted UDH from SMSC");
	return -1;
}

int smpp_pending_smsmessage(SMSCenter *smsc) {

	Octstr *data = NULL;
	smpp_pdu *pdu = NULL;
	int ret = 0, funcret = 0;

	/* Process the MT messages. */

	/* Send whatever we need to send */
	while ((pdu = list_extract_first(smsc->fifo_t_out))) {
		/* Encode the PDU to raw data. */
		if( pdu_encode(pdu, &data) == 1 ) {
			/* Send the PDU data. */
			ret = data_send(smsc->fd_t, data);
			data_free(data);
			if(ret==-1) break;
		}
		pdu_free(pdu);
		if(ret==-1) break;
	}

	/* Receive raw data */
	ret = data_receive(smsc->fd_t, smsc->data_t);
	if(ret == -1) {
		warning(0, "smpp_pending_smsmessage: reopening connections");
		smpp_reopen(smsc);
	}

	/* Interpret the raw data */
	while( data_pop(smsc->data_t, &data) == 1 ) { 
		/* Decode the PDU from raw data. */
		if( (ret = pdu_decode(&pdu, data)) ) {
			/* Act on PDU. */
			pdu->fd = smsc->fd_t;
			ret = pdu_act(smsc, pdu);
			pdu_free(pdu);
		}
		data_free(data);
	}

	/* Process the MO messages. */

	ret = data_receive(smsc->fd_r, smsc->data_r);
	if(ret == -1) {
		warning(0, "smpp_pending_smsmessage: reopening connections");
		smpp_reopen(smsc);
	}

	while ((pdu = list_extract_first(smsc->fifo_r_out))) {
		/* Encode the PDU to raw data. */
		if( (ret = pdu_encode(pdu, &data)) ) {
			/* Send the PDU data. */
			ret = data_send(smsc->fd_r, data);
			data_free(data);
			if(ret==-1) break;
		}
		pdu_free(pdu);
		if(ret==-1) break;
	}

	while( data_pop(smsc->data_r, &data) == 1 ) {
		/* Decode the PDU from raw data. */
		if( (ret = pdu_decode(&pdu, data)) ) {
			/* Act on PDU. */
			pdu->fd = smsc->fd_r;
			ret = pdu_act(smsc, pdu);
			pdu_free(pdu);
		}
		data_free(data);
	}

	/* Signal that we got a MO message */
	if(list_len(smsc->received_mo) > 0) funcret = 1;

	/* If it's been a "long time" (defined elsewhere) since
	   the last message, actively check the link status by
	   sending a LINK_STATUS pdu to the SMSC, thereby
	   (maybe) resetting the close-if-no-traffic timer.
	   Note: in practise we won't need this. */

	/* XXX DO IT */

	return funcret;
}

static smpp_pdu* pdu_new(void) {

	struct smpp_pdu *newpdu = NULL;

	newpdu = gw_malloc(sizeof(struct smpp_pdu));
	memset(newpdu, 0, sizeof(struct smpp_pdu));

	return newpdu;
}

static int pdu_free(smpp_pdu *pdu) {

	if(pdu == NULL) return 0;

	gw_free(pdu->message_body);
	gw_free(pdu);

	return 1;
}

static Octstr* data_new(void) {

	struct Octstr *newstr = NULL;

	newstr = octstr_create("");

	return newstr;
}

static int data_free(Octstr *str) {

	if(str == NULL) return 0;

	octstr_destroy(str);

	return 1;
}

/******************************************************************************
* data_pop
*
*  from: The Octstr to cut from
*  to: The Octstr to create and paste to
*
* Description:
*
* Returns:
*  1 if a PDU was found
*  0 if no PDU found
* -1 if an error occurred
*
*/
static int data_pop(Octstr *from, Octstr **to) {
	unsigned char header[4];
	long length;
	long olen;

	olen = octstr_len(from);

	/* Check if [from] has enough data to contain a PDU (>=16 octets) */
	if(olen < 16) goto no_msg;

	/* Read the length (4 first octets) */
	octstr_get_many_chars((char *) &header, from, 0, 4);

	/* Translate the length */
	length = decode_network_long(header);

	if (length < 0)
		goto error;

	/* Check if we have [length] octets of data */
	if(olen < length) goto no_msg;

	/* Cut the PDU out, move data to fill the hole. */
	*to = octstr_copy(from, 0, length);
	if(*to == NULL) goto error;
	octstr_delete(from, 0, length);

	/* Okay, done */
	return 1;

no_msg:
	*to = NULL;
	return -1;

error:
	error(0, "data_pop: ERROR!!!");
	*to = NULL;
	return -1;
}

/******************************************************************************
* data_receive
*
*  fd: the File Descriptor to read from
*  to: The Octstr to append to
*
* Description:
*  Receive data from [fd] and append it to [to].
*
* Returns:
*  1 if a PDU was found
*  0 if no PDU found
* -1 if an error occurred
*
*/
static int data_receive(int fd, Octstr *to) {

	long length;
	char data[1024];

	fd_set rf;
	struct timeval tox;
	int ret;

	memset(data, 0, sizeof(data));

	FD_ZERO(&rf);
	FD_SET(fd, &rf);
	tox.tv_sec = 0;
	tox.tv_usec = 100;
	ret = select(FD_SETSIZE, &rf, NULL, NULL, &tox);

	if (ret == 0) {
		goto no_data;
	} else if (ret < 0) {
		if(errno == EBADF) {
			error(errno, "data_receive: select failed");
		}
		goto error;
	}

	/* Create temp data structures. */
	length = read(fd, data, sizeof(data)-1);

	if(length == -1) {
/*		if(errno==EWOULDBLOCK) return -1; */
		goto error;
	} else if(length == 0) {
		debug("bb.sms.smpp", 0, "other side closed socket <%i>", fd);
		goto error;
	}

	octstr_append_data(to, data, length);

	/* Okay, done */
	return 1;

no_data:
	return 0;

error:
	debug("bb.sms.smpp", 0, "data_receive: error");
	return -1;
}

/******************************************************************************
* data_send
*
*  fd: the File Descriptor to write to
*  to: The Octstr to get the data from
*
* Description:
*  Write all the data from [from] to [fd].
*
* Returns:
*  1 if a PDU was found
*  0 if no PDU found
* -1 if an error occurred
*
*/
static int data_send(int fd, Octstr *from) {

	long length, curl, written = 0;
	char *willy = NULL;

/*	debug("bb.sms.smpp", 0, "data_send: starting"); */

	/* Create temp data structures. */
	length = octstr_len(from);
	if(length <= 0) return 0;

	willy = gw_malloc(length);

	octstr_get_many_chars(willy, from, 0, length);

	/* Write to socket */
	for(;;) {
		if(written >= length) break;
		curl = write(fd, willy+written, length-written);
		if(curl == -1) {
			if(errno==EAGAIN) continue;
			if(errno==EINTR) continue;
			if(errno==EBADF) {
				error(errno, "data_send: write(2) failed");
			}
			goto error;
		} else if(curl == 0) {
			goto socket_b0rken;
		}
		written += curl;
	}

	/* Okay, done */
	gw_free(willy);
	return 1;

socket_b0rken:
	debug("bb.sms.smpp", 0, "data_send: broken socket!!!");
	gw_free(willy);
	return -1;

error:
	debug("bb.sms.smpp", 0, "data_send: ERROR!!!");
	gw_free(willy);
	return -1;
}

/* Append a single octet of data. Update where to reflect the
   next byte to write to, decrease left by the number of
   bytes written. */
static int smpp_append_oct(char** where, int* left, int data) {
	**where = data;
	*where += 1;

	return 1;
}

static int smpp_read_oct(char** where, int* left, Octet* data) {
	return -1;
	smpp_read_oct(where, left, data);
}

static int smpp_append_cstr(char** where, int* left, char* data) {

	memcpy(*where, data, strlen(data)+1);
	*where += (strlen(data) + 1);

	return 1;
}

static int smpp_read_cstr(char** where, int* left, char** data) {
	return -1;
	smpp_read_cstr(where, left, data);
}

/******************************************************************************
* pdu_act
*
*  Decide what to do with a PDU once we have managed to obtain one.
*
* Refer to:
*  SMPP 3.3 specification section 5.5.2
*
* Returns:
*  1 if a PDU was found
*  0 if no PDU found
* -1 if an error occurred
*
*/
static int pdu_act(SMSCenter *smsc, smpp_pdu *pdu) {

	int ret = -1;

	switch(pdu->id) {

	case SMPP_BIND_RECEIVER_RESP:
		ret = pdu_act_bind_receiver_resp(smsc, pdu);
		break;

	case SMPP_BIND_TRANSMITTER_RESP:
		ret = pdu_act_bind_transmitter_resp(smsc, pdu);
		break;

	case SMPP_UNBIND_RESP:
		ret = pdu_act_unbind_resp(smsc, pdu);
		break;

	case SMPP_SUBMIT_SM_RESP:
		ret = pdu_act_submit_sm_resp(smsc, pdu);
		break;

	case SMPP_SUBMIT_MULTI_RESP:
		ret = pdu_act_submit_multi_resp(smsc, pdu);
		break;

	case SMPP_DELIVER_SM:
		ret = pdu_act_deliver_sm(smsc, pdu);
		break;

	case SMPP_QUERY_SM_RESP:
		ret = pdu_act_query_sm_resp(smsc, pdu);
		break;

	case SMPP_CANCEL_SM_RESP:
		ret = pdu_act_cancel_sm_resp(smsc, pdu);
		break;

	case SMPP_REPLACE_SM_RESP:
		ret = pdu_act_replace_sm_resp(smsc, pdu);
		break;

	case SMPP_ENQUIRE_LINK:
		ret = pdu_act_enquire_link(smsc, pdu);
		break;

	case SMPP_ENQUIRE_LINK_RESP:
		ret = pdu_act_enquire_link_resp(smsc, pdu);
		break;

	case SMPP_GENERIC_NAK:
		ret = pdu_act_generic_nak(smsc, pdu);
		break;

	}

	return ret;
}


/******************************************************************************
* pdu_decode
*
* Description:
*  Decode given raw data into a brand new SMPP PDU.
*
* Returns:
*  1 if a PDU was created
*  0 if no PDU could be created
* -1 if an error occurred
*
*/
static int pdu_decode(smpp_pdu **pdu, Octstr *from) {

	int ret;
	struct smpp_pdu *newpdu;

	newpdu = pdu_new();

	/* Decode the header */
	ret = pdu_header_decode(newpdu, from);

	switch(newpdu->id) {

	case SMPP_BIND_RECEIVER_RESP:
		ret = pdu_decode_bind(newpdu, from);
		break;

	case SMPP_BIND_TRANSMITTER_RESP:
		ret = pdu_decode_bind(newpdu, from);
		break;

	case SMPP_UNBIND_RESP:
		ret = pdu_decode_bind(newpdu, from);
		break;

	case SMPP_DELIVER_SM:
		ret = pdu_decode_deliver_sm(newpdu, from);
		break;

	case SMPP_SUBMIT_SM_RESP:
		ret = pdu_decode_submit_sm_resp(newpdu, from);
		break;
	}

	*pdu = newpdu;

	return 1;

}

/******************************************************************************
* pdu_encode
*
* Description:
*  Encode a given SMPP PDU structure to raw data.
*
* Returns:
*  1 if PDU was translated to raw data
*  0 if PDU couldn't be translated
* -1 if an error occurred
*
*/
static int pdu_encode(smpp_pdu *pdu, Octstr **rawdata) {

	struct Octstr *body = NULL, *header = NULL;
	int ret;

	switch(pdu->id) {

	case SMPP_BIND_RECEIVER:
		ret = pdu_encode_bind(pdu, &body);
		break;

	case SMPP_BIND_RECEIVER_RESP:
		ret = pdu_encode_bind(pdu, &body);
		break;

	case SMPP_BIND_TRANSMITTER:
		ret = pdu_encode_bind(pdu, &body);
		break;

	case SMPP_BIND_TRANSMITTER_RESP:
		ret = pdu_encode_bind(pdu, &body);
		break;

	case SMPP_UNBIND_RESP:
		ret = pdu_encode_bind(pdu, &body);
		break;

	case SMPP_DELIVER_SM_RESP:
		ret = pdu_encode_deliver_sm_resp(pdu, &body);
		break;

	case SMPP_SUBMIT_SM:
		ret = pdu_encode_submit_sm(pdu, &body);
		break;

	default:
		debug("smsc.bb.smpp", 0, "can't encode PDU, just doing the"
			       			" header");
	}

	pdu_header_encode(pdu, &header);

	if(body != NULL) {
		*rawdata = octstr_cat(header, body);
		octstr_destroy(header);
		octstr_destroy(body);
	} else {
		*rawdata = header;
	}

	return 1;
}


static int pdu_header_decode(smpp_pdu *pdu, Octstr *str) {
	unsigned char header[16];

	gw_assert(pdu != NULL);
	gw_assert(str != NULL);

	/* Read the header */
	octstr_get_many_chars(header, str, 0, 16);

	pdu->length = decode_network_long(header);
	pdu->id     = decode_network_long(header + 4);
	pdu->status = decode_network_long(header + 8);
	pdu->sequence_no = decode_network_long(header + 12);

	return 1;

}

static int pdu_header_encode(smpp_pdu *pdu, Octstr **rawdata) {
	unsigned char header[16];

	encode_network_long(header, pdu->length);
	encode_network_long(header + 4, pdu->id);
	encode_network_long(header + 8, pdu->status);
	encode_network_long(header + 12, pdu->sequence_no);

	*rawdata = octstr_create_from_data(header, 16);

	return 1;
}

static int pdu_decode_bind(smpp_pdu *pdu, Octstr *str) {

	return 1;
}

static int pdu_encode_bind(smpp_pdu *pdu, Octstr **str) {

	int length = 0;
	char *data = NULL, *where = NULL;
	int left;

	struct smpp_pdu_bind_receiver *bind_receiver;
	struct smpp_pdu_bind_receiver_resp *bind_receiver_resp;
	struct smpp_pdu_bind_transmitter *bind_transmitter;
	struct smpp_pdu_bind_transmitter_resp *bind_transmitter_resp;

	bind_receiver_resp = NULL;

	switch(pdu->id) {

	case SMPP_BIND_RECEIVER:
		bind_receiver = (smpp_pdu_bind_receiver*) pdu->message_body;
		length = strlen(bind_receiver->system_id) + 1 +
			 strlen(bind_receiver->password) + 1 +
			 strlen(bind_receiver->system_type) + 1 +
			 1 + 1 + 1 +
			 strlen(bind_receiver->address_range) + 1;
		data = gw_malloc(length);
		memset(data, 0, length);
		where = data;

		smpp_append_cstr(&where, &left, bind_receiver->system_id);
		smpp_append_cstr(&where, &left, bind_receiver->password);
		smpp_append_cstr(&where, &left, bind_receiver->system_type);
		smpp_append_oct(&where, &left, bind_receiver->interface_version);
		smpp_append_oct(&where, &left, bind_receiver->addr_ton);
		smpp_append_oct(&where, &left, bind_receiver->addr_npi);
		smpp_append_cstr(&where, &left, bind_receiver->address_range);

		break;

	case SMPP_BIND_RECEIVER_RESP:
		length = strlen(bind_receiver_resp->system_id) + 1;
		data = gw_malloc(length);
		memset(data, 0, length);
		where = data;

		smpp_append_cstr(&where, &left, bind_receiver_resp->system_id);

		break;

	case SMPP_BIND_TRANSMITTER:
		bind_transmitter = (smpp_pdu_bind_transmitter*) pdu->message_body;
		length = strlen(bind_transmitter->system_id) + 1 +
			 strlen(bind_transmitter->password) + 1 +
			 strlen(bind_transmitter->system_type) + 1 +
			 1 + 1 + 1 +
			 strlen(bind_transmitter->address_range) + 1;

		data = gw_malloc(length);
		memset(data, 0, length);
		where = data;

		smpp_append_cstr(&where, &left, bind_transmitter->system_id);
		smpp_append_cstr(&where, &left, bind_transmitter->password);
		smpp_append_cstr(&where, &left, bind_transmitter->system_type);
		smpp_append_oct(&where, &left, bind_transmitter->interface_version);
		smpp_append_oct(&where, &left, bind_transmitter->addr_ton);
		smpp_append_oct(&where, &left, bind_transmitter->addr_npi);
		smpp_append_cstr(&where, &left, bind_transmitter->address_range);

		break;

	case SMPP_BIND_TRANSMITTER_RESP:
		bind_transmitter_resp = (smpp_pdu_bind_transmitter_resp*) pdu->message_body;
		length = strlen(bind_transmitter_resp->system_id) + 1;
		data = gw_malloc(length);

		smpp_append_cstr(&where, &left, bind_transmitter_resp->system_id);

		break;

	case SMPP_UNBIND:
		data = NULL;
		length = 0;
		break;

	case SMPP_UNBIND_RESP:
		data = NULL;
		length = 0;
		break;

	}

	pdu->length = length + 16;

	*str = octstr_create_from_data(data, length);

	gw_free(data);

	return 1;
}

/******************************************************************************
* PDUs
*  BIND_TRANSMITTER, BIND_TRANSMITTER_RESP
*/
static int pdu_act_bind_transmitter_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	struct smpp_pdu *newpdu = NULL;

	/* Validate *msg. */
	gw_assert(smsc != NULL);
	gw_assert(pdu != NULL);

	smsc->smpp_t_state = SMPP_STATE_BOUND;

	/* Process any messages that were sent through the HTTP
	   interface while the transmitter connection was not
	   bound. */
	while ((newpdu = list_extract_first(smsc->unsent_mt))) {
		list_produce(smsc->fifo_t_out, newpdu);
	}

	return 0;

}

/******************************************************************************
* PDUs
*  BIND_RECEIVER, BIND_RECEIVER_RESP
*/
static int pdu_act_bind_receiver_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	smsc->smpp_r_state = SMPP_STATE_BOUND;

	return 1;
}

/******************************************************************************
* PDUs
*  UNBIND, UNBIND_RESP
*/
static int pdu_act_unbind_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	return -1;
}

/******************************************************************************
* PDUs
*  SUBMIT_SM, SUBMIT_SM_RESP
*/
static int pdu_act_submit_sm_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	debug("bb.sms.smpp", 0, "pdu_act_submit_sm_resp: start");

	/* Mark message the SUBMIT_SM_RESP refers to as
	   acknowledged and remove it from smsc->smpp_fifostack. */

	debug("bb.sms.smpp", 0, "pdu->length == %08x", pdu->length);
	debug("bb.sms.smpp", 0, "pdu->id == %08x", pdu->id);
	debug("bb.sms.smpp", 0, "pdu->status == %08x", pdu->status);
	debug("bb.sms.smpp", 0, "pdu->sequence_no == %08x", pdu->sequence_no);

	return -1;
}

static int pdu_encode_submit_sm(smpp_pdu* pdu, Octstr** str) {
	struct smpp_pdu_submit_sm *submit_sm = NULL;
	long length;
	int left;
	char *data = NULL, *where = NULL;

	submit_sm = (struct smpp_pdu_submit_sm*) pdu->message_body;

	length = strlen(submit_sm->service_type) + 1 +
		 1 + 1 +
		 strlen(submit_sm->source_addr) + 1 +
		 1 + 1 +
		 strlen(submit_sm->dest_addr) + 1 +
		 1 + 1 + 1 +
		 strlen(submit_sm->schedule_delivery_time) + 1 +
		 strlen(submit_sm->validity_period) + 1 +
		 1 + 1 + 1 + 1 + 1 +
		 submit_sm->sm_length + 1;

	data = gw_malloc(length);
	memset(data, 0, length);
	where = data;

	smpp_append_cstr(&where, &left, submit_sm->service_type);
	smpp_append_oct(&where, &left, submit_sm->source_addr_ton);
	smpp_append_oct(&where, &left, submit_sm->source_addr_npi);
	smpp_append_cstr(&where, &left, submit_sm->source_addr);
	smpp_append_oct(&where, &left, submit_sm->dest_addr_ton);
	smpp_append_oct(&where, &left, submit_sm->dest_addr_npi);
	smpp_append_cstr(&where, &left, submit_sm->dest_addr);
	smpp_append_oct(&where, &left, submit_sm->esm_class);
	smpp_append_oct(&where, &left, submit_sm->protocol_id);
	smpp_append_oct(&where, &left, submit_sm->priority_flag);
	smpp_append_cstr(&where, &left, submit_sm->schedule_delivery_time);
	smpp_append_cstr(&where, &left, submit_sm->validity_period);
	smpp_append_oct(&where, &left, submit_sm->registered_delivery_flag);
	smpp_append_oct(&where, &left, submit_sm->replace_if_present_flag);
	smpp_append_oct(&where, &left, submit_sm->data_coding);
	smpp_append_oct(&where, &left, submit_sm->sm_default_msg_id);
	smpp_append_oct(&where, &left, submit_sm->sm_length);
	memcpy(where, submit_sm->short_message, submit_sm->sm_length);

	*str = octstr_create_from_data(data, length);
	gw_free(data);

	return 1;
}

static int pdu_decode_submit_sm_resp(smpp_pdu* pdu, Octstr* str) {
	return -1;
}



/******************************************************************************
* PDUs
*  SUBMIT_MULTI, SUBMIT_SM_MULTI_RESP
*/
static int pdu_act_submit_multi_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	debug("bb.sms.smpp", 0, "pdu_act_submit_multi_resp: start");

	return -1;
}

/******************************************************************************
* PDUs
*  DELIVER_SM, DELIVER_SM_RESP
*/
static int pdu_act_deliver_sm(SMSCenter *smsc, smpp_pdu *pdu) {

	struct smpp_pdu *newpdu = NULL;
	struct smpp_pdu_deliver_sm *deliver_sm = NULL;
	struct smpp_pdu_deliver_sm_resp *deliver_sm_resp = NULL;

	/* If DELIVER_SM was sent to acknowledge that the terminal
	   has received the message, ignore. */
	deliver_sm = (struct smpp_pdu_deliver_sm*) pdu->message_body;
	if(deliver_sm==NULL) goto error;

	/* Push a copy of the PDU on the smsc->received_mo fifostack. */
	newpdu = pdu_new();
	memcpy(newpdu, pdu, sizeof(struct smpp_pdu));
	newpdu->message_body = pdu->message_body;
	pdu->message_body = NULL;
	list_produce(smsc->received_mo, newpdu);

	/* Push a DELIVER_SM_RESP structure on the smsc->fifo_r_out List. */
	newpdu = pdu_new();
	memset(newpdu, 0, sizeof(struct smpp_pdu));

	deliver_sm_resp = gw_malloc(sizeof(struct smpp_pdu_deliver_sm_resp));
	memset(deliver_sm_resp, 0, sizeof(struct smpp_pdu_deliver_sm_resp));

	newpdu->length = 17;
	newpdu->id = SMPP_DELIVER_SM_RESP;
	newpdu->status = 0;
	newpdu->sequence_no = pdu->sequence_no;
	newpdu->message_body = deliver_sm_resp;

	list_produce(smsc->fifo_r_out, newpdu);

	return 1;

error:
	return -1;
}

static int pdu_encode_deliver_sm_resp(smpp_pdu *pdu, Octstr **str) {

	Octstr *newstr = NULL;
	struct smpp_pdu_deliver_sm_resp *deliver_sm_resp = NULL;
	char data;

	deliver_sm_resp = (struct smpp_pdu_deliver_sm_resp*) pdu->message_body;

	/* As specified in SMPP 3.3 / 6.3.3.2 */
	data = '\0';

	newstr = octstr_create_from_data(&data, 1);

	*str = newstr;

	return 1;
}

static int pdu_decode_deliver_sm(smpp_pdu* pdu, Octstr* str) {

	char *start, *end, *buff;
	struct smpp_pdu_deliver_sm *deliver_sm;
	Octet oct;

	gw_assert(pdu != NULL);
	gw_assert(str != NULL);

	if(octstr_len(str) < 16) {
		warning(0, "pdu_decode_deliver_sm: incorrect input");
		goto error;
	}

	deliver_sm = gw_malloc(sizeof(struct smpp_pdu_deliver_sm));
	memset(deliver_sm, 0, sizeof(struct smpp_pdu_deliver_sm));
	pdu->message_body = deliver_sm;

	buff = gw_malloc(octstr_len(str));

	octstr_get_many_chars(buff, str, 0, octstr_len(str));
	start = buff + 16;

	end = start + strlen(start);
	strncpy(deliver_sm->service_type, start, 6);
	start = end+1;

	strncpy(&oct, start, 1);
	deliver_sm->source_addr_ton = oct;
	start++;

	strncpy(&oct, start, 1);
	deliver_sm->source_addr_npi = oct;
	start++;

	end = start + strlen(start);
	strncpy(deliver_sm->source_addr, start, 21);
	start = end+1;

	strncpy(&oct, start, 1);
	deliver_sm->dest_addr_ton = oct;
	start++;

	strncpy(&oct, start, 1);
	deliver_sm->dest_addr_npi = oct;
	start++;

	end = start + strlen(start);
	strncpy(deliver_sm->dest_addr, start, 21);
	start = end+1;

	strncpy(&oct, start, 1);
	deliver_sm->esm_class = oct;
	start++;

	strncpy(&oct, start, 1);
	deliver_sm->protocol_id = oct;
	start++;

	strncpy(&oct, start, 1);
	deliver_sm->priority_flag = oct;
	start++;

	end = start + strlen(start);
	strncpy(deliver_sm->schedule_delivery_time, start, 17);
	start = end+1;

	end = start + strlen(start);
	strncpy(deliver_sm->validity_period, start, 17);
	start = end+1;

	strncpy(&oct, start, 1);
	deliver_sm->registered_delivery_flag = oct;
	start++;

	strncpy(&oct, start, 1);
	deliver_sm->replace_if_present_flag = oct;
	start++;

	strncpy(&oct, start, 1);
	deliver_sm->data_coding = oct;
	start++;

	strncpy(&oct, start, 1);
	deliver_sm->sm_default_msg_id = oct;
	start++;

	strncpy(&oct, start, 1);
	deliver_sm->sm_length = oct;
	start++;

	/* Make sure that the upcoming SMPP 3.4 implementation
	   won't break our application with the new definition
	   of max short_message size. */
	end = start + strlen(start);
	memcpy(deliver_sm->short_message, start,
		(deliver_sm->sm_length > 
		 	(int) sizeof(deliver_sm->short_message)) ?
		sizeof(deliver_sm->short_message) : deliver_sm->sm_length);
	start = end+1;

	gw_free(buff);

#ifdef SMPP_DEBUG
	debug("bb.sms.smpp", 0, "pdu->service_type == %s", 
			deliver_sm->service_type);
	debug("bb.sms.smpp", 0, "pdu->source_addr_ton == %i", 
			deliver_sm->source_addr_ton);
	debug("bb.sms.smpp", 0, "pdu->source_addr_npi == %i", 
			deliver_sm->source_addr_npi);
	debug("bb.sms.smpp", 0, "pdu->source_addr == %s", 
			deliver_sm->source_addr);
	debug("bb.sms.smpp", 0, "pdu->dest_addr_ton == %i", 
			deliver_sm->dest_addr_ton);
	debug("bb.sms.smpp", 0, "pdu->dest_addr_npi == %i", 
			deliver_sm->dest_addr_npi);
	debug("bb.sms.smpp", 0, "pdu->dest_addr == %s", 
			deliver_sm->dest_addr);
	debug("bb.sms.smpp", 0, "pdu->esm_class == %i", 
			deliver_sm->esm_class);
	debug("bb.sms.smpp", 0, "pdu->protocol_id == %i", 
			deliver_sm->protocol_id);
	debug("bb.sms.smpp", 0, "pdu->priority_flag == %i", 
			deliver_sm->priority_flag);

	debug("bb.sms.smpp", 0, "pdu->schedule_delivery_time == %s", 
			deliver_sm->schedule_delivery_time);
	debug("bb.sms.smpp", 0, "pdu->validity_period == %s", 
			deliver_sm->validity_period);

	debug("bb.sms.smpp", 0, "pdu->registered_delivery_flag == %i", 
			deliver_sm->registered_delivery_flag);
	debug("bb.sms.smpp", 0, "pdu->replace_if_present_flag == %i", 
			deliver_sm->replace_if_present_flag);
	debug("bb.sms.smpp", 0, "pdu->data_coding == %i", 
			deliver_sm->data_coding);
	debug("bb.sms.smpp", 0, "pdu->sm_default_msg_id == %i", 
			deliver_sm->sm_default_msg_id);
	debug("bb.sms.smpp", 0, "pdu->sm_length == %i", deliver_sm->sm_length);

	start = gw_malloc( 4 );
	end = gw_malloc( (deliver_sm->sm_length*3) + 1 );
	memset(end, 0, (deliver_sm->sm_length*3) + 1);
	for(oct=0; oct < deliver_sm->sm_length; oct++) {
		sprintf(start, "%02x ", (unsigned char) 
				deliver_sm->short_message[oct]);
		strcat(end, start);
	}
	debug("bb.sms.smpp", 0, "pdu->short_message == %s", end);

	gw_free(start);
	gw_free(end);
#endif

	return 1;

error:
	return -1;
}

/******************************************************************************
* PDUs
*  QUERY_SM, QUERY_SM_RESP
*/
static int pdu_act_query_sm_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	/* Ignore, this version doesn't send messages which
	   get these responses. */

	return -1;
}

static int pdu_act_cancel_sm_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	/* Ignore, this version doesn't send messages which
	   get these responses. */

	return -1;
}

static int pdu_act_replace_sm_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	/* Ignore, this version doesn't send messages which
	   get these responses. */

	return -1;
}

static int pdu_act_enquire_link(SMSCenter *smsc, smpp_pdu *pdu) {

	struct smpp_pdu *newpdu = NULL;

	/* Push a ENQUIRE_LINK_RESP on the appropriate fifostack. */
	newpdu = pdu_new();
	if(newpdu==NULL) goto error;
	memset(newpdu, 0, sizeof(struct smpp_pdu));

	newpdu->message_body = NULL;
	newpdu->length = 16;
	newpdu->id = SMPP_ENQUIRE_LINK_RESP;
	newpdu->status = 0;

	if(pdu->fd == smsc->fd_t) {
		newpdu->sequence_no = smsc->seq_t++;
		list_produce(smsc->fifo_t_out, newpdu);
	} else if(pdu->fd == smsc->fd_r) {
		newpdu->sequence_no = smsc->seq_r++;
		list_produce(smsc->fifo_r_out, newpdu);
	}

	return 1;

error:
	return -1;
}

static int pdu_act_enquire_link_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	/* Reset the status flag CAN_SEND or CAN_RECEIVE  */

	return 1;
}

static int pdu_act_generic_nak(SMSCenter *smsc, smpp_pdu *pdu) {

	error(0, "pdu_act_generic_nak: SOMETHING IS DREADFULLY WRONG");

	debug("bb.sms.smpp", 0, "pdu->length == %08x", pdu->length);
	debug("bb.sms.smpp", 0, "pdu->id == %08x", pdu->id);
	debug("bb.sms.smpp", 0, "pdu->status == %08x", pdu->status);
	debug("bb.sms.smpp", 0, "pdu->sequence_no == %08x", pdu->sequence_no);

	/* Panic  */

	return -1;
}
