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


WAPAddrTuple *wap_addr_tuple_create(Octstr *rmt_addr, long rmt_port,
    	    	    	    	    Octstr *lcl_addr, long lcl_port) 
{
    WAPAddrTuple *tuple;
    
    tuple = gw_malloc(sizeof(*tuple));
    tuple->remote = wap_addr_create(rmt_addr, rmt_port);
    tuple->local = wap_addr_create(lcl_addr, lcl_port);
    return tuple;
}


void wap_addr_tuple_destroy(WAPAddrTuple *tuple) 
{
    if (tuple != NULL) {
	wap_addr_destroy(tuple->remote);
	wap_addr_destroy(tuple->local);
	gw_free(tuple);
    }
}


int wap_addr_tuple_same(WAPAddrTuple *a, WAPAddrTuple *b) 
{
    return wap_addr_same(a->remote, b->remote) &&
    	   wap_addr_same(a->local, b->local);
}


WAPAddrTuple *wap_addr_tuple_duplicate(WAPAddrTuple *tuple) 
{
    if (tuple == NULL)
	return NULL;
    
    return wap_addr_tuple_create(tuple->remote->address,
    	    	    	    	 tuple->remote->port,
				 tuple->local->address,
				 tuple->local->port);
}


void wap_addr_tuple_dump(WAPAddrTuple *tuple) 
{
    debug("wap", 0, "WAPAddrTuple %p = <%s:%ld> - <%s:%ld>", 
	  (void *) tuple,
	  octstr_get_cstr(tuple->remote->address),
	  tuple->remote->port,
	  octstr_get_cstr(tuple->local->address),
	  tuple->local->port);
}
