/*                          
 * wtp_send.c - WTP message sending module implementation
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp_send.h"
#include "msg.h"
#include "wapbox.h"

/*****************************************************************************
 *
 * Prototypes of internal functions
 */

static Msg *pack_result(Msg *msg, WTPMachine *machine, WTPEvent *event);

static Msg *pack_abort(Msg *msg, long abort_type, long abort_reason, 
       WTPMachine *machine, WTPEvent *event);

static Msg *pack_ack(Msg *msg, long ack_type, WTPMachine *machine, 
                     WTPEvent *event);

static Msg *pack_negative_ack(Msg *msg, long tid, int retransmission_status, 
       int segments_missing, int *missing_segments);

static Msg *pack_group_ack(Msg *msg, long tid, int retransmission_status, 
                          char packet_sequence_number);

static Msg *add_datagram_address(Msg *msg, WTPMachine *machine);

static Msg *add_segment_address(Msg *msg, Address *address);

static char insert_pdu_type(int type, char octet);

static char indicate_simple_message(char octet);

static char insert_rid(long attribute, char octet);

static void insert_tid(char *pdu, long attribute);

static char insert_abort_type(int abort_type, char octet);

static char indicate_ack_type(char ack_type, char octet);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 * Sends a message object, of wdp datagram type, having result PDU as user 
 * data. Fetches SDU from WTP event, address four-tuple and machine state 
 * information (are we resending the packet or not) from WTP machine. Handles 
 * all errors by itself.
 */
void wtp_send_result(WTPMachine *machine, WTPEvent *event){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_datagram_address(msg, machine);
#ifdef debug
     debug(0, "WTP: packing result pdu");
#endif
     msg = pack_result(msg, machine, event);
#ifdef debug
     debug(0,"WTP: result pdu packed");
#endif   
     if (msg == NULL){
        return;
     }

     put_msg_in_queue(msg);

     return;
}

void wtp_send_abort(long abort_type, long abort_reason, WTPMachine *machine, 
     WTPEvent *event){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_datagram_address(msg, machine);
     msg = pack_abort(msg, abort_type, abort_reason, machine, event);

     if (msg == NULL){
        return;
     }

     put_msg_in_queue(msg);

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
                           int segments_missing, int *missing_segments){
     
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
#ifdef debug
    debug(0, "WTP: inserting tid");
#endif
    insert_tid(wtp_pdu, event->TRResult.tid);
    octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 3);
#ifdef debug
    debug(0,"WTP: sending a result message");
    msg_dump(msg);
#endif

    return msg;

}

static Msg *pack_abort(Msg *msg, long abort_type, long abort_reason, 
       WTPMachine *machine, WTPEvent *event){

       int octet;
       size_t pdu_len;
       char *wtp_pdu;

       pdu_len = 4;
       wtp_pdu = gw_malloc(pdu_len);
       octet = -42;
/*
 * User data includes, when we are speaking of abort PDU, a WSP PDU (Disconnect 
 * PDU). Inputs are abort type, abort reason and tid.
 */  
 
       msg->wdp_datagram.user_data = octstr_duplicate(event->TRAbort.user_data);
       octet = insert_pdu_type(ABORT, octet);
       octet = insert_abort_type(abort_type, octet);
       wtp_pdu[0] = octet;

       insert_tid(wtp_pdu, event->TRAbort.tid);

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
       int segments_missing, int *missing_segments){

       return msg;
}

static Msg *pack_group_ack(Msg *msg, long tid, int retransmission_status, 
                          char packet_sequence_number){

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

static char insert_rid(long attribute, char octet){

       octet += attribute;
       return octet;
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

/****************************************************************************/










