/*
 * wtp_send.h - WTP implementation, message module header
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#ifndef WTP_SEND_H
#define WTP_SEND_H

#include "wtp.h"

/*
 * Send a message object, of wdp datagram type, having result PDU as user 
 * data. Fetches SDU from WTP event, address four-tuple and machine state 
 * information (are we resending the packet) from WTP machine. Handles all 
 * errors by itself.
 */

void wtp_send_result(WTPMachine *machine, WTPEvent *event); 

/*
 * Send a message object, of wdp datagram type, having abort PDU as user 
 * data. Fetches SDU from WTP event, address four-tuple from WTP machine. 
 * Handles all errors by itself.
 */
void wtp_send_abort(long abort_type, long abort_reson, WTPMachine *machine, 
     WTPEvent *event);

/*
 * Send a message object, of wdp datagram type, having ack PDU as user 
 * data. Creates SDU by itself, fetches address four-tuple and machine state
 * from WTP machine. Ack_type is a flag telling whether we are doing tid 
 * verification or not. Handles all errors by itself.
 */

void wtp_send_ack(long ack_type, WTPMachine *machine, WTPEvent *event);

void wtp_send_group_ack(Address *address, long tid, int retransmission_status, 
                        char packet_sequence_number); 

void wtp_send_negative_ack(Address *address, long tid, 
                           int retransmission_status, int segments_missing,
                           int *missing_segments);
#endif




