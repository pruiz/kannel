/*
 * GATEWAY BEARER BOX
 *
 * Message queues and message object types
 *
 * Kalle Marjola for Wapit ltd. 1999
 *
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "wapitlib.h"
#include "bb_msg.h"


RQueue *rq_new(void)
{
    RQueue *nr;
    nr = malloc(sizeof(RQueue));
    if (nr == NULL) {
	error(errno, "Failed to malloc new RQueue");
	return NULL;
    }
    nr->first = nr->last = NULL;
    pthread_mutex_init(&nr->mutex, NULL);
    nr->id_max = 1;
    nr->queue_len = 0;
    return nr;
}


int rq_push_msg(RQueue *queue, RQueueItem *msg)
{
    int ret;
    
    ret = pthread_mutex_lock(&queue->mutex);
    
    if (ret != 0)
	goto error;

    msg->next = NULL;
    if (queue->last != NULL)
	queue->last->next = msg;
    queue->last = msg;
    if (queue->first == NULL)
	queue->first = msg;
    
    msg->id = queue->id_max;
    if (queue->id_max < ID_MAX)
	queue->id_max++;
    else
	queue->id_max = 1;

    queue->queue_len++;
    ret = pthread_mutex_unlock(&queue->mutex);
    if (ret != 0)
	goto error;

    return 0;
    
error:
    error(ret, "Failed to push message");
    return -1;
}


int rq_push_msg_head(RQueue *queue, RQueueItem *msg)
{
    int ret;
    
    ret = pthread_mutex_lock(&queue->mutex);
    
    if (ret != 0)
	goto error;

    msg->next = queue->first;
    queue->first = msg;
    if (queue->last == NULL)
	queue->last = msg;

    msg->id = queue->id_max;
    if (queue->id_max < ID_MAX)
	queue->id_max++;
    else
	queue->id_max = 1;

    queue->queue_len++;
    ret = pthread_mutex_unlock(&queue->mutex);
    if (ret != 0)
	goto error;

    return 0;
    
error:
    error(ret, "Failed to push message to head");
    return -1;
}


int rq_push_msg_ack(RQueue *queue, RQueueItem *msg)
{
    int ret;
    RQueueItem *ptr, *prev;
    
    ret = pthread_mutex_lock(&queue->mutex);
    
    if (ret != 0)
	goto error;

    ptr = queue->first;
    prev = NULL;

    /* find last ACK/NACK
     */
    while(ptr) {
	if (ptr->msg_type != R_MSG_TYPE_ACK &&
	    ptr->msg_type != R_MSG_TYPE_NACK)

	    return prev;
	prev = ptr;
	ptr = ptr->next;
    }
    if (prev == NULL) {
	msg->next = queue->first;
	queue->first = msg;
	if (queue->last == NULL)
	    queue->last = msg;
    }
    else {
	msg->next = prev->next;
	prev->next = msg;
	if (queue->last == prev)
	    queue->last = msg;
    }
    msg->id = queue->id_max;
    if (queue->id_max < ID_MAX)
	queue->id_max++;
    else
	queue->id_max = 1;

    queue->queue_len++;
    ret = pthread_mutex_unlock(&queue->mutex);
    if (ret != 0)
	goto error;

    return 0;
    
error:
    error(ret, "Failed to push acknowledgement");
    return -1;
}


void rq_remove_msg(RQueue *queue, RQueueItem *msg, RQueueItem *prev)
{
    if (prev == NULL)
	queue->first = msg->next;
    else	
	prev->next = msg->next;
	    
    if (msg == queue->last)
	queue->last = prev;
    queue->queue_len--;
}


RQueueItem *rq_pull_msg(RQueue *queue, int req_id)
{
    int ret;
    RQueueItem *ptr, *prev;
    
    ret = pthread_mutex_lock(&queue->mutex);
    if (ret != 0)
	goto error;

    ptr = queue->first;
    prev = NULL;
    while(ptr) {

	if (ptr->source == req_id || ptr->destination == req_id) { 
	    rq_remove_msg(queue, ptr, prev);
	    break;
	}
	prev = ptr;
	ptr = ptr->next;
    }
    ret = pthread_mutex_unlock(&queue->mutex);
    if (ret != 0)
	goto error;

    return ptr;	      
    
error:
    error(ret, "Failed to pull message");
    return NULL;
}


RQueueItem *rq_pull_msg_class(RQueue *queue, int class)
{
    int ret;
    RQueueItem *ptr, *prev;
    
    ret = pthread_mutex_lock(&queue->mutex);
    if (ret != 0)
	goto error;

    ptr = queue->first;
    prev = NULL;
    while(ptr) {

	if (ptr->msg_class == class &&
	    (ptr->msg_type == R_MSG_TYPE_MT ||
	     ptr->msg_type == R_MSG_TYPE_MO)) { 

	    rq_remove_msg(queue, ptr, prev);
	    break;
	}
	prev = ptr;
	ptr = ptr->next;
    }
    ret = pthread_mutex_unlock(&queue->mutex);
    if (ret != 0)
	goto error;

    return ptr;	      
    
error:
    error(ret, "Failed to pull message");
    return NULL;
}


int rq_queue_len(RQueue *queue)
{
    int ret;
    int retval;
    
    ret = pthread_mutex_lock(&queue->mutex);
    if (ret != 0)
	goto error;

    retval = queue->queue_len;

    ret = pthread_mutex_unlock(&queue->mutex);
    if (ret != 0)
	goto error;

    return retval;
    
error:
    error(ret, "Failed to inquire queue length");
    return -1;
}


/*-----------------------------------------------------
 *  RQueueItems
 */


RQueueItem *rqi_new(int class, int type)
{
    RQueueItem *nqi;
    nqi = malloc(sizeof(RQueueItem));
    if (nqi == NULL)
	goto error;
    
    nqi->id = -1;
    nqi->msg_class = class;
    nqi->msg_type = type;
    nqi->msg = octstr_create_empty();

    nqi->sender[0] = '\0';
    nqi->receiver[0] = '\0';
    nqi->time_tag = time(NULL);
    nqi->source = -1;
    nqi->destination = -1;	/* unknown */
    nqi->client_data = NULL;
    
    nqi->next = NULL;

    return nqi;

error:
    error(0, "Failed to create new RQueueItem");
    free(nqi);
    return NULL;
}


void rqi_delete(RQueueItem *msg)
{
    octstr_destroy(msg->msg);
    free(msg);
}


