/*
 *wtp_send.c - WTP message module implementation
 *
 *By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp_send.h"
#include "msg.h"

/*****************************************************************************
 *
 *PROTOTYPES OF INTERNAL FUNCTIONS
 */

static Msg *pack_result(WTPMachine *machine, WTPEvent *event);

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

     Msg *msg;
     Octstr *message_string;

     msg=pack_result(machine, event);
     if (msg == NULL)
        goto error;

#if 0
     msg_destroy(msg);
     octstr_destroy(message_string);
#endif
     return;
/*
 *Abort(CAPTEMPEXCEEDED)
 */
error:
     error(errno, "pack_msg: out of memory");
     free(msg); 
     return;
}

/****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 * Packs a message object, of wdp datagram type, having result PDU as user 
 * data. Fetches SDU from WTP event, address four-tuple and machine state 
 * information (are we resending the packet) from WTP machine. Handles all 
 * errors by itself.
 */

static Msg *pack_result(WTPMachine *machine, WTPEvent *event){

    Msg *msg;
    int octet,
        first_tid,
        last_tid;

    msg=msg_create(wdp_datagram);
    if (msg == NULL)
       goto error;
/*
 *First we transfer address four-tuple
 */
    msg->wdp_datagram.source_address=
         octstr_copy(machine->source_address, 0, 
         octstr_len(machine->source_address));
    if (msg->wdp_datagram.source_address == NULL)
       goto oct_error;
    msg->wdp_datagram.source_port=machine->source_port;
    msg->wdp_datagram.destination_address=
        octstr_copy(machine->destination_address, 0, 
        octstr_len(machine->destination_address));
    if (msg->wdp_datagram.destination_address == NULL)
       goto oct_error;
    msg->wdp_datagram.destination_port=machine->destination_port;
/*
 * Then user data. We try to send fixed length result PDU, without segmenta-
 * tion. Only inputs are the rid field (which tells are we resending or not), 
 * and the tid.
 */      
    msg->wdp_datagram.user_data=octstr_copy(event->TRResult.user_data, 0,
         octstr_len(event->TRResult.user_data));
    if (msg->wdp_datagram.user_data == NULL)
       goto oct_error;

    octet=0x02;
    octet<<=3;

    octet|=0x6;
    octet+=machine->rid;
    
    octstr_set_char(event->TRResult.user_data, 0, octet);  
/*
 *A responder turns on the first bit of the tid field, as an identification.
 */
    first_tid=event->TRResult.tid>>8^0x8000;
    last_tid=event->TRResult.tid&0xff;
    octstr_set_char(event->TRResult.user_data, 1, first_tid);
    octstr_set_char(event->TRResult.user_data, 2, last_tid); 
 
    return msg;
/*
 *Abort(CAPTEMPEXCEEDED)
 */
oct_error:
    error(errno, "wtp_pack_result: out of memory");
    free(msg->wdp_datagram.user_data);
    free(msg->wdp_datagram.source_address);
    free(msg->wdp_datagram.destination_address);
    free(msg);
    return NULL;

error:
    error(errno, "wtp-send: out of memory");
    free(msg);
    return NULL;
}
/****************************************************************************/
