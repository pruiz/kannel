/*                          
 * wtp_send.c - WTP message sending module implementation
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp_send.h"
#include "msg.h"
#include "wapbox.h"

enum {
     basic_error,
     addr_error,
     user_error,
     oct_error
};

/*****************************************************************************
 *
 * Prototypes of internal functions
 */

static Msg *pack_result(Msg *msg, WTPMachine *machine, WTPEvent *event);

static Msg *pack_abort(Msg *msg, long abort_type, WTPMachine *machine, 
                       WTPEvent *event);

static Msg *pack_ack(Msg *msg, long ack_type, WTPMachine *machine, 
                     WTPEvent *event);

static Msg *add_datagram_address(Msg *msg, WTPMachine *machine);

static void tell_send_error(int type, Msg *msg);

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
     if (msg == NULL){
        tell_send_error(basic_error, msg);
        return;
     }

     if ((msg = add_datagram_address(msg, machine)) == NULL){
       tell_send_error(addr_error, msg);
       return;
     }
     debug(0, "WTP: packing result pdu");
     msg = pack_result(msg, machine, event);
     debug(0,"WTP: result pdu packed");
    
     if (msg == NULL){
        tell_send_error(oct_error, msg);
        return;
     }

     put_msg_in_queue(msg);

     return;
}

void wtp_send_abort(long abort_type, WTPMachine *machine, WTPEvent *event){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     if (msg == NULL){
        tell_send_error(basic_error, msg);
        return;
     }

     if ((msg = add_datagram_address(msg, machine)) == NULL){
       tell_send_error(addr_error, msg);
       return;
     }

     msg = pack_abort(msg, abort_type, machine, event);
    
     if (msg == NULL){
        tell_send_error(oct_error, msg);
        return;
     }

     put_msg_in_queue(msg);

     return;
}

void wtp_send_ack(long ack_type, WTPMachine *machine, WTPEvent *event){

     Msg *msg = NULL;

     msg = msg_create(wdp_datagram);
     if (msg == NULL){
        tell_send_error(basic_error, msg);
        return;
     }

     if ((msg = add_datagram_address(msg, machine)) == NULL){
       tell_send_error(addr_error, msg);
       return;
     }

     msg = pack_ack(msg, ack_type, machine, event);
     if (msg == NULL){
        tell_send_error(oct_error, msg);
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

    char *wtp_pdu; 
/*
 * We try to send fixed length result PDU, without segmentation. Only inputs 
 * are the rid field (which tells are we resending or not), and the tid.
 */  
 
    msg->wdp_datagram.user_data = octstr_copy(event->TRResult.user_data, 0,
                                  octstr_len(event->TRResult.user_data));
    if (msg->wdp_datagram.user_data == NULL){
       tell_send_error(basic_error, msg);
       return NULL;
    }
    
    octet = insert_pdu_type(RESULT, octet);
    octet = indicate_simple_message(octet);
    octet = insert_rid(machine->rid, octet);
    wtp_pdu[0] = octet;
    debug(0, "WTP: inserting tid");
    insert_tid(wtp_pdu, event->TRResult.tid);
    
    if (octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 3) == -1){
       tell_send_error(oct_error, msg);
       return NULL;
    }
 
    return msg;

}

static Msg *pack_abort(Msg *msg, long abort_type, WTPMachine *machine, 
                           WTPEvent *event){
       int octet;

       char wtp_pdu[4];
/*
 * User data includes, when we are speaking of abort PDU, a WSP PDU (Disconnect 
 * PDU). Inputs are abort type, abort reason and tid.
 */  
 
       msg->wdp_datagram.user_data = octstr_copy(event->TRAbort.user_data, 0,
                                     octstr_len(event->TRAbort.user_data));
       if (msg->wdp_datagram.user_data == NULL){
          tell_send_error(user_error, msg);
          return NULL;
       }

       octet = insert_pdu_type(ABORT, octet);
       octet = insert_abort_type(abort_type, octet);
       wtp_pdu[0] = octet;

       insert_tid(wtp_pdu, event->TRAbort.tid);

       wtp_pdu[3] = event->TRAbort.abort_reason;

       if (octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 4) == -1){
           tell_send_error(oct_error, msg);
           return NULL;
       }

       return msg;
}

static Msg *pack_ack(Msg *msg, long ack_type, WTPMachine *machine, 
                     WTPEvent *event){

    int octet;

    char wtp_pdu[3];
/*
 * Ack PDU is generated by WTP. Inputs are rid, ack type (are we doing tid 
 * verification or not) and tid.
 */
    octet = insert_pdu_type(ACK, octet);
    octet = indicate_ack_type(ack_type, octet);
    octet = insert_rid(machine->ack_pdu_sent, octet);
    wtp_pdu[0] = octet;

    insert_tid(wtp_pdu, machine->tid);

    if ((msg->wdp_datagram.user_data = octstr_create_empty()) == NULL){
       tell_send_error(user_error, msg);
       return NULL;
    }

    if (octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 3) == -1){
       tell_send_error(user_error, msg);
       return NULL;
    }

    return msg;
}

static Msg *add_datagram_address(Msg *msg, WTPMachine *machine){

       msg->wdp_datagram.source_address = 
    	    octstr_duplicate(machine->destination_address);
       if (msg->wdp_datagram.source_address == NULL){
          tell_send_error(oct_error, msg);
          return NULL;
       }

       msg->wdp_datagram.source_port = machine->destination_port;

       msg->wdp_datagram.destination_address = 
    	    octstr_duplicate(machine->source_address);
       
       if (msg->wdp_datagram.destination_address == NULL){
          tell_send_error(oct_error, msg);
          return NULL;
       }
       
       msg->wdp_datagram.destination_port = machine->source_port;

       return msg;
}

static void tell_send_error(int type, Msg *msg){

       switch (type){
/*
 * Abort(CAPTEMPEXCEEDED)
 */
              case addr_error:
                   error(0, "WTP: out of memory when trying to send a 
                         message");
                   free(msg->wdp_datagram.source_address);
                   free(msg->wdp_datagram.destination_address);
                   free(msg);
              break;

              case oct_error:
                   error(0, "WTP: out of memory when trying to send a 
                         message");
                   free(msg->wdp_datagram.user_data);
                   free(msg->wdp_datagram.source_address);
                   free(msg->wdp_datagram.destination_address);
                   free(msg);
             break;

             case user_error:
                  error(0, "WTP: out of memory");
                  free(msg->wdp_datagram.user_data);
                  free(msg);
             break;

             case basic_error:
                  error(0, "WTP: out of memory when trying to send a message");
                  free(msg);
             break;
       }
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










