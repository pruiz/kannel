/*
 * wapbox.h
 */


#ifndef WAPBOX_H
#define WAPBOX_H

#include "msg.h"

typedef struct {
	Octstr *address;
	long port;
} WAPAddr;

typedef struct {
	WAPAddr *client, *server;
} WAPAddrTuple;


WAPAddr *wap_addr_create(Octstr *address, long port);
void wap_addr_destroy(WAPAddr *addr);
int wap_addr_same(WAPAddr *a, WAPAddr *b);

WAPAddrTuple *wap_addr_tuple_create(Octstr *cli_addr, long cli_port,
				    Octstr *srv_addr, long srv_port);
void wap_addr_tuple_destroy(WAPAddrTuple *tuple);
int wap_addr_tuple_same(WAPAddrTuple *a, WAPAddrTuple *b);
WAPAddrTuple *wap_addr_tuple_duplicate(WAPAddrTuple *tuple);
void wap_addr_tuple_dump(WAPAddrTuple *tuple);

/* XXX these should be renamed and made into a proper WDP layer */
void init_queue(void);
void put_msg_in_queue(Msg *msg);
Msg *remove_msg_from_queue(void);

#endif
