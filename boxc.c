/*
 * BOX Connection
 *
 * Kalle Marjola for Wapit ltd.
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "wapitlib.h"
#include "http.h"
#include "boxc.h"
#include "bb_msg.h"
#include "octstr.h"
#include "msg.h"


BOXC *boxc_open(int fd)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    BOXC *nb;
    char accept_ip[NI_MAXHOST];

    nb = malloc(sizeof(BOXC));
    if (nb == NULL)
	goto error;

    if (fd < 0) {
	nb->fd = fd;
    } else {
	debug(0, "BOXC: Accepting a new client...");

	client_addr_len = sizeof(client_addr);
	nb->fd = accept(fd, (struct sockaddr *)&client_addr, &client_addr_len);
	if (nb->fd < 0)
	    goto error;

	memset(accept_ip, 0, sizeof(accept_ip));
        getnameinfo((struct sockaddr *)&client_addr, client_addr_len,
		    accept_ip, sizeof(accept_ip), 
		    NULL, 0, NI_NUMERICHOST);
        nb->client_ip = strdup(accept_ip);
	if (nb->client_ip == NULL) 
	    goto error;
	info(0, "BOXC: Client connected from <%s>", accept_ip);

	/* TODO: do the hand-shake, baby, yeah-yeah! */
    }
    nb->load = 0;
    return nb;
    
error:
    error(errno, "BOXC: Failed to create and open Box connection");
    free(nb);
    return NULL;

}


int boxc_close(BOXC *boxc)
{
    if (boxc == NULL)
	return 0;
    if (boxc->fd >= 0)
	close(boxc->fd);

    free(boxc);
    return 0;
}



int boxc_send_message(BOXC *boxc, RQueueItem *msg, RQueue *reply_queue)
{
    int ack = 0;
    
    if (boxc->fd == BOXC_THREAD)
	/* smsbox_add_msg(msg); */
	;
    else {
	if (msg->msg_type != R_MSG_TYPE_ACK &&
	    msg->msg_type != R_MSG_TYPE_NACK) {

	    Octstr *pack;

	    pack = msg_pack(msg->msg);
	    if (pack == NULL)
		goto error;

	    octstr_send(boxc->fd, pack);
	    octstr_destroy(pack);

	    if (msg->msg_class == R_MSG_CLASS_SMS)
		if(msg_type(msg->msg) == plain_sms) {
			debug(0, "BOXC:write < %s >", octstr_get_cstr(msg->msg->plain_sms.text));
		} else if(msg_type(msg->msg) == smart_sms) {
			debug(0, "BOXC:write < %s >", octstr_get_cstr(msg->msg->smart_sms.msgdata));
		}
	    else
		debug(0, "BOXC:write < WAP >");		
	    ack = 1;
	}
    }
    if (msg->msg_type == R_MSG_TYPE_MO) {
	if (ack)
	    msg->msg_type = R_MSG_TYPE_ACK;	/* done. */
	else
	    msg->msg_type = R_MSG_TYPE_NACK;	/* failed. */
	rq_push_msg_ack(reply_queue, msg);
    }
    else
	rqi_delete(msg);		/* delete ACK/NACK from SMSC/CSDR */
    return 0;
error:
    error(0, "BOXC: Send message failed");
    return -1;
}


int boxc_get_message(BOXC *boxc, RQueueItem **rmsg)
{
    RQueueItem *msg;
    int ret;

    *rmsg = NULL;
    if (boxc->fd == BOXC_THREAD)
	/* msg = smsbox_get_msg(); */
	;
    else {
	if (read_available(boxc->fd) > 0) {
	    Msg *pmsg;
	    Octstr *os;
	    
	    boxc->box_heartbeat = time(NULL);	/* update heartbeat */
	    
	    /*
	     * Note: the following blocks the connection if there is
	     * partial data. But that's life, smsbox would not
	     * accept our data neither if it has blocked while writing,
	     * or would it?
	     */
	    ret = octstr_recv(boxc->fd, &os);
	    if (ret < 0)
		return -1;	/* time to die */
	    
	    pmsg = msg_unpack(os);
	    if (pmsg == NULL)
		goto error;

	    if (msg_type(pmsg) == heartbeat) {
		boxc->load = pmsg->heartbeat.load;
		debug(0, "BOXC: Load factor %d received", boxc->load);

		octstr_destroy(os);
		msg_destroy(pmsg);
		return 0;
	    }
	    else if (msg_type(pmsg) == plain_sms) {
		
		msg = rqi_new(R_MSG_CLASS_SMS, R_MSG_TYPE_MT);
		if (msg == NULL) {
		    error(0, "Failed to create new message, killing thread");
		    return -1;
		}
		msg->msg = pmsg;
		debug(0, "BOXC: Read < %s >", octstr_get_cstr(pmsg->plain_sms.text));
	    }
	    else if (msg_type(pmsg) == wdp_datagram) {
		
		msg = rqi_new(R_MSG_CLASS_WAP, R_MSG_TYPE_MT);
		if (msg == NULL) {
		    error(0, "Failed to create new message, killing thread");
		    return -1;
		}
		msg->msg = pmsg;
		debug(0, "BOXC: Read < WAP >");
	    }
	}
	else 
	    msg = NULL; /* nothing to read */
    }
    if (msg) {
	*rmsg = msg;
	return 1;
    }
    return 0;

error:
    rqi_delete(msg);
    return -1;
}
 
