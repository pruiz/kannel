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

#include "gwlib.h"
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
    nr->added = 0;
    nr->last_mod = 0;
    return nr;
}


void rq_push_msg(RQueue *queue, RQueueItem *msg)
{
    mutex_lock(&queue->mutex);

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
    queue->added++;
    queue->last_mod = time(NULL);

    mutex_unlock(&queue->mutex);
}


void rq_push_msg_head(RQueue *queue, RQueueItem *msg)
{
    mutex_lock(&queue->mutex);

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
    queue->last_mod = time(NULL);

    mutex_unlock(&queue->mutex);
}


void rq_push_msg_ack(RQueue *queue, RQueueItem *msg)
{
    RQueueItem *ptr, *prev;
    
    mutex_lock(&queue->mutex);

    ptr = queue->first;
    prev = NULL;

    /* find last ACK/NACK
     */
    while(ptr) {
	if (ptr->msg_type != R_MSG_TYPE_ACK &&
	    ptr->msg_type != R_MSG_TYPE_NACK)

	    break;
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
    queue->last_mod = time(NULL);

    mutex_unlock(&queue->mutex);
}


/*
 * remove a message from queue. You must first seek it via
 * external functiomns and give pointer both to message and
 * its previous message (if any)
 *
 * cannot fail. NOTE: queue mutex must be reserved beforehand, and it is
 * NOT released!
 */
static void rq_remove_msg(RQueue *queue, RQueueItem *msg, RQueueItem *prev)
{
    if (prev == NULL)
	queue->first = msg->next;
    else	
	prev->next = msg->next;
	    
    if (msg == queue->last)
	queue->last = prev;
    queue->queue_len--;
    queue->last_mod = time(NULL);
}


RQueueItem *rq_pull_msg(RQueue *queue, int req_id)
{
    RQueueItem *ptr, *prev;
    
    mutex_lock(&queue->mutex);

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
    mutex_unlock(&queue->mutex);

    return ptr;	      
}


RQueueItem *rq_pull_msg_class(RQueue *queue, int class)
{
    RQueueItem *ptr, *prev;
    
    mutex_lock(&queue->mutex);

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
    mutex_unlock(&queue->mutex);

    return ptr;	      
}


int rq_change_destination(RQueue *queue, int class, int type, char *routing_str,
			   int original, int new_destination)
{
    RQueueItem *ptr;
    int tot = 0;
    
    mutex_lock(&queue->mutex);

    ptr = queue->first;
    while(ptr) {

	if (ptr->msg_class == class &&
	    ptr->msg_type == type &&
	    ptr->destination == original)

	    if (routing_str == NULL ||
		strcmp(routing_str, ptr->routing_info)==0) {

		ptr->destination = new_destination;
		tot++;
	    }
	
	ptr = ptr->next;
    }
    mutex_unlock(&queue->mutex);

    return tot;
}


int rq_queue_len(RQueue *queue, int *total)
{
    int retval;
    
    mutex_lock(&queue->mutex);

    retval = queue->queue_len;
    if (total != NULL)
	*total = queue->added;

    mutex_unlock(&queue->mutex);

    return retval;
}


time_t rq_oldest_message(RQueue *queue)
{
    time_t smallest;
    RQueueItem *ptr;
    
    mutex_lock(&queue->mutex);

    smallest = time(NULL);
    ptr = queue->first;
    while(ptr) {
	if (ptr->time_tag < smallest)
	    smallest = ptr->time_tag;
	ptr = ptr->next;
    }
    mutex_unlock(&queue->mutex);

    return smallest;  
}


time_t rq_last_mod(RQueue *queue)
{
    time_t val;
    
    mutex_lock(&queue->mutex);

    val = queue->last_mod;

    mutex_unlock(&queue->mutex);

    return val;  
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
    nqi->msg = NULL;

    nqi->time_tag = time(NULL);
    nqi->source = -1;
    nqi->destination = -1;	/* unknown */
    nqi->routing_info = NULL;
    
    nqi->next = NULL;

    return nqi;

error:
    error(0, "Failed to create new RQueueItem");
    free(nqi);
    return NULL;
}


void rqi_delete(RQueueItem *msg)
{
    msg_destroy(msg->msg);
    free(msg->routing_info);
    free(msg);
}


