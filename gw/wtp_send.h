/*
 * wtp_send.h - WTP implementation, message module header
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#ifndef WTP_SEND_H
#define WTP_SEND_H

#include "msg.h"
#include "wtp_init.h"
#include "wtp_resp.h"
#include "wap-events.h"
#include "wapbox.h"
#include "wtp.h"
#include "wtp_pdu.h"

/*
 * Sends a message object, having type wdp_datagram, having invoke PDU as user
 * data. Fetches address, tid and tid_new from the iniator state machine, 
 * other fields from event. Only for the wtp iniator.
 *
 * Return message to be sended
 */

Msg *wtp_send_invoke(WTPInitMachine *init_machine, WAPEvent *event);

/*
 * Send a message object, of wdp datagram type, having result PDU as user 
 * data. Fetches SDU from WTP event, address four-tuple and machine state 
 * information (are we resending the packet) from WTP machine. Handles all 
 * errors by itself. Returns message, if OK, else NULL. Only for wtp 
 * responder.
 */

Msg *wtp_send_result(WTPRespMachine *resp_machinemachine, WAPEvent *event); 

/*
 * Resend an already packed packet. Inputs are message to be resend and the 
 * value of rid (the retransmission indicator.). 
 */
void wtp_resend(Msg *msg, long rid);

/*
 * Send a message object, of wdp datagram type, having abort PDU as user 
 * data. Fetches SDU from WTP event, address four-tuple from WTP machine. 
 * Handles all errors by itself. Both for wtp iniator and responder.
 */

void wtp_send_abort(long abort_type, long abort_reason, long tid, 
                    WAPAddrTuple *address);

/*
 * Send a message object, of wdp datagram type, having ack PDU as user 
 * data. Creates SDU by itself, fetches address four-tuple and machine state
 * from WTP machine. Ack_type is a flag telling whether we are doing tid 
 * verification or not, rid_flag tells are we retransmitting. Handles all 
 * errors by itself. Both for wtp iniator and responder.
 */

void wtp_send_ack(long ack_type, int rid_flag, long tid, 
                  WAPAddrTuple *address);

void wtp_send_group_ack(WAPAddrTuple *address, int tid, 
                        int retransmission_status, 
                        unsigned char packet_sequence_number); 

void wtp_send_negative_ack(WAPAddrTuple *address, int tid, 
                           int retransmission_status, int segments_missing,
                           WTPSegment *missing_segments);

void wtp_send_address_dump(WAPAddrTuple  *address);

#endif




