/*
 * GATEWAY BEARER BOX
 *
 * Message queues and message object types
 *
 * Kalle Marjola for Wapit ltd. 1999
 *
 */

#ifndef _BB_MSG_H
#define _BB_MSG_H

#include <sys/time.h>

#include "gwlib/gwlib.h"
#include "msg.h"

typedef struct r_queue_item RQueueItem;
typedef struct r_queue RQueue;

/*---------------------------
 * message types
 * note that MO-messages only appear in Request Queue,
 * and MT-messages only appear in Reply Queue
 */

/* message class */
enum {
    R_MSG_CLASS_WAP,		/* UDP/SMSC <-> WAP BOX */
    R_MSG_CLASS_SMS		/* SMSC <-> SMS BOX */
};

/* message type */
enum {			
    R_MSG_TYPE_MO,
    R_MSG_TYPE_MT,
    R_MSG_TYPE_ACK,
    R_MSG_TYPE_NACK
};    

/*
 * Request/reply message type
 */

struct r_queue_item {
    int id;		/* internal number */
    int msg_class;	/* see enum above */
    int msg_type;	/* see enum above */
    Msg *msg;		/* from msg.h */
    time_t time_tag;	/* when created (in our system) */
    int source;		/* original receiver thread id */
    int destination;	/* destination thread, if we know it */

    char *routing_info;	/* optional extra information for router module */
    
    RQueueItem *next;	/* linked list */
};

/*
 * Request/reply queue structure
 * The queue is watched over by the mutex; no pull/push allowed unless
 * it is first locked via 'mutex'
 */

#define ID_MAX 1000000000

struct r_queue {
    RQueueItem *first, *last;

    int id_max;
    int queue_len;		/* items in queue */
    int added;			/* total number of messages added */
    time_t last_mod;		/* time of the last modification */
    Mutex *mutex;
};


/*-------------------------------------------------------
 * RQueue
 *
 * initialize a new RQueue
 * return pointer to it, or NULL if failed
 */
RQueue *rq_new(void);


/*
 * push a new message to the queue. Cannot fail (as long as
 *  user does not give crap)
 */
void rq_push_msg(RQueue *queue, RQueueItem *msg);

/*
 * as above, but pushes to head (and does NOT increase 'added')
 */
void rq_push_msg_head(RQueue *queue, RQueueItem *msg);

/*
 * push an acknowledgement/NACK. It is pushed after last ACK/NACK
 * in the queue, and into head if there is none
 * (does NOT increase 'added')
 */
void rq_push_msg_ack(RQueue *queue, RQueueItem *msg);

/*
 * pull a message from queue which has source or destination
 * identical to requester id
 *
 * returns pulled message or NULL if not found (or on error)
 */
RQueueItem *rq_pull_msg(RQueue *queue, int req_id);

/*
 * as above, but pulls any message of the given class (WAP/SMS)
 * NOTE: ACK/NACK messages are not pulled with this function!
 */
RQueueItem *rq_pull_msg_class(RQueue *queue, int class);

/*
 * change all messages of class 'class', type 'type' and routing_str same
 * as 'routing_str' (unless it is NULL) with destination
 * as 'original' into 'new_destination'
 *
 * Return total number of messages re-routed
 */
int rq_change_destination(RQueue *queue, int class, int type,
			  char *routing_str,
			  int original, int new_destination);

/*
 * return the current length of the queue
 * if total is set, puts the 'added' value into it
 */
int rq_queue_len(RQueue *queue, int *total);

/*
 * return the time_tag of the oldest message in the queue
 */
time_t rq_oldest_message(RQueue *queue);

/*
 * return the time_tag of the last modification
 */
time_t rq_last_mod(RQueue *queue);


/*-------------------------------------------------------
 * RQueueItem
 *
 * create a new rqueue item - note that you must afterwards set
 * 'msg' and 'routing_info'
 */
RQueueItem *rqi_new(int class, int type);

/*
 * delete rqueue item
 * NOTE: does not remove it from the RQueue, so you must have done it first!
 * Frees 'msg' and 'routing_info', but does NOT free client-data
 */
void rqi_delete(RQueueItem *msg);



#endif
