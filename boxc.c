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

#include <sys/types.h>
#include <sys/socket.h>

#include "wapitlib.h"
#include "boxc.h"
#include "bb_msg.h"


BOXC *boxc_open(int fd)
{
    struct sockaddr client_addr;
    socklen_t client_addr_len;
    BOXC *nb;

    nb = malloc(sizeof(BOXC));
    if (nb == NULL)
	goto error;

    if (fd < 0) {
	nb->fd = fd;
    } else {
	info(0, "Accepting the new client...");
	
	nb->fd = accept(fd, &client_addr, &client_addr_len);
	if (nb->fd < 0)
	    goto error;

	info(0, "Client connected.");

	/* TODO: do the hand-shake, baby, yeah-yeah! */
    }
    nb->load = 0;
    return nb;
    
error:
    error(errno, "Failed to create and open Box connection");
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
	char buffer[1024];
	    
	if (msg->msg_type != R_MSG_TYPE_ACK &&
	    msg->msg_type != R_MSG_TYPE_NACK) {

	    sprintf(buffer, "%d %s %s %s\n", msg->id, msg->sender,
		    msg->receiver, octstr_get_cstr(msg->msg));
	    write_to_socket(boxc->fd, buffer);

	    debug(0, "BOXC:write < %s >", buffer);
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
}


int boxc_get_message(BOXC *boxc, RQueueItem **rmsg)
{
    RQueueItem *msg;
    char *sender, *receiver, *text, *p;
    int ret, id;

    *rmsg = NULL;
    if (boxc->fd == BOXC_THREAD)
	/* msg = smsbox_get_msg(); */
	;
    else {
	char buffer[1025];
	
	if (read_available(boxc->fd) > 0) {
	    /*
	     * Note: the following blocks the connection if there is
	     * data without a linefeed. But that's life, smsbox would not
	     * accept our data neither if it has blocked while writing,
	     * or would it?
	     */
	    ret = read_line(boxc->fd, buffer, 1024);
	    if (ret < 1)
		return -1;	/* time to die */
	    
	    boxc->box_heartbeat = time(NULL);		/* update heartbeat */
	    
	    if (*buffer == 'A' || *buffer == 'N') {	/* ignore ack/nack */
		debug(0, "BOXC: ACK/NACK read < %s >, ignore", buffer);
		return 0;
	    }
	    else if (*buffer == 'H') {		/* heartbeat/load */
		boxc->load = atoi(buffer+1);
		debug(0, "BOXC: Load factor %d received", boxc->load);
		return 0;
	    }
	    info(0, "BOXC:read: < %s >", buffer);
	    
	    msg = rqi_new(R_MSG_CLASS_SMS, R_MSG_TYPE_MT);
	    if (msg == NULL) {
		error(0, "Failed to create new message, killing thread");
		return -1;
	    }
	    id = atoi(buffer);
	    p = strchr(buffer, ' ');
	    if (p == NULL)
		goto malformed;
	    else {
		*p++ = '\0';
		sender = p;
		p = strchr(sender, ' ');
		if (p == NULL)
		    goto malformed;
		else {
		    *p++ = '\0';
		    receiver = p;
		    p = strchr(receiver, ' ');
		    if (p == NULL)
			goto malformed;
		    else {
			*p++ = '\0';
			text = p;
		    }
		}
	    }
	    msg->sender = strdup(sender);
	    msg->receiver = strdup(receiver);
	    msg->msg = octstr_create(text);
	    if (msg->sender == NULL || msg->receiver == NULL || msg->msg == NULL) {
		error(0, "Memory allocation failed, send NACK");
		goto error;
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

malformed:
    error(0, "Received a malformed message from SMS BOX, send immediate NACK");
error:
    write_to_socket(boxc->fd, "N\n");
    rqi_delete(msg);
    return 0;
}
 
