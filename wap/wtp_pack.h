/*
 * wtp_pack.h - WTP implementation, message module header
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#ifndef WTP_SEND_H
#define WTP_SEND_H

#include "gwlib/gwlib.h"
#include "wap_events.h"
#include "wtp_init.h"
#include "wtp_resp.h"
#include "wtp.h"
#include "wap.h"

/*
 * Create a datagram event, having invoke PDU as user data. Fetches address,
 * tid and tid_new from the initiator state machine, other fields from event.
 * Only for the wtp initiator.
 *
 * Return message to be sent.
 */

WAPEvent *wtp_pack_invoke(WTPInitMachine *init_machine, WAPEvent *event);

/*
 * Create a datagram event, having result PDU as user data. Fetches SDU
 * from WTP event, address four-tuple and machine state information
 * (are we resending the packet) from WTP machine. Handles all 
 * errors by itself. Returns message, if OK, else NULL. Only for wtp 
 * responder.
 */

WAPEvent *wtp_pack_result(WTPRespMachine *resp_machine, WAPEvent *event); 

/*
 * Same as above but for a segmented result.
 */
WAPEvent *wtp_pack_sar_result(WTPRespMachine *resp_machine, int psn); 

/*
 * Create a datagram event, having abort PDU as user data. Fetches SDU
 * from WTP event, address four-tuple from WTP machine. 
 * Handles all errors by itself. Both for wtp initiator and responder.
 */

WAPEvent *wtp_pack_abort(long abort_type, long abort_reason, long tid, 
                         WAPAddrTuple *address);

/*
 * Create a datagram event, having ack PDU as user data. Creates SDU by
 * itself, fetches address four-tuple and machine state from WTP machine.
 * Ack_type is a flag telling whether we are doing tid verification or not,
 * rid_flag tells are we retransmitting. Handles all errors by itself.
 * Both for wtp initiator and responder.
 */

WAPEvent *wtp_pack_ack(long ack_type, int rid_flag, long tid, 
                       WAPAddrTuple *address);

/*
 * Same as above but for a segmented ack
 */
WAPEvent *wtp_pack_sar_ack(long ack_type, long tid, WAPAddrTuple *address, int psn);

/*
 * Set or unset the retransmission indicator on a PDU that has already
 * been packed as a datagram.  dgram must be of type T_DUnitdata_Req.
 */
void wtp_pack_set_rid(WAPEvent *dgram, long rid);
#endif
