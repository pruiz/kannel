#ifndef _BOXC_H
#define _BOXC_H

#include <time.h>
#include "bb_msg.h"


#define BOXC_THREAD	-1


typedef struct boxc {
    int fd;		/* if BOXC_THREAD, just a separate thread in bearerbox */
    int load;
    time_t box_heartbeat;
    char *client_ip;

} BOXC;


/*
 * create a new BOX Connection by accepting it from given 'fd'.
 * 'ip_allow' and 'ip_deny' are allowed hosts to connect, see
 * documentation. NULL if not present
 *
 * do the handshake etc. and return the created new BOXC, or NULL
 * on failure
 *
 * if the fd is -1, do a thread box connection
 */ 
BOXC *boxc_open(int fd, char *ip_allow, char *ip_deny);

/*
 * close the BOX Connection (and fd)
 */
int boxc_close(BOXC *boxc);

/*
 * write message to our target;
 * after it has been acknowledged, copy message into reply queue
 * and mark as ACK - if the other rejects the message, mark as NACK
 *
 * ACK/NACK messages are simply deleted, naturally
 *
 * return 0 if work done, 1 if NACKed, -1 on error
 */
int boxc_send_message(BOXC *boxc, RQueueItem *msg, RQueue *reply_queue);

/*
 * receive, if any, message from BOX Connection
 * if any message received, add it automatically to reply_queue,
 * except if it was a heartbeat/load message
 *
 * return 0 if no message received, 1 if message received,
 * -1 on error/heartbeat timeout
 */
int boxc_get_message(BOXC *boxc, RQueueItem **msg);


#endif
