/*
 * wtp.c - WTP common functions implementation
 *
 * Aarno Syvänen
 * Lars Wirzenius
 */

#include "wtp.h" 

/*****************************************************************************
 *
 * Prototypes of internal functions:
 *
 * Parse a `wdp_datagram' message object (of type Msg, see msg.h) and
 * create a corresponding WTPEvents list object. Also check that the datagram
 * is syntactically valid. If there is a problem (memory allocation or
 * invalid packet), then return NULL, and send an appropriate error
 * packet to the phone. Otherwise return a pointer to the event structure
 * that has been created.
 */

static WAPEvent *unpack_wdp_datagram_real(Msg *msg);

static int deduce_tid(Octstr *user_data);
static int concatenated_message(Octstr *user_data);
static int truncated_message(Msg *msg);
static WAPEvent *unpack_invoke(WTP_PDU *pdu, Msg *msg);
static WAPEvent *unpack_ack(WTP_PDU *pdu, Msg *msg);
static WAPEvent *unpack_abort(WTP_PDU *pdu, Msg *msg);
static WAPEvent *pack_error(Msg *msg);

/******************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 * Handles a possible concatenated message. Creates a list of wap events.
 */
List *wtp_unpack_wdp_datagram(Msg *msg){
        List *events = NULL;
        WAPEvent *event = NULL;
        Msg *msg_found = NULL;
        Octstr *data = NULL;
        long pdu_len;

        events = list_create();
        
        if (concatenated_message(msg->wdp_datagram.user_data)){
           data = octstr_duplicate(msg->wdp_datagram.user_data);
	   octstr_delete(data, 0, 1);

           while (octstr_len(data) != 0){

	         if (octstr_get_bits(data, 0, 1) == 0){
	            pdu_len = octstr_get_char(data, 0);
                    octstr_delete(data, 0, 1);

                 } else {
		    pdu_len = octstr_get_bits(data, 1, 15);
                    octstr_delete(data, 0, 2);
                 }
                 
                 msg_found = msg_duplicate(msg);
		 octstr_destroy(msg_found->wdp_datagram.user_data);
                 msg_found->wdp_datagram.user_data =
			octstr_copy(data, 0, pdu_len);
                 event = unpack_wdp_datagram_real(msg_found);
                 wap_event_assert(event);
                 list_append(events, event);
                 octstr_delete(data, 0, pdu_len);
                 msg_destroy(msg_found);
           }/* while*/
           octstr_destroy(data);

        } else {
	   event = unpack_wdp_datagram_real(msg); 
           wap_event_assert(event);
           list_append(events, event);
        } 

        return events;
}/* function */

/*
 * Responder set the first bit of the tid field. If we get a packet from the 
 * responder, we are the iniator. 
 *
 * Return 1, when the event is for responder, 0 when it is for iniator and 
 * -1 when error.
 */
int wtp_event_is_for_responder(WAPEvent *event){

    switch(event->type){
          
          case RcvInvoke:
	       return event->u.RcvInvoke.tid < INIATOR_TID_LIMIT;

          case RcvAck:
	       return event->u.RcvAck.tid < INIATOR_TID_LIMIT;

          case RcvAbort:
	       return event->u.RcvAbort.tid < INIATOR_TID_LIMIT;

          case RcvErrorPDU:
               return event->u.RcvErrorPDU.tid < INIATOR_TID_LIMIT;

          default:
	       error(1, "Received an erroneous PDU corresponding an event");
               wap_event_dump(event);
	       return -1;
    }
}

/*****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 * If pdu was truncated, tid cannot be trusted. We ignore this message.
 */
static int truncated_message(Msg *msg){

        if (octstr_len(msg->wdp_datagram.user_data) < 3){
           debug("wap.wtp", 0, "A too short PDU received");
           msg_dump(msg, 0);
           return 1;
        } else
	  return 0;
}

static WAPEvent *unpack_invoke(WTP_PDU *pdu, Msg *msg){
       WAPEvent *event;

       event = wap_event_create(RcvInvoke);
       event->u.RcvInvoke.user_data = 
	      octstr_duplicate(pdu->u.Invoke.user_data);
       event->u.RcvInvoke.tcl = pdu->u.Invoke.class;
       event->u.RcvInvoke.tid = pdu->u.Invoke.tid;
       event->u.RcvInvoke.tid_new = pdu->u.Invoke.tidnew;
       event->u.RcvInvoke.rid = pdu->u.Invoke.rid;
       event->u.RcvInvoke.up_flag = pdu->u.Invoke.uack;
       event->u.RcvInvoke.no_cache_supported = 0;
       event->u.RcvInvoke.version = pdu->u.Invoke.version;
       event->u.RcvInvoke.gtr = pdu->u.Invoke.gtr;
       event->u.RcvInvoke.ttr = pdu->u.Invoke.ttr;
       event->u.RcvInvoke.addr_tuple = 
              wap_addr_tuple_create(msg->wdp_datagram.source_address,
			            msg->wdp_datagram.source_port,
			            msg->wdp_datagram.destination_address,
			            msg->wdp_datagram.destination_port);

       return event;
}

static WAPEvent *unpack_ack(WTP_PDU *pdu, Msg *msg){
       WAPEvent *event;

       event = wap_event_create(RcvAck);
       event->u.RcvAck.tid = pdu->u.Ack.tid;
       event->u.RcvAck.tid_ok = pdu->u.Ack.tidverify;
       event->u.RcvAck.rid = pdu->u.Ack.rid;
       event->u.RcvAck.addr_tuple =
              wap_addr_tuple_create(msg->wdp_datagram.source_address,
		    	            msg->wdp_datagram.source_port,
			            msg->wdp_datagram.destination_address,
				    msg->wdp_datagram.destination_port);

       return event;
}

static WAPEvent *unpack_abort(WTP_PDU *pdu, Msg *msg){
       WAPEvent *event;

       event = wap_event_create(RcvAbort);
       event->u.RcvAbort.tid = pdu->u.Abort.tid;
       event->u.RcvAbort.abort_type = pdu->u.Abort.abort_type;
       event->u.RcvAbort.abort_reason = pdu->u.Abort.abort_reason;
       event->u.RcvAbort.addr_tuple = 
	      wap_addr_tuple_create(msg->wdp_datagram.source_address,
				    msg->wdp_datagram.source_port,
				    msg->wdp_datagram.destination_address,
				    msg->wdp_datagram.destination_port);

       return event;
}

static WAPEvent *pack_error(Msg *msg){
       WAPEvent *event;

       event = wap_event_create(RcvErrorPDU);
       event->u.RcvErrorPDU.tid = deduce_tid(msg->wdp_datagram.user_data);
       event->u.RcvErrorPDU.addr_tuple = 
	      wap_addr_tuple_create(msg->wdp_datagram.source_address,
				    msg->wdp_datagram.source_port,
				    msg->wdp_datagram.destination_address,
				    msg->wdp_datagram.destination_port);       

       return event;
}

/*
 * Transfers data from fields of a message to fields of WTP event. User data 
 * has the host byte order. Updates the log. 
 *
 * This function does incoming events check nro 4 (WTP 10.2).
 *
 * Return event, when we have a partially correct message or the message 
 * received has illegal header (WTP 10.2 nro 4); NULL, when the message was 
 * truncated or unpacking function returned NULL.
 */

WAPEvent *unpack_wdp_datagram_real(Msg *msg){
	WTP_PDU *pdu;

	WAPEvent *event;
        Octstr *data;

        data = msg->wdp_datagram.user_data;

        if (truncated_message(msg))
	    return NULL;

	pdu = wtp_pdu_unpack(data);
/*
 * Wtp_pdu_unpack returned NULL, we build a rcv error event. 
 */
	if (pdu == NULL){
           error(0, "pdu unpacking returned NULL");
           event = pack_error(msg);
           return event;
        }   		

	event = NULL;

	switch (pdu->type) {

	case Invoke:
	     event = unpack_invoke(pdu, msg);
/*
 * If an iniator gets invoke, it would be an illegal pdu.
 */
             if (!wtp_event_is_for_responder(event)){
                debug("wap.wtp", 0, "Invoke when iniator. Message was");
                wap_event_destroy(event);
                wap_event_create(RcvErrorPDU);
             }
	break;

        case Ack:
	     event = unpack_ack(pdu, msg);    
        break;

	case Abort:
	     event = unpack_abort(pdu, msg);
        break;         

	default:
	        event = pack_error(msg);
	        debug("wap.wtp", 0, "Unhandled PDU type. Message was");
                msg_dump(msg, 0);
		return event;
	}

	wtp_pdu_destroy(pdu);
	
	wap_event_assert(event);
	return event;
}

/*
 * Used for debugging and when wtp unpack does not return a tid. We include
 * first bit; it tells does message received belong to the iniator or to the
 * responder.
 */

static int deduce_tid(Octstr *user_data){
 
       return octstr_get_bits(user_data, 8, 16);
}

static int concatenated_message(Octstr *user_data){

       return octstr_get_char(user_data, 0) == 0x00;
}



