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
 *PROTOTYPES OF INTERNAL FUNCTIONS
 */

static Msg *wtp_pack_result(WTPMachine *machine, WTPEvent *event);

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

     Msg *msg=NULL;

     msg=wtp_pack_result(machine, event);
     if (msg == NULL)
        goto msg_error;

     put_msg_in_queue(msg);

     return;

/*
 *Abort(CAPTEMPEXCEEDED)
 */
msg_error:
     error(errno, "wtp_send_result: out of memory");
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

static Msg *wtp_pack_result(WTPMachine *machine, WTPEvent *event){

    Msg *msg;
    int octet,
        first_tid,
        last_tid,
        tid;

    char wtp_pdu[3];

    msg=msg_create(wdp_datagram);
    if (msg == NULL)
       goto error;
/*
 *First we transfer address four-tuple
 */

    msg->wdp_datagram.source_address = 
    	octstr_duplicate(machine->destination_address);
    if (msg->wdp_datagram.source_address == NULL)
       goto oct_error;
    msg->wdp_datagram.source_port=machine->destination_port;

    msg->wdp_datagram.destination_address = 
    	octstr_duplicate(machine->source_address);
    if (msg->wdp_datagram.destination_address == NULL)
       goto oct_error;
    msg->wdp_datagram.destination_port=machine->source_port;

/*
 * Then user data. We try to send fixed length result PDU, without segmenta-
 * tion. Only inputs are the rid field (which tells are we resending or not), 
 * and the tid.
 */      
    msg->wdp_datagram.user_data=octstr_copy(event->TRResult.user_data, 0,
         octstr_len(event->TRResult.user_data));
    if (msg->wdp_datagram.user_data == NULL)
       goto error;
/*
 * First we set pdu type
 */
    octet=0x02;
    octet<<=3;
/*
 * Then GTR and TTR flags on
 */
    octet|=0x6;
/*
 * And the value of rid
 */
    octet+=machine->rid;
    wtp_pdu[0]=octet;
/*
 * A responder turns on the first bit of the tid field, as an identification.
 */
    tid=event->TRResult.tid^0x8000;
    first_tid=tid>>8;
#if 0
    debug(0, "first tid was %d", first_tid);
#endif
    last_tid=event->TRResult.tid&0xff;
    wtp_pdu[1]=first_tid;
    wtp_pdu[2]=last_tid;
    
    if (octstr_insert_data(msg->wdp_datagram.user_data, 0, wtp_pdu, 3) == -1)
       goto oct_error; 
 
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
