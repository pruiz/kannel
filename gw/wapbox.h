/*
 * wapbox.h
 */


#ifndef WAPBOX_H
#define WAPBOX_H

#include "msg.h"

/*
 * Shortest timer tick (in seconds, being shortest defined time amongst 
 * protocol timers) is currently defined. 
 */
#define WB_DEFAULT_TIMER_TICK 1
#define CONNECTIONLESS_PORT 9200

void init_queue(void);
void put_msg_in_queue(Msg *msg);
Msg *remove_msg_from_queue(void);

#endif
