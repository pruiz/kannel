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

#include "smsc.h"
#include "smsc_smpp.h"
#include "octstr.h"
#include "wapitlib.h"

SMSCenter *smpp_open(char *host, int port, char *system_id, char *password, char* system_type, char *address_range) {

	SMSCenter *smsc = NULL;
	struct smpp_pdu *pdu = NULL;
	struct smpp_pdu_bind_receiver *bind_receiver = NULL;
	struct smpp_pdu_bind_transmitter *bind_transmitter = NULL;

	smsc = smscenter_construct();
	if(smsc==NULL) goto error;

	smsc->type = SMSC_TYPE_SMPP_IP;
	sprintf(smsc->name, "SMPP:%s:%i:%s:%s", host, port, system_id, system_type);
	smsc->latency = 100*1000;

	smsc->hostname = strdup(host);
	smsc->port = port;

	smsc->smpp_system_id = (system_id != NULL) ? strdup(system_id) : NULL;
	smsc->smpp_system_type = (system_type != NULL) ? strdup(system_type) : NULL;
	smsc->smpp_password = (password != NULL) ? strdup(password) : NULL;
	smsc->smpp_address_range = (address_range != NULL) ? strdup(address_range) : NULL;

	/* Create FIFO stacks */
	smsc->unsent_mt = fifo_new();
	if(smsc->unsent_mt == NULL) goto error;

	smsc->sent_mt = fifo_new();
	if(smsc->sent_mt == NULL) goto error;

	smsc->delivered_mt = fifo_new();
	if(smsc->delivered_mt == NULL) goto error;

	smsc->received_mo = fifo_new();
	if(smsc->received_mo == NULL) goto error;

	smsc->fifo_t_in = fifo_new();
	if(smsc->fifo_t_in == NULL) goto error;

	smsc->fifo_t_out = fifo_new();
	if(smsc->fifo_t_out == NULL) goto error;

	smsc->fifo_r_in = fifo_new();
	if(smsc->fifo_r_in == NULL) goto error;

	smsc->fifo_r_out = fifo_new();
	if(smsc->fifo_r_out == NULL) goto error;

	/* Create buffers */
	smsc->data_t = data_new();
	if(smsc->data_t == NULL) goto error;
	
	smsc->data_r = data_new();
	if(smsc->data_r == NULL) goto error;

	/* Open the transmitter connection */
	smsc->fd_t = tcpip_connect_to_server(smsc->hostname, smsc->port);
	if(smsc->fd_t == -1) goto error;
	smsc->smpp_t_state = SMPP_STATE_CONNECTED;

	/* Open the receiver connection */
	smsc->fd_r = tcpip_connect_to_server(smsc->hostname, smsc->port);
	if(smsc->fd_r == -1) goto error;
	smsc->smpp_r_state = SMPP_STATE_CONNECTED;

	/* Push a BIND_RECEIVER PDU on the [smsc->unsent] stack. */
	pdu = pdu_new();
	if(pdu == NULL) goto error;
	pdu->id = SMPP_BIND_RECEIVER;
	smsc->seq_r = 1;
	pdu->sequence_no = 1;
	bind_receiver = malloc(sizeof(struct smpp_pdu_bind_receiver));
	if(bind_receiver==NULL) goto error;
	memset(bind_receiver, 0, sizeof(struct smpp_pdu_bind_receiver));
	strncpy(bind_receiver->system_id, system_id, 16);
	strncpy(bind_receiver->password, password, 9);
	strncpy(bind_receiver->system_type, system_type, 13);
	strncpy(bind_receiver->address_range, address_range, 41);
	pdu->message_body = bind_receiver;
	fifo_push(smsc->fifo_r_out, pdu);
	
	/* Push a BIND_TRANSMITTER PDU on the [smsc->unsent] stack. */
	pdu = pdu_new();
	if(pdu == NULL) goto error;
	pdu->id = SMPP_BIND_TRANSMITTER;
	smsc->seq_t = 1;
	pdu->sequence_no = 1;
	bind_transmitter = malloc(sizeof(struct smpp_pdu_bind_transmitter));
	if(bind_transmitter==NULL) goto error;
	memset(bind_transmitter, 0, sizeof(struct smpp_pdu_bind_transmitter));
	strncpy(bind_transmitter->system_id, system_id, 16);
	strncpy(bind_transmitter->password, password, 9);
	strncpy(bind_transmitter->system_type, system_type, 13);
	strncpy(bind_transmitter->address_range, address_range, 41);
	pdu->message_body = bind_transmitter;
	fifo_push(smsc->fifo_t_out, pdu);

	/* Done, return */
	return smsc;

error:
	error(0, "smpp_open: could not open");
	pdu_free(pdu);

	/* Destroy FIFO stacks. */
	fifo_free(smsc->unsent_mt);
	fifo_free(smsc->sent_mt);
	fifo_free(smsc->delivered_mt);
	fifo_free(smsc->received_mo);
	fifo_free(smsc->fifo_t_in);
	fifo_free(smsc->fifo_t_out);
	fifo_free(smsc->fifo_r_in);
	fifo_free(smsc->fifo_r_out);

	/* Destroy buffers */
	data_free(smsc->data_t);
	data_free(smsc->data_r);

	smscenter_destruct(smsc);
	return NULL;
}

int smpp_reopen(SMSCenter *smsc) {

	struct smpp_pdu *pdu = NULL;
	struct smpp_pdu_bind_receiver *bind_receiver = NULL;
	struct smpp_pdu_bind_transmitter *bind_transmitter = NULL;

	if(smsc==NULL) goto error;

	/* Destroy FIFO stacks. */
	fifo_free(smsc->unsent_mt);
	fifo_free(smsc->sent_mt);
	fifo_free(smsc->delivered_mt);
	fifo_free(smsc->received_mo);
	fifo_free(smsc->fifo_t_in);
	fifo_free(smsc->fifo_t_out);
	fifo_free(smsc->fifo_r_in);
	fifo_free(smsc->fifo_r_out);

	/* Destroy buffers */
	data_free(smsc->data_t);
	data_free(smsc->data_r);

	/* Close sockets */
	close(smsc->fd_t);
	close(smsc->fd_r);

	/* Create FIFO stacks */
	smsc->unsent_mt = fifo_new();
	if(smsc->unsent_mt == NULL) goto error;

	smsc->sent_mt = fifo_new();
	if(smsc->sent_mt == NULL) goto error;

	smsc->delivered_mt = fifo_new();
	if(smsc->delivered_mt == NULL) goto error;

	smsc->received_mo = fifo_new();
	if(smsc->received_mo == NULL) goto error;

	smsc->fifo_t_in = fifo_new();
	if(smsc->fifo_t_in == NULL) goto error;

	smsc->fifo_t_out = fifo_new();
	if(smsc->fifo_t_out == NULL) goto error;

	smsc->fifo_r_in = fifo_new();
	if(smsc->fifo_r_in == NULL) goto error;

	smsc->fifo_r_out = fifo_new();
	if(smsc->fifo_r_out == NULL) goto error;

	/* Create buffers */
	smsc->data_t = data_new();
	if(smsc->data_t == NULL) goto error;
	
	smsc->data_r = data_new();
	if(smsc->data_r == NULL) goto error;

	/* Open the transmitter connection */
	smsc->fd_t = tcpip_connect_to_server(smsc->hostname, smsc->port);
	if(smsc->fd_t == -1) goto error;
	smsc->smpp_t_state = SMPP_STATE_CONNECTED;

	/* Open the receiver connection */
	smsc->fd_r = tcpip_connect_to_server(smsc->hostname, smsc->port);
	if(smsc->fd_r == -1) goto error;
	smsc->smpp_r_state = SMPP_STATE_CONNECTED;

	/* Push a BIND_RECEIVER PDU on the [smsc->unsent] stack. */
	pdu = pdu_new();
	if(pdu == NULL) goto error;
	pdu->id = SMPP_BIND_RECEIVER;
	smsc->seq_r = 1;
	pdu->sequence_no = 1;
	bind_receiver = malloc(sizeof(struct smpp_pdu_bind_receiver));
	if(bind_receiver==NULL) goto error;
	memset(bind_receiver, 0, sizeof(struct smpp_pdu_bind_receiver));
	strncpy(bind_receiver->system_id, smsc->smpp_system_id, 16);
	strncpy(bind_receiver->password, smsc->smpp_password, 9);
	strncpy(bind_receiver->system_type, smsc->smpp_system_type, 13);
	strncpy(bind_receiver->address_range, smsc->smpp_address_range, 41);
	pdu->message_body = bind_receiver;
	fifo_push(smsc->fifo_r_out, pdu);
	
	/* Push a BIND_TRANSMITTER PDU on the [smsc->unsent] stack. */
	pdu = pdu_new();
	if(pdu == NULL) goto error;
	pdu->id = SMPP_BIND_TRANSMITTER;
	smsc->seq_t = 1;
	pdu->sequence_no = 1;
	bind_transmitter = malloc(sizeof(struct smpp_pdu_bind_transmitter));
	if(bind_transmitter==NULL) goto error;
	memset(bind_transmitter, 0, sizeof(struct smpp_pdu_bind_transmitter));
	strncpy(bind_transmitter->system_id, smsc->smpp_system_id, 16);
	strncpy(bind_transmitter->password, smsc->smpp_password, 9);
	strncpy(bind_transmitter->system_type, smsc->smpp_system_type, 13);
	strncpy(bind_transmitter->address_range, smsc->smpp_address_range, 41);
	pdu->message_body = bind_transmitter;
	fifo_push(smsc->fifo_t_out, pdu);

	/* Done, return */
	return 1;

error:
	error(0, "smpp_reopen: could not open");

	pdu_free(pdu);

	/* Destroy FIFO stacks. */
	fifo_free(smsc->unsent_mt);
	fifo_free(smsc->sent_mt);
	fifo_free(smsc->delivered_mt);
	fifo_free(smsc->received_mo);
	fifo_free(smsc->fifo_t_in);
	fifo_free(smsc->fifo_t_out);
	fifo_free(smsc->fifo_r_in);
	fifo_free(smsc->fifo_r_out);

	/* Destroy buffers */
	data_free(smsc->data_t);
	data_free(smsc->data_r);

	smscenter_destruct(smsc);

	return -1;
}


int smpp_close(SMSCenter *smsc) {

	struct smpp_pdu *pdu = NULL;

	debug(0, "smpp_close: closing");

	/* Push a UNBIND PDU on the [smsc->fifo_r_out] stack. */
	pdu = pdu_new();
	if(pdu == NULL) goto error;
	pdu->id = SMPP_UNBIND;
	pdu->length = 16;
	pdu->status = 0;
	pdu->sequence_no = 1;
	pdu->message_body = NULL;
	fifo_push(smsc->fifo_r_out, pdu);
	
	/* Push a UNBIND PDU on the [smsc->fifo_t_out] stack. */
	pdu = pdu_new();
	if(pdu == NULL) goto error;
	pdu->id = SMPP_UNBIND;
	pdu->length = 16;
	pdu->status = 0;
	pdu->sequence_no = 1;
	pdu->message_body = NULL;
	fifo_push(smsc->fifo_t_out, pdu);

	/* Write out the UNBIND PDUs. */
	smpp_pending_smsmessage(smsc);

	/* Check states */

#if 0
/*  XXX LATER, WHEN THIS IS IMPLEMENTED IN SMSGATEWAY.C XXX */
	/* If states are BOUND then push UNBIND messages to
	   fifostack and return a failure. */

	if(smsc->smpp_t_state == 1)
		return 0;

	if(smsc->smpp_r_state == 1)
		return 0;
#endif

	/* Close transmitter connection. */
	close(smsc->fd_t);

	/* Close receiver connection. */
	close(smsc->fd_r);

	/* Destroy FIFO stacks. */
	fifo_free(smsc->unsent_mt);
	fifo_free(smsc->sent_mt);
	fifo_free(smsc->delivered_mt);
	fifo_free(smsc->received_mo);
	fifo_free(smsc->fifo_t_in);
	fifo_free(smsc->fifo_t_out);
	fifo_free(smsc->fifo_r_in);
	fifo_free(smsc->fifo_r_out);

	/* Destroy buffers */
	data_free(smsc->data_t);
	data_free(smsc->data_r);

	return 0;

error:
	return -1;
}

int smpp_submit_smsmessage(SMSCenter *smsc, SMSMessage *msg) {

	struct smpp_pdu *pdu = NULL;
	struct smpp_pdu_submit_sm *submit_sm = NULL;

	panic(0, "smpp_submit_smsmessage: USE OF THIS FUNCTION IS DEPRACATED");
	return -1;

	/* Validate *msg. */
	if(smsc == NULL) goto error;
	if(msg == NULL) goto error;

	/* If we cannot really send yet, push message to
	   smsc->unsent_mt where it will stay until
	   smpp_pdu_act_bind_transmitter_resp is called. */

	if( smsc->smpp_t_state != SMPP_STATE_BOUND ) {
		fifo_push_smsmessage(smsc->unsent_mt, msg);
		return 1;
	}

	/* Push a SUBMIT_SM PDU on the smsc->fifo_t_out fifostack. */
	pdu = pdu_new();
	if(pdu == NULL) goto error;
	memset(pdu, 0, sizeof(struct smpp_pdu));

	submit_sm = malloc(sizeof(struct smpp_pdu_submit_sm));
	if(submit_sm == NULL) goto error;
	memset(submit_sm, 0, sizeof(struct smpp_pdu_submit_sm));

	strncpy(submit_sm->source_addr, msg->sender, 21);
	submit_sm->source_addr_npi = GSM_ADDR_NPI_UNKNOWN;
	submit_sm->source_addr_ton = GSM_ADDR_TON_NETWORKSPECIFIC;

	/* Notice that the +2 is to get rid of the 00 start. */
	strncat(submit_sm->dest_addr, msg->receiver+2, 21);
	submit_sm->dest_addr_npi = GSM_ADDR_NPI_E164;
	submit_sm->dest_addr_ton = GSM_ADDR_TON_INTERNATIONAL;

	submit_sm->data_coding = 3;

	submit_sm->sm_length = octstr_len(msg->text);
	octstr_get_many_chars(submit_sm->short_message, msg->text, 0, 160);
	charset_iso_to_smpp(submit_sm->short_message);

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

	fifo_push(smsc->fifo_t_out, pdu);

	return 0;

error:
	return -1;
}


int smpp_receive_smsmessage(SMSCenter *smsc, SMSMessage **msg) {

	SMSMessage *newmsg = NULL;
	char *newnum = NULL;

	panic(0, "smpp_receive_smsmessage: USE OF THIS FUNCTION IS DEPRACATED");
	return -1;

	/* Pop a SMSMessage message from the MSG_MO stack. */
	if( fifo_pop_smsmessage(smsc->received_mo, &newmsg) == 1 ) {

		/* Change the number format on msg->sender. */
		newnum = malloc(strlen(newmsg->sender)+3);
		if(newnum==NULL) goto error;
		memset(newnum, 0, strlen(newmsg->sender)+3);
		strcpy(newnum, "00");
		strcat(newnum, newmsg->sender);
		free(newmsg->sender);
		newmsg->sender = newnum;

		*msg = newmsg;

		return 1;
	}
	
	return 0;
error:
	error(errno, "smpp_receive_smsmessage: error");
	return -1;
}

int smpp_submit_msg(SMSCenter *smsc, Msg *msg) {


	struct smpp_pdu *pdu = NULL;
	struct smpp_pdu_submit_sm *submit_sm = NULL;

	/* Validate *msg. */
	if(smsc == NULL) goto error;
	if(msg == NULL) goto error;

	msg_dump(msg);

	/* If we cannot really send yet, push message to
	   smsc->unsent_mt where it will stay until
	   smpp_pdu_act_bind_transmitter_resp is called. */

	/* Push a SUBMIT_SM PDU on the smsc->fifo_t_out fifostack. */
	pdu = pdu_new();
	if(pdu == NULL) goto error;
	memset(pdu, 0, sizeof(struct smpp_pdu));

	submit_sm = malloc(sizeof(struct smpp_pdu_submit_sm));
	if(submit_sm == NULL) goto error;
	memset(submit_sm, 0, sizeof(struct smpp_pdu_submit_sm));

	if(msg_type(msg) == smart_sms) {

		if(msg->smart_sms.flag_8bit == 1) {
			/* As per GSM 03.38. */
			submit_sm->esm_class = 67;
		} else {
			submit_sm->esm_class = 0;
		}

		if(msg->smart_sms.flag_udh == 1) {
			/* As per GSM 03.38. */
			submit_sm->data_coding = 245;
		} else {
			submit_sm->data_coding = 3;
		}

		strncpy(submit_sm->source_addr, octstr_get_cstr(msg->smart_sms.sender), 21);
		strncat(submit_sm->dest_addr, octstr_get_cstr(msg->smart_sms.receiver)+2, 21);

		submit_sm->sm_length = octstr_len(msg->smart_sms.udhdata) + 
			octstr_len(msg->smart_sms.msgdata);

		octstr_get_many_chars(submit_sm->short_message, msg->smart_sms.udhdata, 0, 160);

		octstr_get_many_chars(
			submit_sm->short_message + octstr_len(msg->smart_sms.udhdata),
			msg->smart_sms.msgdata, 0, 160 - octstr_len(msg->smart_sms.udhdata));

		charset_iso_to_smpp(submit_sm->short_message);

	} else {
		error(0, "smpp_submit_sms: Msg is WRONG TYPE");
		msg_dump(msg);
		goto error;
	}

	submit_sm->source_addr_npi = GSM_ADDR_NPI_UNKNOWN;
	submit_sm->source_addr_ton = GSM_ADDR_TON_NETWORKSPECIFIC;

	/* Notice that the +2 is to get rid of the 00 start. */
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
		fifo_push(smsc->fifo_t_out, pdu);
	} else {
		/* The message has to be queued and sent
	   	   upon receiving a BIND_TRANSMITTER_RESP. */
		fifo_push(smsc->unsent_mt, pdu);
	}

	return 1;

error:
	error(errno, "smpp_submit_msg: error");
	return -1;
}

int smpp_receive_msg(SMSCenter *smsc, Msg **msg) {

	Msg *newmsg = NULL;
	struct smpp_pdu *pdu = NULL;
	struct smpp_pdu_deliver_sm *deliver_sm = NULL;
	char *newnum = NULL;

	/* Pop a SMSMessage message from the MSG_MO stack. */
	if( fifo_pop(smsc->received_mo, &pdu) == 1 ) {

		deliver_sm = (struct smpp_pdu_deliver_sm*) pdu->message_body;
		if(deliver_sm==NULL) goto error;

		/* Change the number format on msg->sender. */
		newnum = malloc(strlen(deliver_sm->source_addr)+1);
		if(newnum==NULL) goto error;
		strcpy(newnum, deliver_sm->source_addr);
		strcpy(deliver_sm->source_addr, "00");
		strncat(deliver_sm->source_addr, newnum, sizeof(deliver_sm->source_addr)-2);
		free(newnum);

		newmsg = msg_create(smart_sms);
		if(newmsg==NULL) goto error;

		if( (deliver_sm->esm_class == 67) && (deliver_sm->data_coding == 245) ) {
			newmsg->smart_sms.flag_8bit = 1;
			newmsg->smart_sms.flag_udh = 1;
		} else if( (deliver_sm->esm_class == 3) && (deliver_sm->data_coding == 0) ) {
			newmsg->smart_sms.flag_8bit = 0;
			newmsg->smart_sms.flag_udh = 0;
		} else {
			debug(0, "problemss....");
			newmsg = NULL;
		}

		newmsg->smart_sms.receiver = octstr_create(deliver_sm->dest_addr);
		newmsg->smart_sms.sender = octstr_create(deliver_sm->source_addr);
		newmsg->smart_sms.msgdata = octstr_create(deliver_sm->short_message);
		newmsg->smart_sms.udhdata = octstr_create("");

		msg_dump(newmsg);

		*msg = newmsg;

		return 1;
	}

	return 0;
error:
	error(errno, "smpp_receive_msg: error");
	msg_destroy(newmsg);
	return -1;
}

int smpp_pending_smsmessage(SMSCenter *smsc) {

	Octstr *data = NULL;
	smpp_pdu *pdu = NULL;
	int ret = 0, funcret = 0;

	/* Process the MT messages. */

	/* Send whatever we need to send */
	while( fifo_pop(smsc->fifo_t_out, &pdu) == 1 ) {
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

	while( fifo_pop(smsc->fifo_r_out, &pdu) == 1 ) {
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
	if(smsc->received_mo->left != NULL) funcret = 1;

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

	newpdu = malloc(sizeof(struct smpp_pdu));
	memset(newpdu, 0, sizeof(struct smpp_pdu));

	return newpdu;
}

static int pdu_free(smpp_pdu *pdu) {

	if(pdu == NULL) return 0;

	smsmessage_destruct(pdu->smsmsg);
	free(pdu->message_body);
	free(pdu);

	return 1;
}

static Octstr* data_new(void) {

	struct Octstr *newstr = NULL;

	newstr = octstr_create_empty();

	return newstr;
}

static int data_free(Octstr *str) {

	if(str == NULL) return 0;

	octstr_destroy(str);

	return 1;
}

static fifostack* fifo_new(void) {

	struct fifostack *fifo = NULL;

	fifo = malloc(sizeof(struct fifostack));
	if(fifo == NULL) goto error;
	memset(fifo, 0, sizeof(struct fifostack));

	fifo->left = NULL;
	fifo->right = NULL;

	return fifo;

error:
	error(0, "fifo_new: memory allocation error");
	return NULL;
}

static void fifo_free(fifostack *fifo) {

	struct smpp_pdu *pdu = NULL;

	if(fifo == NULL) return;

	/* Drain the leftover PDUs to the bit sink. */
	while( fifo_pop(fifo, &pdu) == 1 )  {
		if(pdu==NULL) break;
		pdu_free(pdu);
		pdu = NULL;
	}

	free(fifo);

	return;
}

static int fifo_push(fifostack *fifo, smpp_pdu *pdu) {

	if(fifo == NULL) {
		error(0, "fifo_push: NULL input");
		goto error;
	}

	if(pdu == NULL) {
		error(0, "fifo_push: NULL input");
		goto error;
	}

	/* If fifostack is completely empty. */
	if( fifo->left == NULL ) {
		fifo->left = pdu;
		fifo->right = pdu;
		pdu->left = NULL;
		pdu->right = NULL;
		goto a_ok;
	}

	/* Ok, insert the pdu on the left side. */
	pdu->left = NULL;
	(fifo->left)->left = pdu;
	fifo->left = pdu;

a_ok:
	return 1;
error:
	error(0, "fifo_push: error");
	return -1;

}

static int fifo_pop(fifostack *fifo, smpp_pdu **pdu) {

	if(fifo == NULL) {
		error(0, "fifo_pop: NULL input");
		goto error;
	}

	if(pdu == NULL) {
		error(0, "fifo_pop: NULL input");
		goto error;
	}

	/* If fifostack is completely empty. */
	if( (fifo->left == NULL) && (fifo->right == NULL) ) {
		goto no_msgs;
	}

	/* Drop a message from the right side. */
	*pdu = fifo->right;

	/* If this was the last PDU and the fifostack is now empty. */
	if((fifo->right)->left == NULL) {
		fifo->right = NULL;
		fifo->left = NULL;
	} else {
		/* Set the new right edge. */
		fifo->right = (fifo->right)->left;
		/* Terminate. */
		(fifo->right)->right = NULL;
	}

	return 1;

no_msgs:
	return 0;

error:
	error(0, "fifo_pop: returing error");
	return -1;
}

static int fifo_push_smsmessage(fifostack *fifo, SMSMessage *msg) {

	struct smpp_pdu *pdu = NULL;

	pdu = pdu_new();
	if(pdu == NULL) goto error;
	memset(pdu, 0, sizeof(struct smpp_pdu));
	
	pdu->smsmsg = msg;
	fifo_push(fifo, pdu);

	return 1;

error:
	error(0, "fifo_push_smsmessage: returning error");
	return -1;
}

static int fifo_pop_smsmessage(fifostack *fifo, SMSMessage **msg) {

	struct smpp_pdu *pdu = NULL;
	int ret;

	ret = fifo_pop(fifo, &pdu);
	if(ret == 0) goto no_msg;
	if(ret < 0) goto error;

	*msg = pdu->smsmsg;
	if(*msg == NULL) goto error;

	pdu->smsmsg = NULL;
	pdu_free(pdu);

	return 1;

no_msg:
	return 0;

error:
	error(0, "fifo_pop_smsmessage: returning error");
	return -1;
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

	uint32_t quadoct;
	uint32_t realint;
	unsigned int olen = 0;

	olen = octstr_len(from);

	/* Check if [from] has enough data to contain a PDU (>=16 octets) */
	if(olen < 16) goto no_msg;

	/* Read the length (4 first octets) */
	octstr_get_many_chars((char*)&quadoct, from, 0, 4);

	/* Translate the length XXX MAKE THIS INTO 1 FUNCTION */
	realint = ntohl(quadoct);

	/* Check if we have [length] octets of data */
	if(olen < realint) goto no_msg;

	/* Cut the PDU out, move data to fill the hole. */
	*to = octstr_copy(from, 0, realint);
	if(*to == NULL) goto error;
	octstr_delete(from, 0, realint);

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

	size_t length;
	char   data[1024];
	Octstr *newstr = NULL;

	fd_set rf;
	struct timeval tox;
	int ret;

	memset(&data, 0, sizeof(data));

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
	length = read(fd, &data, sizeof(data)-1);

	if(length == -1) {
/*		if(errno==EWOULDBLOCK) return -1; */
		goto error;
	} else if(length == 0) {
		debug(0, "soketti <%i> kloused", fd);
		goto error;
	}

	newstr = octstr_create_from_data(data, length);
	
	if(newstr == NULL) goto error;
	octstr_insert(to, newstr, octstr_len(to));

	/* Okay, done */
	return 1;

no_data:
	return 0;

error:
	debug(0, "data_receive: error");
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

	size_t length, curl, written = 0;
	char *willy = NULL;

/*	debug(0, "data_send: starting"); */

	/* Create temp data structures. */
	length = octstr_len(from);
	if(length <= 0) return 0;

	willy = malloc(length);
	if(willy == NULL) goto error;

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
	free(willy);
	return 1;

socket_b0rken:
	debug(0, "data_send: broken socket!!!");
	free(willy);
	return -1;

error:
	debug(0, "data_send: ERROR!!!");
	free(willy);
	return -1;
}

/* Append a single octet of data. Update where to reflect the
   next byte to write to, decrease left by the number of
   bytes written. */
static int smpp_append_oct(char** where, int* left, uint32_t data) {

/* Change this to reflect differences in endianness and
   bytenesses. */
#if BYTE_ORDER == LITTLE_ENDIAN
	memcpy(*where, &data, 1);
	*where += 1;
#elif BYTE_ORDER == BIG_ENDIAN
	memcpy(*where, (&data)+3, 1);
	*where += 1;
#else
	/* We have to write another section here for PDPs :) */
	error(0, "smpp_append_oct: wrong endianness.");
#endif

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

static int smpp_append_int32(char** where, int* left, uint32_t data) {
	return -1;
	smpp_append_int32(where, left, data);
}

static int smpp_read_int32(char** where, int* left, uint32_t* data) {
	return -1;
	smpp_read_int32(where, left, data);
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

/*	case SMPP_QUERY_LAST_MSGS_RESP:
		ret = pdu_act_query_last_msgs_resp(smsc, pdu);
		break;

	case SMPP_QUERY_MSG_DETAILS_RESP:
		ret = pdu_act_query_msg_details_resp(smsc, pdu);
		break;
*/
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
#if 0

	case SMPP_SUBMIT_MULTI_RESP:
		ret = pdu_decode_submit_multi_resp(smsc, pdu);
		break;

	case SMPP_QUERY_SM_RESP:
		ret = pdu_decode_query_sm_resp(smsc, pdu);
		break;

/*
	case SMPP_QUERY_LAST_MSGS_RESP:
		ret = pdu_decode_query_last_msgs_resp(smsc, pdu);
		break;

	case SMPP_QUERY_MSG_DETAILS_RESP:
		ret = pdu_decode_query_msg_details_resp(smsc, pdu);
		break;
*/

	case SMPP_CANCEL_SM_RESP:
		ret = pdu_decode_cancel_sm_resp(smsc, pdu);
		break;

	case SMPP_REPLACE_SM_RESP:
		ret = pdu_decode_replace_sm_resp(smsc, pdu);
		break;

	case SMPP_ENQUIRE_LINK:
		ret = pdu_decode_enquire_link(smsc, pdu);
		break;

	case SMPP_ENQUIRE_LINK_RESP:
		ret = pdu_decode_enquire_link_resp(smsc, pdu);
		break;

	case SMPP_GENERIC_NAK:
		ret = pdu_decode_generic_nak(smsc, pdu);
		break;
#endif
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

	struct Octstr *body = NULL, *header = NULL, *whole = NULL;
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

#if 0
	case SMPP_SUBMIT_MULTI:
		ret = pdu_encode_submit_multi(pdu, &body);
		break;

	case SMPP_QUERY_SM:
		ret = pdu_encode_query_sm(pdu, &body);
		break;

/*
	case SMPP_QUERY_LAST_MSGS:
		ret = pdu_encode_query_last_msgs(pdu, &body);
		break;

	case SMPP_QUERY_MSG_DETAILS:
		ret = pdu_encode_query_msg_details(pdu, &body);
		break;
*/

	case SMPP_CANCEL_SM:
		ret = pdu_encode_cancel_sm(pdu, &body);
		break;

	case SMPP_REPLACE_SM:
		ret = pdu_encode_replace_sm(pdu, &body);
		break;

	case SMPP_ENQUIRE_LINK:
		ret = pdu_encode_enquire_link(pdu, &body);
		break;

	case SMPP_ENQUIRE_LINK_RESP:
		ret = pdu_encode_enquire_link_resp(pdu, &body);
		break;

	case SMPP_GENERIC_NAK:
		ret = pdu_encode_generic_nak(pdu, &body);
		break;
#endif

	}

	pdu_header_encode(pdu, &header);

	if(body != NULL) {
		whole = octstr_cat(header, body);
	} else {
		whole = header;
		header = NULL;
	}

	octstr_destroy(header);
	octstr_destroy(body);

	*rawdata = whole;

	return 1;
}


static int pdu_header_decode(smpp_pdu *pdu, Octstr *str) {

	uint32_t header[4];

	if(pdu == NULL) goto error;
	if(str == NULL) goto error;

	/* Read the header */
	octstr_get_many_chars((char*)&header, str, 0, sizeof(header));

	pdu->length = ntohl(header[0]);
	pdu->id     = ntohl(header[1]);
	pdu->status = ntohl(header[2]);
	pdu->sequence_no = ntohl(header[3]);

	return 1;

error:
	return -1;
}

static int pdu_header_encode(smpp_pdu *pdu, Octstr **rawdata) {

	uint32_t length, id, status, seq;

	Octstr *newdata = NULL;
	char temp[16], *tempptr;

	memset(temp, 0, sizeof(temp));

	tempptr = temp;

	length = htonl(pdu->length);
	id     = htonl(pdu->id);
	status = htonl(pdu->status);
	seq    = htonl(pdu->sequence_no);

	memcpy(tempptr, &length, 4);
	tempptr += 4;
	
	memcpy(tempptr, &id, 4);
	tempptr += 4;

	memcpy(tempptr, &status, 4);
	tempptr += 4;

	memcpy(tempptr, &seq, 4);
	tempptr += 4;

	newdata = octstr_create_from_data(temp, sizeof(temp));

	*rawdata = newdata;

	return 1;
}

static int pdu_decode_bind(smpp_pdu *pdu, Octstr *str) {

	return 1;
}

static int pdu_encode_bind(smpp_pdu *pdu, Octstr **str) {

	int length = 0;
	char *data = NULL, *where = NULL;
	Octstr *body_encoded;
	int left;
	
	struct smpp_pdu_bind_receiver *bind_receiver;
	struct smpp_pdu_bind_receiver_resp *bind_receiver_resp;
	struct smpp_pdu_bind_transmitter *bind_transmitter;
	struct smpp_pdu_bind_transmitter_resp *bind_transmitter_resp;

	switch(pdu->id) {

	case SMPP_BIND_RECEIVER:
		bind_receiver = (smpp_pdu_bind_receiver*) pdu->message_body;
		length = strlen(bind_receiver->system_id) + 1 +
			 strlen(bind_receiver->password) + 1 +
			 strlen(bind_receiver->system_type) + 1 +
			 1 + 1 + 1 +
			 strlen(bind_receiver->address_range) + 1;
		data = malloc(length);
		if(data == NULL) goto error;
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
		data = malloc(length);
		if(data == NULL) goto error;
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

		data = malloc(length);
		if(data == NULL) goto error;
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
		data = malloc(length);
		if(data == NULL) goto error;

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

	body_encoded = octstr_create_from_data(data, length);

	*str = body_encoded;

	free(data);

	return 1;
error:
	return -1;
}

/******************************************************************************
* PDUs
*  BIND_TRANSMITTER, BIND_TRANSMITTER_RESP
*/
static int pdu_act_bind_transmitter_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	struct smpp_pdu *newpdu = NULL;

	/* Validate *msg. */
	if(smsc == NULL) goto error;
	if(pdu == NULL) goto error;

	smsc->smpp_t_state = SMPP_STATE_BOUND;

	/* Process any messages that were sent through the HTTP
	   interface while the transmitter connection was not
	   bound. */
	while( fifo_pop(smsc->unsent_mt, &newpdu) == 1 ) {
		fifo_push(smsc->fifo_t_out, newpdu);
	}

	return 0;

error:
	return -1;
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

/*	debug(0, "pdu_act_unbind_resp: start"); */

	/* Remove the status flag CAN_RECEIVE or CAN_SEND */

	return -1;
}

/******************************************************************************
* PDUs
*  SUBMIT_SM, SUBMIT_SM_RESP
*/
static int pdu_act_submit_sm_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	debug(0, "pdu_act_submit_sm_resp: start");

	/* Mark message the SUBMIT_SM_RESP refers to as 
	   acknowledged and remove it from smsc->smpp_fifostack. */

	debug(0, "pdu->length == %08x", pdu->length);
	debug(0, "pdu->id == %08x", pdu->id);
	debug(0, "pdu->status == %08x", pdu->status);
	debug(0, "pdu->sequence_no == %08x", pdu->sequence_no);

	return -1;
}

static int pdu_encode_submit_sm(smpp_pdu* pdu, Octstr** str) {

	struct smpp_pdu_submit_sm *submit_sm = NULL;

	uint32_t length;
	int left;
	char *data = NULL, *where = NULL;
	Octstr *newstr = NULL;

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

	data = malloc(length);
	if(data == NULL) goto error;
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
	/* To preserver 8bit do memcpy... don't care about &where since
	   this is the last variable... */
	memcpy(where, submit_sm->short_message, submit_sm->sm_length);

	newstr = octstr_create_from_data(data, length);

	*str = newstr;

	return 1;
error:
	return -1;
}

static int pdu_decode_submit_sm_resp(smpp_pdu* pdu, Octstr* str) {
	return -1;
}



/******************************************************************************
* PDUs
*  SUBMIT_MULTI, SUBMIT_SM_MULTI_RESP
*/
static int pdu_act_submit_multi_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	debug(0, "pdu_act_submit_multi_resp: start");

	/* Mark messages reffer to by the SUBMIT_MULTI_RESP as
	   acknowledged and remove them from smsc->fifostack. */

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

	/* Convert message from the Default Charset to ISO-8859-1. */
	charset_smpp_to_iso(deliver_sm->short_message);

	/* Push the SMSMessage structure on the smsc->received_mo fifostack. */
	fifo_push(smsc->received_mo, pdu);

	/* Push a DELIVER_SM_RESP structure on the smsc->fifo_r_out fifostack. */
	newpdu = pdu_new();
	if(newpdu == NULL) goto error;
	memset(newpdu, 0, sizeof(struct smpp_pdu));

	deliver_sm_resp = malloc(sizeof(struct smpp_pdu_deliver_sm_resp));
	if(deliver_sm_resp == NULL) goto error;
	memset(deliver_sm_resp, 0, sizeof(struct smpp_pdu_deliver_sm_resp));

	newpdu->length = 17;
	newpdu->id = SMPP_DELIVER_SM_RESP;
	newpdu->status = 0;
	newpdu->sequence_no = pdu->sequence_no;
	newpdu->message_body = deliver_sm_resp;

	fifo_push(smsc->fifo_r_out, newpdu);

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

	if(pdu==NULL) goto error;
	if(str==NULL) goto error;

	if(octstr_len(str) < 16) {
		warning(0, "pdu_decode_deliver_sm: incorrect input");
		goto error;
	}

	deliver_sm = malloc(sizeof(struct smpp_pdu_deliver_sm));
	if(deliver_sm==NULL) goto error;
	memset(deliver_sm, 0, sizeof(struct smpp_pdu_deliver_sm));
	pdu->message_body = deliver_sm;

	buff = malloc(octstr_len(str));
	if(buff == NULL) goto error;

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
		(deliver_sm->sm_length > sizeof(deliver_sm->short_message)) ? 
			sizeof(deliver_sm->short_message) : deliver_sm->sm_length);
	start = end+1;

	free(buff);

	debug(0, "pdu->service_type == %s", deliver_sm->service_type);
	
	debug(0, "pdu->source_addr_ton == %i", deliver_sm->source_addr_ton);
	debug(0, "pdu->source_addr_npi == %i", deliver_sm->source_addr_npi);
	debug(0, "pdu->source_addr == %s", deliver_sm->source_addr);
	
	debug(0, "pdu->dest_addr_ton == %i", deliver_sm->dest_addr_ton);
	debug(0, "pdu->dest_addr_npi == %i", deliver_sm->dest_addr_npi);
	debug(0, "pdu->dest_addr == %s", deliver_sm->dest_addr);
	
	debug(0, "pdu->esm_class == %i", deliver_sm->esm_class);
	debug(0, "pdu->protocol_id == %i", deliver_sm->protocol_id);
	debug(0, "pdu->priority_flag == %i", deliver_sm->priority_flag);
	
	debug(0, "pdu->schedule_delivery_time == %s", deliver_sm->schedule_delivery_time);
	debug(0, "pdu->validity_period == %s", deliver_sm->validity_period);

	debug(0, "pdu->registered_delivery_flag == %i", deliver_sm->registered_delivery_flag);
	debug(0, "pdu->replace_if_present_flag == %i", deliver_sm->replace_if_present_flag);
	debug(0, "pdu->data_coding == %i", deliver_sm->data_coding);
	debug(0, "pdu->sm_default_msg_id == %i", deliver_sm->sm_default_msg_id);
	debug(0, "pdu->sm_length == %i", deliver_sm->sm_length);

	start = malloc( 4 );
	end = malloc( (deliver_sm->sm_length*3) + 1 );
	if(start == NULL) goto error;
	if(end == NULL) goto error;
	memset(end, 0, (deliver_sm->sm_length*3) + 1);
	for(oct=0; oct < deliver_sm->sm_length; oct++) {
		sprintf(start, "%02x ", (unsigned char) deliver_sm->short_message[oct]);
		strcat(end, start);
	}
	debug(0, "pdu->short_message == %s", end);

	free(start);
	free(end);	

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

#if 0
static int pdu_act_query_last_msgs_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	/* Ignore, this version doesn't send messages which
	   get these responses. */

	return -1;
}
	
static int pdu_act_query_msg_details_resp(SMSCenter *smsc, smpp_pdu *pdu) {

	/* Ignore, this version doesn't send messages which
	   get these responses. */

	return -1;
}
#endif

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
		fifo_push(smsc->fifo_t_out, newpdu);
	} else if(pdu->fd == smsc->fd_r) {
		newpdu->sequence_no = smsc->seq_r++;
		fifo_push(smsc->fifo_r_out, newpdu);
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

	debug(0, "pdu->length == %08x", pdu->length);
	debug(0, "pdu->id == %08x", pdu->id);
	debug(0, "pdu->status == %08x", pdu->status);
	debug(0, "pdu->sequence_no == %08x", pdu->sequence_no);

	/* Panic  */

	return -1;
}

static int charset_smpp_to_iso(char* data) {

	int i;

	while(*data != '\0') {
		i = 0;
		/* The translation table is 0 terminated. */
		while( translation_table[i].iso != 0 ) {
			if( translation_table[i].smpp == *data ) {
				*data = translation_table[i].iso;
				break;
			}
			i++;
		}
		data++;
	}

	return 1;
}

static int charset_iso_to_smpp(char* data) {

	int i;

	while(*data != '\0') {
		i = 0;
		/* The translation table is 0 terminated. */
		while( translation_table[i].iso != 0 ) {
			if( translation_table[i].iso == *data ) {
				*data = translation_table[i].smpp;
				break;
			}
			i++;
		}
		data++;
	}

	return 1;
}

