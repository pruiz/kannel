/*                          
 * wtp_send.c - WTP message sending module implementation
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

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

static Msg *pack_result(Msg *msg, WTPMachine *machine, WTPEvent *event);

static Msg *pack_abort(Msg *msg, long abort_type, long abort_reason, 
       WTPEvent *event);

static Msg *pack_stop(Msg *msg, long abort_type, long abort_reason, int tid);

static Msg *pack_ack(Msg *msg, long ack_type, WTPMachine *machine, 
                     WTPEvent *event);

static Msg *pack_negative_ack(Msg *msg, int tid, int retransmission_status, 
       int segments_missing, WTPSegment *missing_segments);

static Msg *pack_group_ack(Msg *msg, int tid, int retransmission_status, 
                          unsigned char packet_sequence_number);

static Msg *add_datagram_address(Msg *msg, WTPMachine *machine);

static Msg *add_segment_address(Msg *msg, Address *address);

static Msg *add_direct_address(Msg *msg, Address *address);

static unsigned char insert_pdu_type(int type, unsigned char octet);

static unsigned char indicate_simple_message(unsigned char octet);

/*
 * Setting resending status of this octet (are we trying again)
 */
static unsigned char insert_rid(int attribute, unsigned char octet);

/*
 * Inserting transaction identifier into the message
 */
static void insert_tid(unsigned char *pdu, int attribute);

/*
 * Setting retransmission status of a already packed message.
 */
static Msg *set_rid(Msg *msg, long rid);

/*
 * Returns retransmission status of the entire message
 */ 
static long message_rid(Msg *msg);

/*
 * Inserting the type of an abort (by provider or by user) into an octet
 */ 
static unsigned char insert_abort_type(int abort_type, unsigned char octet);

/*
 * Inserting ack type (a flag are we doing tid verification or normal 
 * acknowledgement into this message.
 */
static unsigned char indicate_ack_type(unsigned char ack_type, 
                     unsigned char octet);

static void insert_missing_segments_list(unsigned char *wtp_pdu, 
                    WTPSegment *missing_segments);

static unsigned char indicate_variable_header(unsigned char octet);

/*
 * Insert the type of transaction information item (data included into the 
 * header) into this octet
 */
static unsigned char insert_tpi_type(int pdu_type, unsigned char octet);

/*
 * Insert the length of the transaction information item into this octet
 */
static unsigned char insert_tpi_length(int tpi_length, unsigned char octet);

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
Msg *wtp_send_result(WTPMachine *machine, WTPEvent *event){

     Msg *msg, *dup;

     msg = msg_create(wdp_datagram);
     msg = add_datagram_address(msg, machine);
     msg = pack_result(msg, machine, event);
  
     if (msg == NULL){
        return NULL;
     }

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
        result = set_rid(result, rid);
     }

     put_msg_in_queue(msg_duplicate(result));
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

     put_msg_in_queue(msg);

     return;
}

/*
 * Same as previous, expect now abort type and reason, reply address and trans-
 * action tid are direct inputs. (This function is used when the transaction is 
 * aborted before calling the state machine).
 */
void wtp_do_not_start(long abort_type, long abort_reason, Address *address, int tid){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_direct_address(msg, address);
     debug("wap.wtp", 0, "WTP: do_not_start: address added");
     msg = pack_stop(msg, abort_type, abort_reason, tid);

     put_msg_in_queue(msg);
     debug("wap.wtp.send", 0, "WTP: do_not_start: aborted");

     return;
}

void wtp_send_ack(long ack_type, WTPMachine *machine, WTPEvent *event){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_datagram_address(msg, machine);
     msg = pack_ack(msg, ack_type, machine, event);

     put_msg_in_queue(msg);
     debug("wap.wtp.send", 0, "WTP_SEND: message put into the queue");  

     return;
}

void wtp_send_group_ack(Address *address, int tid, int retransmission_status, 
                        unsigned char packet_sequence_number){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_segment_address(msg, address);
     msg = pack_group_ack(msg, tid, retransmission_status, 
                          packet_sequence_number);

     put_msg_in_queue(msg); 

     return;
}

void wtp_send_negative_ack(Address *address, int tid, int retransmission_status, 
                           int segments_missing, WTPSegment *missing_segments){
     
     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     msg = add_segment_address(msg, address);
     msg = pack_negative_ack(msg, tid, retransmission_status, segments_missing,
                          missing_segments);

     put_msg_in_queue(msg); 

     return;
}

void wtp_send_address_dump(Address *address){

       debug("wap.wtp.send", 0, "WTP: address dump starting");
       debug("wap.wtp.send", 0, "WTP: source address");
       octstr_dump(address->source_address, 1);
       debug("wap.wtp.send", 0, "WTP: source port %ld: ", address->source_port);
       debug("wap.wtp.send", 0, "WTP: destination address");
       octstr_dump(address->destination_address, 1);
       debug("wap.wtp.send", 0, "WTP: destination port %ld: ", address->destination_port);
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

    unsigned char octet;
    size_t pdu_len;
    unsigned char *wtp_pdu = NULL;

/* 
 * We turn on flags telling that we are not supporting segmentation and 
 * reassembly  
 */                           
    octet = 6;                
    pdu_len = 3;
    wtp_pdu = gw_malloc(pdu_len);
    
/*
 * We try to send fixed length result PDU, without segmentation. Only inputs 
 * are the rid field (which tells are we resending or not), and the tid.
 */  
 
    octstr_destroy(msg->wdp_datagram.user_data);
    msg->wdp_datagram.user_data = 
         octstr_duplicate(event->TR_Result_Req.user_data);
    
    octet = insert_pdu_type(RESULT, octet);
    octet = indicate_simple_message(octet);
    octet = insert_rid(machine->rid, octet);
    wtp_pdu[first_byte] = octet;

    insert_tid(wtp_pdu, event->TR_Result_Req.tid);

    octstr_insert_data(msg->wdp_datagram.user_data, first_byte, wtp_pdu, 3);
    
    gw_free(wtp_pdu);
    return msg;

}

/*
 * Packs a message object, of wdp datagram type, consisting of Abort PDU header.
 * Fetches abort type and reason from direct input, tid from WTP event. Handles all 
 * errors by itself.
 */
static Msg *pack_abort(Msg *msg, long abort_type, long abort_reason, 
       WTPEvent *event){

       unsigned char octet;
       size_t pdu_len;
       unsigned char *wtp_pdu = NULL;

       pdu_len = 4;
       wtp_pdu = gw_malloc(pdu_len);
       octet = 0;

       octstr_destroy(msg->wdp_datagram.user_data);
       msg->wdp_datagram.user_data = octstr_create_empty();

       octet = insert_pdu_type(ABORT, octet);
       octet = insert_abort_type(abort_type, octet);
       wtp_pdu[first_byte] = octet;

       insert_tid(wtp_pdu, event->TR_Abort_Req.tid);

       wtp_pdu[fourth_byte] = abort_reason;

       octstr_insert_data(msg->wdp_datagram.user_data, first_byte, wtp_pdu, 4);

       gw_free(wtp_pdu);
       return msg;
}

/*
 * As previous, expect now tid is supplied as a direct input
 */
static Msg *pack_stop(Msg *msg, long abort_type, long abort_reason, int tid){

       unsigned char octet;
       size_t pdu_len;
       unsigned char *wtp_pdu = NULL;

       pdu_len = 4;
       wtp_pdu = gw_malloc(pdu_len);
       octet = 0;

       octstr_destroy(msg->wdp_datagram.user_data);
       msg->wdp_datagram.user_data = octstr_create_empty();

       octet = insert_pdu_type(ABORT, octet);
       octet = insert_abort_type(abort_type, octet);
       wtp_pdu[first_byte] = octet;

       insert_tid(wtp_pdu, tid);

       wtp_pdu[fourth_byte] = abort_reason;

       octstr_insert_data(msg->wdp_datagram.user_data, first_byte, wtp_pdu, 4);

       gw_free(wtp_pdu);
       return msg;
}

static Msg *pack_ack(Msg *msg, long ack_type, WTPMachine *machine, 
                     WTPEvent *event){

    unsigned char octet;
    size_t pdu_len;
    unsigned char *wtp_pdu = NULL;

    pdu_len = 3;
    wtp_pdu = gw_malloc(pdu_len);
    octet = 0;
/*
 * Ack PDU is generated solely by WTP. Inputs are rid (are we sending the packet or  * not), ack type (are we doing tid verification or not) and tid.
 */
    octet = insert_pdu_type(ACK, octet);
    octet = indicate_ack_type(ack_type, octet);
    octet = insert_rid(machine->rid, octet);
    wtp_pdu[first_byte] = octet;

    insert_tid(wtp_pdu, machine->tid);

    octstr_destroy(msg->wdp_datagram.user_data);
    msg->wdp_datagram.user_data = octstr_create_empty();
    octstr_insert_data(msg->wdp_datagram.user_data, first_byte, wtp_pdu, fourth_byte);

    gw_free(wtp_pdu);

    return msg;
}

static Msg *pack_negative_ack(Msg *msg, int tid, int retransmission_status, 
       int segments_missing, WTPSegment *missing_segments){

       unsigned char octet;
       size_t pdu_len;
       unsigned char *wtp_pdu = NULL;
/*
 * Negative ack PDU includes a list of all missing segments. Other inputs are rid
 * and tid.
 */
       pdu_len = segments_missing + 4;
       wtp_pdu = gw_malloc(pdu_len);
       octet = 0;

       octet = insert_pdu_type(NEGATIVE_ACK, octet);
       octet = insert_rid(retransmission_status, octet);
       wtp_pdu[first_byte] = octet;

       insert_tid(wtp_pdu, tid);

       wtp_pdu[4] = segments_missing;

       insert_missing_segments_list(wtp_pdu, missing_segments);

       octstr_destroy(msg->wdp_datagram.user_data);
       msg->wdp_datagram.user_data = octstr_create_empty();
       octstr_insert_data(msg->wdp_datagram.user_data, first_byte, wtp_pdu, 
                          segments_missing + 4);

       gw_free(wtp_pdu);
       return msg;
}

static Msg *pack_group_ack(Msg *msg, int tid, int retransmission_status, 
                          unsigned char packet_sequence_number){
       
       unsigned char octet;
       size_t pdu_len,
              tpi_length;
       unsigned char *wtp_pdu = NULL;
/*
 * Group acknowledgement PDU has a packet sequence tpi added in it. Its lenght 
 * is two octets. Tpi length is one, because this number excludes first octet 
 * (the header of aheader). 
 */
       pdu_len = 5;
       wtp_pdu = gw_malloc(pdu_len);
       tpi_length = 1;
       octet = 0;

       octet = indicate_variable_header(octet);
       octet = insert_pdu_type(ACK, octet);
       octet = indicate_ack_type(ACKNOWLEDGEMENT, octet);
       octet = insert_rid(retransmission_status, octet);
       wtp_pdu[first_byte] = octet;

       insert_tid(wtp_pdu, tid);

       octet = -42;
       octet = insert_tpi_type(PACKET_SEQUENCE_NUMBER, octet);
       octet = insert_tpi_length(tpi_length, octet);
       wtp_pdu[4] = octet;

       wtp_pdu[5] = packet_sequence_number;

       octstr_destroy(msg->wdp_datagram.user_data);
       msg->wdp_datagram.user_data = octstr_create_empty();
       octstr_insert_data(msg->wdp_datagram.user_data, first_byte, wtp_pdu, 5);

       gw_free(wtp_pdu);
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

       debug("wap.wtp.send", 0, "WTP: add_direct_address");
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

       gw_assert(msg != NULL);
       return msg;
}

static unsigned char insert_pdu_type (int type, unsigned char octet) {

       type <<= 3;
       octet |= type;

       return octet;
}

static unsigned char indicate_simple_message(unsigned char octet){
    
       octet |= 0x6;
       return octet;
}

/*
 * Turns on retransmission indicator flag (are we resending or not) of an 
 * entire message.
 */
static Msg *set_rid(Msg *msg, long rid){

       unsigned char first_octet;
    
       first_octet = octstr_get_char(msg->wdp_datagram.user_data, first_byte);
       first_octet = insert_rid(rid, first_octet);
       octstr_set_char(msg->wdp_datagram.user_data, first_byte, first_octet); 
       
       return msg;
}

/*
 * Turns on retransmission indicator flag (rid) of an octet
 */
static unsigned char insert_rid(int attribute, unsigned char octet){

       octet |= attribute;
       return octet;
}

/*
 * Returns retransmission indicator of an entire message.
 */
static long message_rid(Msg *msg){

       unsigned char first_octet;
       long rid = 0;

       first_octet = octstr_get_char(msg->wdp_datagram.user_data, first_byte);
       rid = first_octet&1;

       return rid;
}

/*
 * Insert transaction identifier octets of the header (second and third octets)
 */
static void insert_tid(unsigned char *pdu, int attribute){

       int tid;
       unsigned char first_tid,
            last_tid;

       tid = attribute^0x8000;
       first_tid = tid>>8;
       last_tid = attribute&0xff;
       pdu[second_byte] = first_tid;
       pdu[third_byte] = last_tid;
}

/*
 * Insert type of the abort (by user or by provider) into an octet
 */
static unsigned char insert_abort_type(int abort_type, unsigned char octet){
       
       octet |= abort_type;
       return octet;
}

/*
 * Set flag telling whether we are doing tid verification or normal acknow-
 * ledgement into an octet
 */
static unsigned char indicate_ack_type(unsigned char ack_type, 
                     unsigned char octet){

       unsigned char tid_ve_octet;

       tid_ve_octet = ack_type&1;
       tid_ve_octet <<= 2;
       octet |= tid_ve_octet;

       return octet;
}

static void insert_missing_segments_list(unsigned char *wtp_pdu, 
       WTPSegment *missing_segments){
    
       gw_assert(missing_segments != NULL);
       gw_assert(wtp_pdu != NULL);
}

static unsigned char indicate_variable_header(unsigned char octet){

       octet |= 128;

       return octet;
}

/*
 * Insert the type of transaction information item (tpi, additional data 
 * inside the header) into an octet.
 */
static unsigned char insert_tpi_type(int type, unsigned char octet){

       type &= 15;
       type <<= 3;
       octet |= type;

       return octet;
}

/*
 * Ditto length
 */
static unsigned char insert_tpi_length(int length, unsigned char octet){

       length &= 3;
       octet |= length;

       return octet;
}

/****************************************************************************/










