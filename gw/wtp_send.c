/*                          
 * wtp_send.c - WTP message sending module implementation
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "gwlib/gwlib.h"
#include "wtp_send.h"


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

static void add_datagram_address(Msg *msg, WAPAddrTuple *address);
static void add_initiator_address(Msg *msg, WTPRespMachine *init_machine);
static void add_responder_address(Msg *msg, WTPInitMachine *resp_machine);
static void add_segment_address(Msg *msg, WAPAddrTuple *address);

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
static unsigned short send_tid(unsigned short tid);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 * Sends a message object, having type wdp_datagram, having invoke PDU as user
 * data. Fetches address, tid and tid_new from the initiator state machine, 
 * other fields from event. Only for the wtp initiator.
 *
 * Return message to be sended
 */

Msg *wtp_send_invoke(WTPInitMachine *machine, WAPEvent *event){
    Msg *msg = NULL,
        *dup = NULL;
    WTP_PDU *pdu = NULL;

    gw_assert(event->type == TR_Invoke_Req);
    pdu = wtp_pdu_create(Invoke);
    pdu->u.Invoke.con = 0;
    pdu->u.Invoke.gtr = 1;
    pdu->u.Invoke.ttr = 1;
    pdu->u.Invoke.rid = 0;
    pdu->u.Invoke.version = 0;
/* 
 * Now SendTID = GenTID (See WTP 10.5)
 */
    pdu->u.Invoke.tid = (unsigned short) machine->tid;
    pdu->u.Invoke.tidnew = machine->tidnew;
    pdu->u.Invoke.user_data = octstr_duplicate(
                              event->u.TR_Invoke_Req.user_data);
    pdu->u.Invoke.class = event->u.TR_Invoke_Req.tcl;
    pdu->u.Invoke.uack = event->u.TR_Invoke_Req.up_flag;

    msg = msg_create(wdp_datagram);
    add_responder_address(msg, machine);
    msg->wdp_datagram.user_data = wtp_pdu_pack(pdu);
    wtp_pdu_destroy(pdu);

    dup = msg_duplicate(msg);
    put_msg_in_queue(msg);

    return dup;
}
 
/* Sends a message object, of wdp datagram type, having result PDU as user 
 * data. Fetches SDU from WTP event ,address four-tuple from WTP machine. 
 * Handles all errors by itself. Only for wtp responder.
 *
 * Returns message to be sended. 
 */
Msg *wtp_send_result(WTPRespMachine *machine, WAPEvent *event){

     Msg *msg, *dup;
     WTP_PDU *pdu;
     
     gw_assert(event->type == TR_Result_Req);
     pdu = wtp_pdu_create(Result);
     pdu->u.Result.con = 0;
     pdu->u.Result.gtr = 1;
     pdu->u.Result.ttr = 1;
     pdu->u.Result.rid = 0;
     pdu->u.Result.tid = send_tid(machine->tid);
     pdu->u.Result.user_data = 
     	  octstr_duplicate(event->u.TR_Result_Req.user_data);

     msg = msg_create(wdp_datagram);
     add_initiator_address(msg, machine);
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
void wtp_resend(Msg *msg, long rid){

     if (message_rid(msg) == 0) {
        set_rid(msg, rid);
     }

     put_msg_in_queue(msg_duplicate(msg));
}

/*
 * Sends a message object, of wdp datagram type, having abort header as user 
 * data. Inputs are address four-tuple, tid , abort type and reason from 
 * direct input. Handles all errors by itself. Both wtp initiator and responder 
 * use this function.
 */

void wtp_send_abort(long abort_type, long abort_reason, long tid, 
     WAPAddrTuple *address) {
     Msg *msg = NULL;
     WTP_PDU *pdu;

     pdu = wtp_pdu_create(Abort);
     pdu->u.Abort.con = 0;
     pdu->u.Abort.abort_type = abort_type;
     pdu->u.Abort.tid = send_tid(tid);
     pdu->u.Abort.abort_reason = abort_reason;

     msg = msg_create(wdp_datagram);
     add_datagram_address(msg, address);
     msg->wdp_datagram.user_data = wtp_pdu_pack(pdu);
     
     wtp_pdu_destroy(pdu);

     put_msg_in_queue(msg);

     return;
}

/*
 * Send a message object, of wdp datagram type, having ack PDU as user 
 * data. Creates SDU by itself, fetches address four-tuple and machine state
 * from WTP machine. Ack_type is a flag telling whether we are doing tid 
 * verification or not, rid_flag tells are we retransmitting. Handles all 
 * errors by itself. Both wtp initiator and responder use this function. So this
 * function does not set SendTID; the caller must do this instead.
 */
void wtp_send_ack(long ack_type, int rid_flag, long tid, 
                  WAPAddrTuple *address){
     Msg *msg = NULL;
     WTP_PDU *pdu;
     
     pdu = wtp_pdu_create(Ack);
     pdu->u.Ack.con = 0;
     pdu->u.Ack.tidverify = ack_type;
     pdu->u.Ack.rid = rid_flag;
     pdu->u.Ack.tid = send_tid(tid);

     msg = msg_create(wdp_datagram);
     add_datagram_address(msg, address);
     msg->wdp_datagram.user_data = wtp_pdu_pack(pdu);
     
     wtp_pdu_destroy(pdu);

     put_msg_in_queue(msg);
     
     return;
}

void wtp_send_group_ack(WAPAddrTuple *address, int tid, 
                        int retransmission_status, 
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

void wtp_send_negative_ack(WAPAddrTuple *address, int tid, 
                           int retransmission_status,
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
     /* XXX: Convert missing_segments to an octstr of packet sequence 
     numbers */
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
 *
 * Functions for determining the datagram address. If duplication is bad, void *
 * is worse. We must swap the source and the destination, because we are answer-
 * ing a query.
 */
static void add_datagram_address(Msg *msg, WAPAddrTuple *address){

       msg->wdp_datagram.source_address = octstr_duplicate(
            address->server->address);

       msg->wdp_datagram.source_port = address->server->port;

       msg->wdp_datagram.destination_address = 
    	    octstr_duplicate(address->client->address);
       
       msg->wdp_datagram.destination_port = address->client->port;
}

/* 
 * And initiator address form responder state machine.
 */
static void add_initiator_address(Msg *msg, WTPRespMachine *resp_machine){

       debug("wap.wtp_send", 0, "WTP_SEND: add_initiator_address");
       msg->wdp_datagram.source_address = 
    	    octstr_duplicate(resp_machine->addr_tuple->server->address);
       msg->wdp_datagram.source_port = resp_machine->addr_tuple->server->port;
       msg->wdp_datagram.destination_address = 
    	    octstr_duplicate(resp_machine->addr_tuple->client->address);
       msg->wdp_datagram.destination_port = 
           resp_machine->addr_tuple->client->port;
}

/* 
 * And responder address from initiator  state machine.
 */
static void add_responder_address(Msg *msg, WTPInitMachine *init_machine){

       debug("wap.wtp.send", 0, "WTP_SEND: adding direct address");
       msg->wdp_datagram.source_address = 
    	    octstr_duplicate(init_machine->addr_tuple->server->address);
       msg->wdp_datagram.source_port = init_machine->addr_tuple->server->port;
       msg->wdp_datagram.destination_address = 
    	    octstr_duplicate(init_machine->addr_tuple->client->address);
       msg->wdp_datagram.destination_port = 
            init_machine->addr_tuple->client->port;
}

static void add_segment_address(Msg *msg, WAPAddrTuple *address){

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

static unsigned short send_tid(unsigned short tid){

       return tid ^ 0x8000;
}

/****************************************************************************/
