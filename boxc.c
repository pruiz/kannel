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
	nb->fd = accept(fd, &client_addr, &client_addr_len);
	if (nb->fd < 0)
	    goto error;

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
    if (boxc->fd == BOXC_THREAD)
	/* smsbox_add_msg(msg); */
	;
    else
	; /* write into socket etc. */
    
    if (msg->msg_type == R_MSG_TYPE_MO) {
	msg->msg_type = R_MSG_TYPE_NACK;	/* failed */
	rq_push_msg_head(reply_queue, msg);
    }
    else
	rqi_delete(msg);
    return 0;
}


int boxc_get_message(BOXC *boxc, RQueue *reply_queue)
{
    RQueueItem *msg;
    
    if (boxc->fd == BOXC_THREAD)
	/* msg = smsbox_get_msg(); */
	;
    else
	msg = NULL; /* read from socket etc. */

    if (msg) {
	rq_push_msg(reply_queue, msg);
	return 1;
    }
    return 0;
}
 
