/*                          
 * wtp_send.c - WTP message sending module implementation
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp_send.h"
#include "msg.h"
#include "wapbox.h"

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

static Msg *pack_result(Msg *msg, WTPMachine *machine, WTPEvent *event);

static Msg *pack_abort(Msg *msg, long abort_type, long abort_reason, 
       WTPEvent *event);

static Msg *pack_stop(Msg *msg, long abort_type, long abort_reason, long tid);

static Msg *pack_ack(Msg *msg, long ack_type, WTPMachine *machine, 
                     WTPEvent *event);

static Msg *pack_negative_ack(Msg *msg, long tid, int retransmission_status, 
       int segments_missing, WTPSegment *missing_segments);

static Msg *pack_group_ack(Msg *msg, long tid, int retransmission_status, 
                          char packet_sequence_number);

static Msg *add_datagram_address(Msg *msg, WTPMachine *machine);

static Msg *add_segment_address(Msg *msg, Address *address);

static Msg *add_direct_address(Msg *msg, Address *address);

static char insert_pdu_type(int type, char octet);

static char indicate_simple_message(char octet);

static char insert_rid(long attribute, char octet);

static void insert_tid(char *pdu, long attribute);

static Msg *set_rid(Msg *msg, long rid);

static long message_rid(Msg *msg);

static char insert_abort_type(int abort_type, char octet);

static char indicate_ack_type(char ack_type, char octet);

static void insert_missing_segments_list(char *wtp_pdu, 
                                         WTPSegment *missing_segments);

static char indicate_variable_header(char octet);

static char insert_tpi_type(int pdu_type, char octet);

static char insert_tpi_length(int tpi_length, char octet);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 * Sends a message object, of wdp datagram type, having result PDU as user 
 * data. Fetches SDU from WTP event, address four-tuple and machine state 
 * information (are we resending the packet or not) from WTP machine. Handles 
 * all errors by itself.
 *
 * Returns message, if succesfull, NULL otherwise. 
 */
Msg *wtp_send_result(WTPMachine *machine, WTPEvent *event){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_datagram_address(msg, machine);

     debug("wap.wtp", 0, "WTP: packing result pdu");

     msg = pack_result(msg, machine, event);

     debug("wap.wtp", 0,"WTP: result pdu packed");
  
     if (msg == NULL){
        return NULL;
     }

     put_msg_in_queue(msg);

     return msg;
}

/*
 * Resend an already packed packet. We must turn on rid bit first (if it is 
 * not already turned).
 */
void wtp_resend_result(Msg *result, long rid){

     if (message_rid(result) == 0) {
        debug("wap.wtp", 0, "WTP: resend: turning the first bit");
        result = set_rid(result, rid);
     }
     put_msg_in_queue(result);
}

/*
 * Sends a message object, of wdp datagram type, having abort header as user 
 * data. Fetches address four-tuple from WTP machine, tid from wtp event, abort 
 * type and reason from direct input. Handles all errors by itself.
 */
void wtp_send_abort(long abort_type, long abort_reason, WTPMachine *machine, 
     WTPEvent *event){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_datagram_address(msg, machine);
     msg = pack_abort(msg, abort_type, abort_reason, event);

     if (msg == NULL){
        return;
     }

     put_msg_in_queue(msg);

     return;
}

/*
 * Same as previous, expect now abort type and reason, reply address and trans-
 * action tid are direct inputs. (This function is used when the transaction is 
 * aborted before calling the state machine).
 */
void wtp_do_not_start(long abort_type, long abort_reason, Address *address, long tid){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_direct_address(msg, address);
     debug("wap.wtp", 0, "WTP: do_not_start: address added");
     msg = pack_stop(msg, abort_type, abort_reason, tid);

     if (msg == NULL){
        return;
     }

     put_msg_in_queue(msg);
     debug("wap.wtp", 0, "WTP: do_not_start: aborted");

     return;
}

void wtp_send_ack(long ack_type, WTPMachine *machine, WTPEvent *event){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_datagram_address(msg, machine);
     msg = pack_ack(msg, ack_type, machine, event);

     if (msg == NULL){
        return;
     }

     put_msg_in_queue(msg); 

     return;
}

void wtp_send_group_ack(Address *address, long tid, int retransmission_status, 
                        char packet_sequence_number){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_segment_address(msg, address);
     msg = pack_group_ack(msg, tid, retransmission_status, 
                          packet_sequence_number);

     if (msg == NULL){
        return;
     }

     put_msg_in_queue(msg); 

     return;
}

void wtp_send_negative_ack(Address *address, long tid, int retransmission_status, 
                           int segments_missing, WTPSegment *missing_segments){
     
     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_segment_address(msg, address);
     msg = pack_negative_ack(msg, tid, retransmission_status, segments_missing,
                          missing_segments);

     if (msg == NULL){
        return;
     }

     put_msg_in_queue(msg); 

     return;
}

void wtp_send_address_dump(Address *address){

       debug("wap.wtp", 0, "WTP: address dump starting");
       debug("wap.wtp", 0, "WTP: source address");
       octstr_dump(address->source_address);
       debug("wap.wtp", 0, "WTP: source port %ld: ", address->source_port);
       debug("wap.wtp", 0, "WTP: destination address");
       octstr_dump(address->destination_address);
       debug("wap.wtp", 0, "WTP: destination port %ld: ", address->destination_port);
}

/****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 * Packs a message object, of wdp datagram type, having result PDU as user 
 * data. Fetches SDU from WTP event, machine state information (are we 
 * resending the packet) from WTP machine. Handles all errors by itself.
 */

static Msg *pack_result(Msg *msg, WTPMachine *machine, WTPEvent *event){

    int octet;
    size_t pdu_len;
    char *wtp_pdu = NULL; 
    
    octet = -42;

    pdu_len = 4;
    wtp_pdu = gw_malloc(pdu_len);
    
/*
 * We try to send fixed length result PDU, without segmentation. Only inputs 
 * are the rid field (which tells are we resending or not), and the tid.
 */  
 
    msg->wdp_datagram.user_data = octstr_duplicate(event->TRResult.user_data);
    
    octet = insert_pdu_type(RESULT, octet);
    octet = indicate_simple_message(octet);
    octet = insert_rid(machine->rid, octet);
    wtp_pdu[0] = octet;
    debug("wap.wtp", 0, "WTP: inserting tid");
    insert_tid(wtp_pdu, event->TRResult.tid);
    octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 3);
    debug("wap.wtp", 0,"WTP: sending a result message");
    msg_dump(msg);

    return msg;

}

/*
 * Packs a message object, of wdp datagram type, consisting of Abort PDU header.
 * Fetches abort type and reason from direct input, tid from WTP event. Handles all 
 * errors by itself.
 */
static Msg *pack_abort(Msg *msg, long abort_type, long abort_reason, 
       WTPEvent *event){

       int octet;
       size_t pdu_len;
       char *wtp_pdu;

       pdu_len = 4;
       wtp_pdu = gw_malloc(pdu_len);
       octet = -42;

       msg->wdp_datagram.user_data = octstr_create_empty();

       octet = insert_pdu_type(ABORT, octet);
       octet = insert_abort_type(abort_type, octet);
       wtp_pdu[0] = octet;

       insert_tid(wtp_pdu, event->TRAbort.tid);

       wtp_pdu[3] = abort_reason;

       octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 4);

       return msg;
}

/*
 * As previous, expect now tid is supplied as a direct input
 */
static Msg *pack_stop(Msg *msg, long abort_type, long abort_reason, long tid){

       int octet;
       size_t pdu_len;
       char *wtp_pdu;

       pdu_len = 4;
       wtp_pdu = gw_malloc(pdu_len);
       octet = -42;

       msg->wdp_datagram.user_data = octstr_create_empty();

       octet = insert_pdu_type(ABORT, octet);
       octet = insert_abort_type(abort_type, octet);
       wtp_pdu[0] = octet;

       insert_tid(wtp_pdu, tid);

       wtp_pdu[3] = abort_reason;

       octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 4);

       return msg;
}

static Msg *pack_ack(Msg *msg, long ack_type, WTPMachine *machine, 
                     WTPEvent *event){

    int octet;
    size_t pdu_len;
    char *wtp_pdu;

    pdu_len = 3;
    wtp_pdu = gw_malloc(pdu_len);
    octet = -42;
/*
 * Ack PDU is generated solely by WTP. Inputs are rid, ack type (are we doing tid 
 * verification or not) and tid.
 */
    octet = insert_pdu_type(ACK, octet);
    octet = indicate_ack_type(ack_type, octet);
    octet = insert_rid(machine->ack_pdu_sent, octet);
    wtp_pdu[0] = octet;

    insert_tid(wtp_pdu, machine->tid);

    msg->wdp_datagram.user_data = octstr_create_empty();
    octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 3);

    return msg;
}

static Msg *pack_negative_ack(Msg *msg, long tid, int retransmission_status, 
       int segments_missing, WTPSegment *missing_segments){

       int octet;
       size_t pdu_len;
       char *wtp_pdu;
/*
 * Negative ack PDU includes a list of all missing segments. Other inputs are rid
 * and tid.
 */
       pdu_len = segments_missing + 4;
       wtp_pdu = gw_malloc(pdu_len);
       octet = -42;

       octet = insert_pdu_type(NEGATIVE_ACK, octet);
       octet = insert_rid(retransmission_status, octet);
       wtp_pdu[0] = octet;

       insert_tid(wtp_pdu, tid);

       wtp_pdu[4] = segments_missing;

       insert_missing_segments_list(wtp_pdu, missing_segments);

       msg->wdp_datagram.user_data = octstr_create_empty();
       octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 
                          segments_missing + 4);

       return msg;
}

static Msg *pack_group_ack(Msg *msg, long tid, int retransmission_status, 
                          char packet_sequence_number){
       
       char octet;
       size_t pdu_len,
              tpi_length;
       char *wtp_pdu;
/*
 * Group acknowledgement PDU has a packet sequence tpi added in it. Its lenght 
 * is two octets. Tpi length is one, because this number excludes first octet 
 * (the header of aheader). 
 */
       pdu_len = 5;
       wtp_pdu = gw_malloc(pdu_len);
       tpi_length = 1;
       octet = -42;

       octet = indicate_variable_header(octet);
       octet = insert_pdu_type(ACK, octet);
       octet = indicate_ack_type(ACKNOWLEDGEMENT, octet);
       octet = insert_rid(retransmission_status, octet);
       wtp_pdu[0] = octet;

       insert_tid(wtp_pdu, tid);

       octet = -42;
       octet = insert_tpi_type(PACKET_SEQUENCE_NUMBER, octet);
       octet = insert_tpi_length(tpi_length, octet);
       wtp_pdu[4] = octet;

       wtp_pdu[5] = packet_sequence_number;

       msg->wdp_datagram.user_data = octstr_create_empty();
       octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 5);

       return msg;
}

/* 
 * We must swap the source and the destination, because we are answering a query.
 */
static Msg *add_datagram_address(Msg *msg, WTPMachine *machine){

       msg->wdp_datagram.source_address = 
    	    octstr_duplicate(machine->destination_address);

       msg->wdp_datagram.source_port = machine->destination_port;

       msg->wdp_datagram.destination_address = 
    	    octstr_duplicate(machine->source_address);
       
       msg->wdp_datagram.destination_port = machine->source_port;

       return msg;
}

/* 
 * Now we have the direct reply address.
 */
static Msg *add_direct_address(Msg *msg, Address *address){

       debug("wap.wtp", 0, "WTP: add_direct_address");
       wtp_send_address_dump(address);
       msg->wdp_datagram.source_address = 
    	    octstr_duplicate(address->source_address);
       msg->wdp_datagram.source_port = address->source_port;
       msg->wdp_datagram.destination_address = 
    	    octstr_duplicate(address->destination_address);
       msg->wdp_datagram.destination_port = address->destination_port;
       return msg;
}

static Msg *add_segment_address(Msg *msg, Address *address){

       return msg;
}

static char insert_pdu_type (int type, char octet) {

       octet = type;
       octet <<= 3;

       return octet;
}

static char indicate_simple_message(char octet){
    
       octet |= 0x6;
       return octet;
}

/*
 * Turns on rid bit in the middle of a message.
 */
static Msg *set_rid(Msg *msg, long rid){

       char first_octet;
    
       first_octet = octstr_get_char(msg->wdp_datagram.user_data, 0);
       first_octet = insert_rid(rid, first_octet);
       octstr_set_char(msg->wdp_datagram.user_data, 0, first_octet); 
       
       return msg;
}

static char insert_rid(long attribute, char octet){

       octet += attribute;
       return octet;
}

/*
 * Returns rid of an entire message.
 */
static long message_rid(Msg *msg){

       char first_octet;
       long rid = 0;

       first_octet = octstr_get_char(msg->wdp_datagram.user_data, 0);
       rid = first_octet&1;

       return rid;
}

static void insert_tid(char *pdu, long attribute){

       int tid;
       char first_tid,
            last_tid;

       tid = attribute^0x8000;
       first_tid = tid>>8;
       last_tid = attribute&0xff;
       pdu[1] = first_tid;
       pdu[2] = last_tid;
}

static char insert_abort_type(int abort_type, char octet){
       
       return octet + abort_type;
}

static char indicate_ack_type(char ack_type, char octet){

       char tid_ve_octet;

       tid_ve_octet = ack_type&1;
       tid_ve_octet <<= 2;
       octet += tid_ve_octet;

       return octet;
}

static void insert_missing_segments_list(char *wtp_pdu, 
       WTPSegment *missing_segments){

}

static char indicate_variable_header(char octet){

       char this_octet;

       this_octet = 1;

       return octet + (this_octet<<7);
}

static char insert_tpi_type(int type, char octet){

       octet = type;
       octet <<= 3;

       return octet;
}

static char insert_tpi_length(int length, char octet){

       octet = length;
       octet &= 3;

       return octet;
}

/****************************************************************************/










