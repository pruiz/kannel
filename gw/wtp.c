/*
 * wtp.c - WTP implementation
 *
 * Aarno Syvänen
 * Lars Wirzenius
 */

#include "gwlib/gwlib.h"
#include "wtp.h" 
#include "wtp_pdu.h" 

/***********************************************************************
 * Internal data structures.
 */


/*
 * Abort types (i.e., provider abort codes defined by WAP)
 */
enum {
	UNKNOWN = 0x00,
	PROTOERR = 0x01,
	INVALIDTID = 0x02,
	NOTIMPLEMENTEDCL2 = 0x03,
	NOTIMPLEMENTEDSAR = 0x04,
	NOTIMPLEMENTEDUACK = 0x05,
	WTPVERSIONZERO = 0x06,
	CAPTEMPEXCEEDED = 0x07,
	NORESPONSE = 0x08,
	MESSAGETOOLARGE = 0x09
};    


/*
 * List of WTPMachines.
 */
static List *machines = NULL;


/*
 * Counter for WTPMachine id numbers, to make sure they are unique.
 */
static Counter *machine_id_counter = NULL;


/*
 * Give the status the module:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } run_status = limbo;


/*
 * Queue of events to be handled by WTP layer.
 */
static List *queue = NULL;


/*****************************************************************************
 *
 * Prototypes of internal functions:
 */
 
/*
 * Create an uniniatilized wtp state machine.
 */

static WTPMachine *wtp_machine_create(WAPAddrTuple *tuple, long tid, long tcl);
static void wtp_machine_destroy(WTPMachine *sm);

/*
 * Parse a `wdp_datagram' message object (of type Msg, see msg.h) and
 * create a corresponding WTPEvents list object. Also check that the datagram
 * is syntactically valid. If there is a problem (memory allocation or
 * invalid packet), then return NULL, and send an appropriate error
 * packet to the phone. Otherwise return a pointer to the event structure
 * that has been created.
 */
static WAPEvent *wtp_unpack_wdp_datagram_real(Msg *msg);

/*
 * Checks whether wtp machines data structure includes a spesific machine.
 * The machine in question is identified with with source and destination
 * address and port and tid. Address information is fetched from message
 * fields, tid from an field of the event. If the machine does not exist and
 * the event is RcvInvoke, a new machine is created and added in the machines
 * data structure. If the event was RcvAck or RcvAbort, the event is ignored.
 * If the event is RcvErrorPDU, new machine is created.
 */
static WTPMachine *wtp_machine_find_or_create(WAPEvent *event);


/*
 * Feed an event to a WTP state machine. Handle all errors by itself, do not 
 * report them to the caller. Generate a pointer to WSP event, if an indication 
 * or a confirmation is required.
 */
static void wtp_handle_event(WTPMachine *machine, WAPEvent *event);

/*
 * Creates wtp machine having addsress quintuple and transaction class 
 * iniatilised. If machines list is busy, just waits.
 */ 

/*
 * Print a wtp event or a wtp machine state name as a string.
 */

static unsigned char *name_state(int name);

/*
 * Find the WTPMachine from the global list of WTPMachine structures that
 * corresponds to the five-tuple of source and destination addresses and
 * ports and the transaction identifier. Return a pointer to the machine,
 * or NULL if not found.
 */
static WTPMachine *wtp_machine_find(WAPAddrTuple *tuple, long tid, long mid);


static void main_thread(void *);
static WTPMachine *find_machine_using_mid(long mid);
static WAPEvent *create_tr_invoke_ind(WTPMachine *sm, Octstr *user_data);
static WAPEvent *create_tr_result_cnf(WTPMachine *sm);
static WAPEvent *create_tr_abort_ind(WTPMachine *sm, long abort_reason);
static WAPEvent *create_rcv_error_pdu(Msg *msg);

static int deduce_tid(Octstr *user_data);
static int concatenated_message(Octstr *user_data);

static void handle_wrong_version(Msg *msg, int tid);
static void handle_no_sar(Msg *msg, int tid);

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
                 event = wtp_unpack_wdp_datagram_real(msg_found);
                 wap_event_assert(event);
                 list_append(events, event);
                 octstr_delete(data, 0, pdu_len);
                 msg_destroy(msg_found);
           }/* while*/
           octstr_destroy(data);

        } else {
	   event = wtp_unpack_wdp_datagram_real(msg); 
           wap_event_assert(event);
           list_append(events, event);
        } 

        return events;
}/* function */

/*
 * Transfers data from fields of a message to fields of WTP event. User data has
 * the host byte order. Updates the log and sends protocol error messages. Reassembles 
 * segmented messages, too.
 *
 * First empty instance of segment_lists is created by wapbox.c. This function allocates 
 * and deallocates memory for its member lists. After deallocation a new instance of an 
 * empty segments data structure is creted. For result, an wtp event is created, if 
 * appropiate. The memory for this data structure is deallocated either by this module, if 
 * its data is added to a message to be reassembled, or by wtp_handle_event.
 *
 * Return event, when we have a single message or have reassembled whole the message or 
 * the message received has illegal header; NULL, when we have a segment inside of a 
 * segmented message or when it has a special error.
 */
WAPEvent *wtp_unpack_wdp_datagram_real(Msg *msg){
	WTP_PDU *pdu;
	WAPEvent *event;
        Octstr *data;
	int tid;

        if (octstr_len(data = msg->wdp_datagram.user_data) < 3){
           event = create_rcv_error_pdu(msg);
           debug("wap.wtp", 0, "A too short PDU received");
           msg_dump(msg, 0);
           return event;
        }

        tid = deduce_tid(msg->wdp_datagram.user_data);
        debug("wap.wtp", 0, "tid was %d", tid);
	pdu = wtp_pdu_unpack(data);

/*
 * Wtp_pdu_unpack returns NULL, when the error was illegal header
 */
	if (pdu == NULL){
	   event = create_rcv_error_pdu(msg);
           debug("wap.wtp", 0, "A PDU with an illegal header received");
           return event;
        }   		

	event = NULL;	

	switch (pdu->type) {

	case Invoke:
	     if (pdu->u.Invoke.version != 0) {
		debug("wap.wtp", 0, "WTP: Received PDU with wrong version field %ld.", pdu->u.Invoke.version);
		handle_wrong_version(msg, pdu->u.Invoke.tid);
	        return NULL;
	     }
	     if (pdu->u.Invoke.ttr && pdu->u.Invoke.gtr){
		event = wap_event_create(RcvInvoke);
		event->u.RcvInvoke.user_data = 
			octstr_duplicate(pdu->u.Invoke.user_data);
		event->u.RcvInvoke.tcl = pdu->u.Invoke.class;
		event->u.RcvInvoke.tid = pdu->u.Invoke.tid;
		event->u.RcvInvoke.tid_new = pdu->u.Invoke.tidnew;
		event->u.RcvInvoke.rid = pdu->u.Invoke.rid;
		event->u.RcvInvoke.up_flag = pdu->u.Invoke.uack;
		event->u.RcvInvoke.no_cache_supported = 0;
		event->u.RcvInvoke.addr_tuple = 
		  wap_addr_tuple_create(msg->wdp_datagram.source_address,
					msg->wdp_datagram.source_port,
					msg->wdp_datagram.destination_address,
					msg->wdp_datagram.destination_port);
             } else {
                handle_no_sar(msg, pdu->u.Invoke.tid);
	        return NULL;
             }
		break;

	case Ack:
		event = wap_event_create(RcvAck);
		event->u.RcvAck.tid = pdu->u.Ack.tid;
		event->u.RcvAck.tid_ok = pdu->u.Ack.tidverify;
		event->u.RcvAck.rid = pdu->u.Ack.rid;
		event->u.RcvAck.addr_tuple =
		  wap_addr_tuple_create(msg->wdp_datagram.source_address,
					msg->wdp_datagram.source_port,
					msg->wdp_datagram.destination_address,
					msg->wdp_datagram.destination_port);
		break;

	case Abort:
		event = wap_event_create(RcvAbort);
		event->u.RcvAbort.tid = pdu->u.Abort.tid;
		event->u.RcvAbort.abort_type = pdu->u.Abort.abort_type;
		event->u.RcvAbort.abort_reason = pdu->u.Abort.abort_reason;
		event->u.RcvAbort.addr_tuple = 
		  wap_addr_tuple_create(msg->wdp_datagram.source_address,
					msg->wdp_datagram.source_port,
					msg->wdp_datagram.destination_address,
					msg->wdp_datagram.destination_port);
		break;

	default:
	        event = wap_event_create(RcvErrorPDU);
	        debug("wap.wtp", 0, "Unhandled PDU type. Message was");
                msg_dump(msg, 0);
		return event;
	}

	wtp_pdu_destroy(pdu);
	
	wap_event_assert(event);
	return event;
}

void wtp_init(void) {
     machines = list_create();
     machine_id_counter = counter_create();

     queue = list_create();
     list_add_producer(queue);

     gw_assert(run_status == limbo);
     run_status = running;
     gwthread_create(main_thread, NULL);
}

void wtp_shutdown(void) {
     gw_assert(run_status == running);
     run_status = terminating;
     list_remove_producer(queue);
     gwthread_join_every(main_thread);

     debug("wap.wtp", 0, "wtp_shutdown: %ld machines left",
     	   list_len(machines));
     while (list_len(machines) > 0)
	wtp_machine_destroy(list_extract_first(machines));
     list_destroy(machines);

     while (list_len(queue) > 0)
	wap_event_destroy(list_extract_first(queue));
     list_destroy(queue);

     counter_destroy(machine_id_counter);
}

void wtp_dispatch_event(WAPEvent *event) {
	list_produce(queue, event);
}

int wtp_get_address_tuple(long mid, WAPAddrTuple **tuple) {
	WTPMachine *sm;
	
	sm = find_machine_using_mid(mid);
	if (sm == NULL)
		return -1;

	*tuple = wap_addr_tuple_duplicate(sm->addr_tuple);

	return 0;
}


/*****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 */

static void main_thread(void *arg) {
	WTPMachine *sm;
	WAPEvent *e;

	while (run_status == running && (e = list_consume(queue)) != NULL) {
		sm = wtp_machine_find_or_create(e);
		if (sm == NULL)
			wap_event_destroy(e);
		else
			wtp_handle_event(sm, e);
	}
}


 /*
 * Give the name of an event in a readable form. 
 */

static unsigned char *name_state(int s){

       switch (s){
              #define STATE_NAME(state) case state: return #state;
              #define ROW(state, event, condition, action, new_state)
              #include "wtp_state-decl.h"
              default:
                      return "unknown state";
       }
}


/*
 * Feed an event to a WTP state machine. Handle all errors yourself, do not
 * report them to the caller. Note: Do not put {}s of the else block inside
 * the macro definition (it ends with a line without a backlash). 
 */
static void wtp_handle_event(WTPMachine *machine, WAPEvent *event){
     WAPEvent *wsp_event = NULL;
     WAPEvent *timer_event = NULL;

     debug("wap.wtp", 0, "WTP: machine %ld, state %s, event %s.", 
	   machine->mid, 
	   name_state(machine->state), 
	   wap_event_name(event->type));

     #define STATE_NAME(state)
     #define ROW(wtp_state, event_type, condition, action, next_state) \
	     if (machine->state == wtp_state && \
		event->type == event_type && \
		(condition)) { \
		action \
		machine->state = next_state; \
		debug("wap.wtp", 0, "WTP %ld: New state %s", machine->mid, #next_state); \
	     } else 
     #include "wtp_state-decl.h"
	     {
		error(0, "WTP: handle_event: unhandled event!");
		debug("wap.wtp", 0, "WTP: handle_event: Unhandled event was:");
		wap_event_dump(event);
		return;
	     }

     if (event != NULL) {
	wap_event_destroy(event);  
     }

     if (machine->state == LISTEN)
     	wtp_machine_destroy(machine);
}


static WTPMachine *wtp_machine_find_or_create(WAPEvent *event){

          WTPMachine *machine = NULL;
          long tid, mid;
	  WAPAddrTuple *tuple;

	  tid = -1;
	  tuple = NULL;
	  mid = -1;

          switch (event->type){

	          case RcvInvoke:
                       tid = event->u.RcvInvoke.tid;
		       tuple = event->u.RcvInvoke.addr_tuple;
                  break;

	          case RcvAck:
                       tid = event->u.RcvAck.tid;
		       tuple = event->u.RcvAck.addr_tuple;
                  break;

	          case RcvAbort:
                       tid = event->u.RcvAbort.tid;
		       tuple = event->u.RcvAbort.addr_tuple;
                  break;

	          case RcvErrorPDU:
                       tid = event->u.RcvErrorPDU.tid;
		       tuple = event->u.RcvErrorPDU.addr_tuple;
                  break;

		  case TR_Invoke_Req:
			/* XXX We don't support this yet, we have to
			 * be WTP Initiator too to get this right. */
			break;

		  case TR_Invoke_Res:
		  	mid = event->u.TR_Invoke_Res.handle;
			break;

		  case TR_Result_Req:
		  	mid = event->u.TR_Result_Req.handle;
			break;

		  case TR_Abort_Req:
			mid = event->u.TR_Abort_Req.handle;
			break;

                  default:
                       debug("wap.wtp", 0, "WTP: machine_find_or_create: unhandled event"); 
                       wap_event_dump(event);
                       return NULL;
                  break;
	   }

	   gw_assert(tuple != NULL || mid != -1);
           machine = wtp_machine_find(tuple, tid, mid);
           
           if (machine == NULL){

              switch (event->type){
/*
 * When PDU with an illegal header is received, its tcl-field is irrelevant (and possibly 
 * meaningless). In this case we must create a new machine, if there is any. There is a
 * machine for all events handled statefull manner.
 */
	              case RcvInvoke: case RcvErrorPDU:
	                   machine = wtp_machine_create(tuple, tid,
							event->u.RcvInvoke.tcl);
                      break;

	              case RcvAck: 
			   info(0, "WTP: machine_find_or_create: ack received, yet having no machine");
                      break;

                      case RcvAbort: 
			   info(0, "WTP: machine_find_or_create: abort received, yet having no machine");
                      break;
                 
	              default:
                           debug("wap.wtp", 0, "WTP: machine_find_or_create: unhandled event");
                           wap_event_dump(event);
                           return NULL;
                      break;
              }
	   }
           
           return machine;
}

/*
 *  Transaction is identified by the address four-tuple and tid.
 */
struct machine_pattern {
	WAPAddrTuple *tuple;
	long tid;
	long mid;
};

static int is_wanted_machine(void *a, void *b) {
	struct machine_pattern *pat;
	WTPMachine *m;
	
	m = a;
	pat = b;

	if (m->mid == pat->mid)
		return 1;

	if (pat->mid != -1)
		return 0;

	return m->tid == pat->tid && 
		wap_addr_tuple_same(m->addr_tuple, pat->tuple);
}


static WTPMachine *wtp_machine_find(WAPAddrTuple *tuple, long tid, long mid) {
	struct machine_pattern pat;
	WTPMachine *m;
	
	pat.tuple = tuple;
	pat.tid = tid;
	pat.mid = mid;
	
	m = list_search(machines, &pat, is_wanted_machine);
	return m;
}


WTPMachine *wtp_machine_create(WAPAddrTuple *tuple, long tid, long tcl) {
	WTPMachine *machine;
	
        machine = gw_malloc(sizeof(WTPMachine));
        
        #define INTEGER(name) machine->name = 0;
        #define ENUM(name) machine->name = LISTEN;
        #define MSG(name) machine->name = msg_create(wdp_datagram);
        #define OCTSTR(name) machine->name = NULL;
        #define WSP_EVENT(name) machine->name = NULL;
        #define TIMER(name) machine->name = wtp_timer_create();
	#define LIST(name) machine->name = list_create();
	#define ADDRTUPLE(name) machine->name = NULL;
        #define MACHINE(field) field
        #include "wtp_machine-decl.h"

	list_append(machines, machine);

	machine->mid = counter_increase(machine_id_counter);
	machine->addr_tuple = wap_addr_tuple_duplicate(tuple);
	machine->tid = tid;
	machine->tcl = tcl;
	
	debug("wap.wtp", 0, "WTP: Created WTPMachine %p (%ld)", 
		(void *) machine, machine->mid);

	return machine;
} 


/*
 * Destroys a WTPMachine. Assumes it is safe to do so. Assumes it has already
 * been deleted from the machines list.
 */
static void wtp_machine_destroy(WTPMachine *machine){
	debug("wap.wtp", 0, "WTP: Destroying WTPMachine %p (%ld)", 
		(void *) machine, machine->mid);
	
	list_delete_equal(machines, machine);
        #define INTEGER(name) machine->name = 0;
        #define ENUM(name) machine->name = LISTEN;
        #define MSG(name) msg_destroy(machine->name);
        #define OCTSTR(name) octstr_destroy(machine->name);
        #define WSP_EVENT(name) machine->name = NULL;
        #define TIMER(name) wtp_timer_destroy(machine->name);
	#define LIST(name) list_destroy(machine->name);
	#define ADDRTUPLE(name) wap_addr_tuple_destroy(machine->name);
        #define MACHINE(field) field
        #include "wtp_machine-decl.h"
	gw_free(machine);
}

/*
 * Create a TR-Invoke.ind event.
 */
static WAPEvent *create_tr_invoke_ind(WTPMachine *sm, Octstr *user_data) {
	WAPEvent *event;
	
	event = wap_event_create(TR_Invoke_Ind);
	event->u.TR_Invoke_Ind.ack_type = sm->u_ack;
	event->u.TR_Invoke_Ind.user_data = octstr_duplicate(user_data);
	event->u.TR_Invoke_Ind.tcl = sm->tcl;
	event->u.TR_Invoke_Ind.addr_tuple = 
		wap_addr_tuple_duplicate(sm->addr_tuple);
	event->u.TR_Invoke_Ind.handle = sm->mid;
	return event;
}


/*
 * Create a TR-Result.cnf event.
 */
static WAPEvent *create_tr_result_cnf(WTPMachine *sm) {
	WAPEvent *event;
	
	event = wap_event_create(TR_Result_Cnf);
	event->u.TR_Result_Cnf.addr_tuple = 
		wap_addr_tuple_duplicate(sm->addr_tuple);
	event->u.TR_Result_Cnf.handle = sm->mid;
	return event;
}


/*
 * Create a TR-Abort.ind event.
 */
static WAPEvent *create_tr_abort_ind(WTPMachine *sm, long abort_reason) {
	WAPEvent *event;
	
	event = wap_event_create(TR_Abort_Ind);

	event->u.TR_Abort_Ind.abort_code = abort_reason;
	event->u.TR_Abort_Ind.addr_tuple = 
		wap_addr_tuple_duplicate(sm->addr_tuple);
	event->u.TR_Abort_Ind.handle = sm->mid;

	return event;
}

/*
 * Create RcvErrorPDU event
 */
static WAPEvent *create_rcv_error_pdu(Msg *msg){
       WAPEvent *event;

       gw_assert(msg != NULL);
       event = wap_event_create(RcvErrorPDU);
       event->u.RcvErrorPDU.tid = deduce_tid(msg->wdp_datagram.user_data);
       event->u.RcvErrorPDU.addr_tuple = wap_addr_tuple_create(
                                         msg->wdp_datagram.source_address,
                                         msg->wdp_datagram.source_port,
                                         msg->wdp_datagram.destination_address,
                                         msg->wdp_datagram.destination_port);
       return event;
}

static int machine_has_mid(void *a, void *b) {
	WTPMachine *sm;
	long mid;
	
	sm = a;
	mid = *(long *) b;
	return sm->mid == mid;
}

static int deduce_tid(Octstr *user_data){
 
       return octstr_get_bits(user_data, 8, 16);
}

static int concatenated_message(Octstr *user_data){

       return octstr_get_char(user_data, 0) == 0x00;
}

static WTPMachine *find_machine_using_mid(long mid) {
       return list_search(machines, &mid, machine_has_mid);
}

static void handle_wrong_version(Msg *msg, int tid){
       WAPAddrTuple *address;

       address = wap_addr_tuple_create(msg->wdp_datagram.source_address, 
                                       msg->wdp_datagram.source_port,
                                       msg->wdp_datagram.destination_address,
                                       msg->wdp_datagram.destination_port);
       wtp_do_not_start(PROVIDER, WTPVERSIONZERO, address, tid);
       wap_addr_tuple_destroy(address);
}

/*
 * This function will be removed when we have SAR
 */
static void handle_no_sar(Msg *msg, int tid){
       WAPAddrTuple *address;

       address = wap_addr_tuple_create(msg->wdp_datagram.source_address, 
                                       msg->wdp_datagram.source_port,
                                       msg->wdp_datagram.destination_address,
                                       msg->wdp_datagram.destination_port);
       wtp_do_not_start(PROVIDER, NOTIMPLEMENTEDSAR, address, tid);
       wap_addr_tuple_destroy(address);
}
