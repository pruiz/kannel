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


typedef struct {
	Octstr *address;
	long port;
} WAPAddr;

typedef struct {
	WAPAddr *client, *server;
} WAPAddrTuple;


WAPAddr *wap_addr_create(Octstr *address, long port);
void wap_addr_destroy(WAPAddr *addr);

WAPAddrTuple *wap_addr_tuple_create(Octstr *cli_addr, long cli_port,
				    Octstr *srv_addr, long srv_port);
void wap_addr_tuple_destroy(WAPAddrTuple *tuple);

void init_queue(void);
void put_msg_in_queue(Msg *msg);
Msg *remove_msg_from_queue(void);

#endif
