/*
 * wap_addr.c - implement WAPAddr and WAPAddrTuple types.
 */

#include <stdlib.h>

#include "gwlib/gwlib.h"
#include "wap_addr.h"


WAPAddr *wap_addr_create(Octstr *address, long port) 
{
    WAPAddr *addr;
    
    addr = gw_malloc(sizeof(*addr));
    addr->address = octstr_duplicate(address);
    addr->port = port;
    return addr;
}


void wap_addr_destroy(WAPAddr *addr) 
{
    if (addr != NULL) {
	octstr_destroy(addr->address);
	gw_free(addr);
    }
}


int wap_addr_same(WAPAddr *a, WAPAddr *b) 
{
    return a->port == b->port && octstr_compare(a->address, b->address) == 0;
}


WAPAddrTuple *wap_addr_tuple_create(Octstr *cli_addr, long cli_port,
    	    	    	    	    Octstr *srv_addr, long srv_port) 
{
    WAPAddrTuple *tuple;
    
    tuple = gw_malloc(sizeof(*tuple));
    tuple->client = wap_addr_create(cli_addr, cli_port);
    tuple->server = wap_addr_create(srv_addr, srv_port);
    return tuple;
}


void wap_addr_tuple_destroy(WAPAddrTuple *tuple) 
{
    if (tuple != NULL) {
	wap_addr_destroy(tuple->client);
	wap_addr_destroy(tuple->server);
	gw_free(tuple);
    }
}


int wap_addr_tuple_same(WAPAddrTuple *a, WAPAddrTuple *b) 
{
    return wap_addr_same(a->client, b->client) &&
    	   wap_addr_same(a->server, b->server);
}


WAPAddrTuple *wap_addr_tuple_duplicate(WAPAddrTuple *tuple) 
{
    if (tuple == NULL)
	return NULL;
    
    return wap_addr_tuple_create(tuple->client->address,
    	    	    	    	 tuple->client->port,
				 tuple->server->address,
				 tuple->server->port);
}


void wap_addr_tuple_dump(WAPAddrTuple *tuple) 
{
    debug("wap", 0, "WAPAddrTuple %p = <%s:%ld> - <%s:%ld>", 
	  (void *) tuple,
	  octstr_get_cstr(tuple->client->address),
	  tuple->client->port,
	  octstr_get_cstr(tuple->server->address),
	  tuple->server->port);
}
