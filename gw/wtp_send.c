/*                          
 * wtp_send.c - WTP message sending module implementation
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp_pdu.h"
#include "wtp_send.h"
#include "msg.h"
#include "wapbox.h"

/*
 * Readable names for octets
 */
enum {
     first_byte,
     second_byte,
     third_byte,
     fourth_byte
};

/*
 * Types of header information added by the user (TPIs, or transportation 
 * information items). 
 */
enum {
     ERROR_DATA = 0x00,
     INFO_DATA = 0x01,
     OPTION = 0x02,
     PACKET_SEQUENCE_NUMBER = 0x03
};

/*****************************************************************************
 *
 * Prototypes of internal functions
 */

static void add_datagram_address(Msg *msg, WTPMachine *machine);

static void add_segment_address(Msg *msg, Address *address);

static void add_direct_address(Msg *msg, WAPAddrTuple *address);

/*
 * Setting retransmission status of a already packed message.
 */
static void set_rid(Msg *msg, long rid);

/*
 * Returns retransmission status of the entire message
 */ 
static long message_rid(Msg *msg);

/*
 * WTP defines SendTID and RcvTID.  We should use SendTID in all PDUs
 * we send.  The RcvTID is the one we got from the initial Invoke and
 * is the one we expect on all future PDUs for this machine.  
 * SendTID is always RcvTID xor 0x8000.
 * 
 * Note that when we are the Initiator, for example with WSP PUSH,
 * we must still store the RcvTID in machine->tid, to be consistent
 * with the current code.  So we'll choose the SendTID and then calculate
 * the RcvTID.
 */
static unsigned short send_tid(WTPMachine *machine);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 * Sends a message object, of wdp datagram type, having result PDU as user 
 * data. Fetches SDU from WTP event, address four-tuple and machine state 
 * information (are we resending the packet or not) from WTP machine. Handles 
 * all errors by itself.
 *
 * Returns message to be sended, if succesfull, NULL otherwise. 
 */
Msg *wtp_send_result(WTPMachine *machine, WAPEvent *event){

     Msg *msg, *dup;
     WTP_PDU *pdu;
     
     gw_assert(event->type == TR_Result_Req);
     pdu = wtp_pdu_create(Result);
     pdu->u.Result.con = 0;
     pdu->u.Result.gtr = 1;
     pdu->u.Result.ttr = 1;
     pdu->u.Result.rid = 0;
     pdu->u.Result.tid = send_tid(machine);
     pdu->u.Result.user_data = 
     	octstr_duplicate(event->u.TR_Result_Req.user_data);

     msg = msg_create(wdp_datagram);
     add_datagram_address(msg, machine);
     msg->wdp_datagram.user_data = wtp_pdu_pack(pdu);
     wtp_pdu_destroy(pdu);
  
     dup = msg_duplicate(msg);
     put_msg_in_queue(msg);

     return dup;
}

/*
 * Resend an already packed packet. We must turn on rid bit first (if it is 
 * not already turned).
 */
void wtp_resend_result(Msg *result, long rid){

     if (message_rid(result) == 0) {
        debug("wap.wtp.send", 0, "WTP: resend: turning the first bit");
        set_rid(result, rid);
     }

     put_msg_in_queue(msg_duplicate(result));
}

/*
 * Sends a message object, of wdp datagram type, having abort header as user 
 * data. Fetches address four-tuple from WTP machine, tid from wtp event, abort 
 * type and reason from direct input. Handles all errors by itself.
 */
void wtp_send_abort(long abort_type, long abort_reason, WTPMachine *machine, 
     WAPEvent *event){

     Msg *msg = NULL;
     WTP_PDU *pdu;

     gw_assert(event->type == TR_Abort_Req || event->type == RcvErrorPDU);
     pdu = wtp_pdu_create(Abort);
     pdu->u.Abort.con = 0;
     pdu->u.Abort.abort_type = abort_type;
     pdu->u.Abort.tid = send_tid(machine);
     pdu->u.Abort.abort_reason = abort_reason;

     msg = msg_create(wdp_datagram);
     add_datagram_address(msg, machine);
     msg->wdp_datagram.user_data = wtp_pdu_pack(pdu);
     debug("wap.wtp_send", 0, "WTP_SEND: sending a message");
     msg_dump(msg, 0);
     
     wtp_pdu_destroy(pdu);

     put_msg_in_queue(msg);

     return;
}

/*
 * Same as previous, expect now abort type and reason, reply address and trans-
 * action tid are direct inputs. (This function is used when the transaction is 
 * aborted before calling the state machine).
 */
void wtp_do_not_start(long abort_type, long abort_reason, WAPAddrTuple *address, 
     int tid){

     Msg *msg = NULL;
     WTP_PDU *pdu;

     pdu = wtp_pdu_create(Abort);
     pdu->u.Abort.con = 0;
     pdu->u.Abort.tid = tid ^ 0x8000;
     pdu->u.Abort.abort_type = abort_type;
     pdu->u.Abort.abort_reason = abort_reason;

     msg = msg_create(wdp_datagram);
     add_direct_address(msg, address);
     msg->wdp_datagram.user_data = wtp_pdu_pack(pdu);
     debug("wap.wtp_send", 0, "putting a message in the queue");
     msg_dump(msg, 0);

     put_msg_in_queue(msg);
     debug("wap.wtp.send", 0, "WTP_SEND: do_not_start: aborted");
     wtp_pdu_destroy(pdu);

     return;
}

void wtp_send_ack(long ack_type, WTPMachine *machine, WAPEvent *event){

     Msg *msg = NULL;
     WTP_PDU *pdu;
     
     pdu = wtp_pdu_create(Ack);
     pdu->u.Ack.con = 0;
     pdu->u.Ack.tidverify = ack_type;
     pdu->u.Ack.rid = machine->rid;
     pdu->u.Ack.tid = send_tid(machine);

     msg = msg_create(wdp_datagram);
     add_datagram_address(msg, machine);
     msg->wdp_datagram.user_data = wtp_pdu_pack(pdu);
     
     wtp_pdu_destroy(pdu);

     put_msg_in_queue(msg);
     debug("wap.wtp.send", 0, "WTP_SEND: message put into the queue");  

     return;
}

void wtp_send_group_ack(Address *address, int tid, int retransmission_status, 
                        unsigned char packet_sequence_number){

     Msg *msg = NULL;
     WTP_PDU *pdu;

     pdu = wtp_pdu_create(Ack);
     pdu->u.Ack.con = 1;
     pdu->u.Ack.tidverify = 0;
     pdu->u.Ack.rid = retransmission_status;
     pdu->u.Ack.tid = tid ^ 0x8000;
     wtp_pdu_append_tpi(pdu, PACKET_SEQUENCE_NUMBER,
     		octstr_create_from_data(&packet_sequence_number, 1));

     msg = msg_create(wdp_datagram);
     add_segment_address(msg, address);
     msg->wdp_datagram.user_data = wtp_pdu_pack(pdu);

     wtp_pdu_destroy(pdu);

     put_msg_in_queue(msg); 

     return;
}

void wtp_send_negative_ack(Address *address, int tid, int retransmission_status,
                           int segments_missing, WTPSegment *missing_segments){
     
     Msg *msg = NULL;
     WTP_PDU *pdu;

     warning(0, "Cannot send negative ack, SAR not implemented.");
     return;

     pdu = wtp_pdu_create(Negative_ack);
     pdu->u.Negative_ack.con = 0;
     pdu->u.Negative_ack.rid = retransmission_status;
     pdu->u.Negative_ack.tid = tid ^ 0x8000;
     pdu->u.Negative_ack.nmissing = segments_missing;
     /* XXX: Convert missing_segments to an octstr of packet sequence numbers */
     pdu->u.Negative_ack.missing = NULL;
     
     msg = msg_create(wdp_datagram);
     add_segment_address(msg, address);
     msg->wdp_datagram.user_data = wtp_pdu_pack(pdu);

     put_msg_in_queue(msg); 

     return;
}

void wtp_send_address_dump(WAPAddrTuple *address){

       if (address != NULL){
          debug("wap.wtp.send", 0, "WTP_SEND: address dump starting");
          debug("wap.wtp.send", 0, "WTP_SEND: source address");
          octstr_dump(address->client->address, 1);
          debug("wap.wtp.send", 0, "WTP_SEND: source port %ld: ", 
                 address->client->port);
          debug("wap.wtp.send", 0, "WTP_SEND: destination address");
                 octstr_dump(address->server->address, 1);
          debug("wap.wtp.send", 0, "WTP_SEND: destination port %ld: ", 
                address->server->port);
       } else
         debug("wap.wtp_send", 0, "Address pointing NULL");
}

/****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 */

/* 
 * We must swap the source and the destination, because we are answering a query.
 */
static void add_datagram_address(Msg *msg, WTPMachine *machine){

       msg->wdp_datagram.source_address = 
    	    octstr_duplicate(machine->addr_tuple->server->address);

       msg->wdp_datagram.source_port = machine->addr_tuple->server->port;

       msg->wdp_datagram.destination_address = 
    	    octstr_duplicate(machine->addr_tuple->client->address);
       
       msg->wdp_datagram.destination_port = machine->addr_tuple->client->port;
}

/* 
 * Now we have the direct reply address.
 */
static void add_direct_address(Msg *msg, WAPAddrTuple *address){

       debug("wap.wtp.send", 0, "WTP: adding direct address");
       wtp_send_address_dump(address);
       msg->wdp_datagram.source_address = 
    	    octstr_duplicate(address->client->address);
       msg->wdp_datagram.source_port = address->client->port;
       msg->wdp_datagram.destination_address = 
    	    octstr_duplicate(address->server->address);
       msg->wdp_datagram.destination_port = address->server->port;
}

static void add_segment_address(Msg *msg, Address *address){

       gw_assert(msg != NULL);
}

/*
 * Turns on retransmission indicator flag (are we resending or not) of an 
 * entire message.
 */
static void set_rid(Msg *msg, long rid){

       octstr_set_bits(msg->wdp_datagram.user_data, 7, 1, rid);
}

/*
 * Returns retransmission indicator of an entire message.
 */
static long message_rid(Msg *msg){

       return octstr_get_bits(msg->wdp_datagram.user_data, 7, 1);
}

static unsigned short send_tid(WTPMachine *machine){

       return machine->tid ^ 0x8000;
}

/****************************************************************************/
