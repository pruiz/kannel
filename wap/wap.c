/*
 * wap.c - Generic functions for wap library
 */

#include "gwlib/gwlib.h"
#include "wap.h"
#include "wtp.h"

#define CONNECTIONLESS_PORT 9200

void wap_dispatch_datagram(WAPEvent *dgram)
{
    gw_assert(dgram != NULL);

    if (dgram->type != T_DUnitdata_Ind) {
	warning(0, "wap_dispatch_datagram got event of unexpected type.");
	wap_event_dump(dgram);
	wap_event_destroy(dgram);
        return;
    }

    /* XXX Assumption does not hold for client side */
    if (dgram->u.T_DUnitdata_Ind.addr_tuple->local->port
        == CONNECTIONLESS_PORT) {
	wsp_unit_dispatch_event(dgram);
    } else {
        List *events;

        events = wtp_unpack_wdp_datagram(dgram);
	wap_event_destroy(dgram);
        while (list_len(events) > 0) {
	    WAPEvent *event;

	    event = list_extract_first(events);
            if (wtp_event_is_for_responder(event))
                wtp_resp_dispatch_event(event);
            else
                wtp_initiator_dispatch_event(event);
        }
        list_destroy(events, NULL);
    }
}
