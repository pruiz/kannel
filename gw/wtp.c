/*
 * wtp.c - WTP implementation
 *
 * Aarno Syvänen
 * Lars Wirzenius
 */

#include "gwlib/gwlib.h"
#include "wtp.h" 
#include "wtp_pdu.h" 

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
 * Global data structuresc:
 *
 * wtp machines list
 */

static List *machines = NULL;

/*
 * Global WTP transaction identifier and its lock (this is used by WSP when it 
 * wants to start a new transaction)
 */

static unsigned long wtp_tid = 0;

Mutex *wtp_tid_lock = NULL;


static Counter *machine_id_counter = NULL;

/*****************************************************************************
 *
 * Prototypes of internal functions:
 *
 * Create an uniniatilized wtp state machine.
 */

static WTPMachine *wtp_machine_create_empty(void);
static void wtp_machine_destroy(WTPMachine *sm);


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
static WTPMachine *wtp_machine_create(Octstr *srcaddr, long srcport,
				Octstr *destaddr, long destport, long tid,
				long tcl);

/*
 * Generates a new transaction handle by incrementing the previous one by one.
 */
static unsigned long wtp_tid_next(void);

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
static WTPMachine *wtp_machine_find(Octstr *source_address, long source_port,
	Octstr *destination_address, long destination_port, long tid,
	long mid);

/*
 * Packs a wsp event. Fetches flags and user data from a wtp event. Address 
 * five-tuple and tid are fields of the wtp machine.
 */
static WAPEvent *pack_wsp_event(WAPEventName wsp_name, WAPEvent *wtp_event, 
         WTPMachine *machine);

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

static List *queue = NULL;

static void main_thread(void *);


/******************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 */

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
 * Return event, when we have a single message or have reassembled whole the message; NULL, 
 * when we have a segment inside of a segmented message.
 */
WAPEvent *wtp_unpack_wdp_datagram(Msg *msg){
	WTP_PDU *pdu;
	WAPEvent *event;
	
	pdu = wtp_pdu_unpack(msg->wdp_datagram.user_data);
	if (pdu == NULL)
		return NULL;

	event = NULL;	

	switch (pdu->type) {
	case Invoke:
		event = wap_event_create(RcvInvoke);
		event->RcvInvoke.user_data = pdu->u.Invoke.user_data;
		event->RcvInvoke.exit_info = NULL;
		event->RcvInvoke.tcl = pdu->u.Invoke.class;
		event->RcvInvoke.tid = pdu->u.Invoke.tid;
		event->RcvInvoke.tid_new = pdu->u.Invoke.tidnew;
		event->RcvInvoke.rid = pdu->u.Invoke.rid;
		event->RcvInvoke.up_flag = pdu->u.Invoke.uack;
		event->RcvInvoke.exit_info_present = 0;
		event->RcvInvoke.no_cache_supported = 0;
		event->RcvInvoke.client_address = 
			octstr_duplicate(msg->wdp_datagram.source_address);
		event->RcvInvoke.client_port = 
			msg->wdp_datagram.source_port;
		event->RcvInvoke.server_address =
			octstr_duplicate(msg->wdp_datagram.destination_address);
		event->RcvInvoke.server_port = 
			msg->wdp_datagram.destination_port;
		break;

	case Ack:
		event = wap_event_create(RcvAck);
		event->RcvAck.tid = pdu->u.Ack.tid;
		event->RcvAck.tid_ok = pdu->u.Ack.tidverify;
		event->RcvAck.rid = pdu->u.Ack.rid;
		event->RcvAck.client_address = 
			octstr_duplicate(msg->wdp_datagram.source_address);
		event->RcvAck.client_port = 
			msg->wdp_datagram.source_port;
		event->RcvAck.server_address =
			octstr_duplicate(msg->wdp_datagram.destination_address);
		event->RcvAck.server_port = 
			msg->wdp_datagram.destination_port;
		break;

	case Abort:
		event = wap_event_create(RcvAbort);
		event->RcvAbort.tid = pdu->u.Abort.tid;
		event->RcvAbort.abort_type = pdu->u.Abort.abort_type;
		event->RcvAbort.abort_reason = pdu->u.Abort.abort_reason;
		event->RcvAbort.client_address = 
			octstr_duplicate(msg->wdp_datagram.source_address);
		event->RcvAbort.client_port = 
			msg->wdp_datagram.source_port;
		event->RcvAbort.server_address =
			octstr_duplicate(msg->wdp_datagram.destination_address);
		event->RcvAbort.server_port = 
			msg->wdp_datagram.destination_port;
		break;

	default:
		panic(0, "Unhandled WTP PDU type while unpacking!");
		break;
	}
	
	wap_event_assert(event);
	return event;
}

void wtp_init(void) {
     machines = list_create();
     machine_id_counter = counter_create();
     wtp_tid_lock = mutex_create();

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
     gwthread_join_all(main_thread);
     debug("wap.wtp", 0, "wtp_shutdown: %ld machines left",
     	   list_len(machines));
     while (list_len(machines) > 0)
	wtp_machine_destroy(list_extract_first(machines));
     list_destroy(machines);
     counter_destroy(machine_id_counter);
     mutex_destroy(wtp_tid_lock);
}

void wtp_dispatch_event(WAPEvent *event) {
	list_produce(queue, event);
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
     WAPEventName current_primitive;
     WAPEvent *wsp_event = NULL;
     WAPEvent *timer_event = NULL;

     debug("wap.wtp", 0, "WTP: machine %p, state %s, event %s.", 
	   (void *) machine, 
	   name_state(machine->state), 
	   wap_event_name(event->type));

     #define STATE_NAME(state)
     #define ROW(wtp_state, event_type, condition, action, next_state) \
	     if (machine->state == wtp_state && \
		event->type == event_type && \
		(condition)) { \
		action \
		machine->state = next_state; \
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

     if (!machine->in_use)
     	wtp_machine_destroy(machine);
}

static unsigned long wtp_tid_next(void){
     
     mutex_lock(wtp_tid_lock);
     ++wtp_tid;
     mutex_unlock(wtp_tid_lock);

     return wtp_tid;
} 


static WTPMachine *wtp_machine_find_or_create(WAPEvent *event){

          WTPMachine *machine = NULL;
          long tid;
	  Octstr *src_addr, *dst_addr;
	  long src_port, dst_port, mid;

	  tid = -1;
	  src_addr = NULL;
	  dst_addr = NULL;
	  src_port = -1;
	  dst_port = -1;
	  mid = -1;

          switch (event->type){

	          case RcvInvoke:
                       tid = event->RcvInvoke.tid;
		       src_addr = event->RcvInvoke.client_address;
		       src_port = event->RcvInvoke.client_port;
		       dst_addr = event->RcvInvoke.server_address;
		       dst_port = event->RcvInvoke.server_port;
                  break;

	          case RcvAck:
                       tid = event->RcvAck.tid;
		       src_addr = event->RcvAck.client_address;
		       src_port = event->RcvAck.client_port;
		       dst_addr = event->RcvAck.server_address;
		       dst_port = event->RcvAck.server_port;
                  break;

	          case RcvAbort:
                       tid = event->RcvAbort.tid;
		       src_addr = event->RcvAbort.client_address;
		       src_port = event->RcvAbort.client_port;
		       dst_addr = event->RcvAbort.server_address;
		       dst_port = event->RcvAbort.server_port;
                  break;

	          case RcvErrorPDU:
                       tid = event->RcvErrorPDU.tid;
		       src_addr = event->RcvErrorPDU.client_address;
		       src_port = event->RcvErrorPDU.client_port;
		       dst_addr = event->RcvErrorPDU.server_address;
		       dst_port = event->RcvErrorPDU.server_port;
                  break;

		  case TR_Invoke_Res:
		  	mid = event->TR_Invoke_Res.mid;
			break;

		  case TR_Result_Req:
		  	mid = event->TR_Result_Req.mid;
			break;

                  default:
                       debug("wap.wtp", 0, "WTP: machine_find_or_create: unhandled event"); 
                       wap_event_dump(event);
                       return NULL;
                  break;
	   }

	   gw_assert(src_addr != NULL || mid != -1);
           machine = wtp_machine_find(src_addr, src_port, dst_addr, dst_port,
                    		tid, mid);
           
           if (machine == NULL){

              switch (event->type){
/*
 * When PDU with an illegal header is received, its tcl-field is irrelevant (and possibly 
 * meaningless).
 */
	              case RcvInvoke: 
	                   machine = wtp_machine_create(
                                     src_addr, src_port, 
				     dst_addr, dst_port,
				     tid, event->RcvInvoke.tcl);
                           machine->in_use = 1;
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
 *  We are interested only machines in use, it is, having in_use-flag 1. Transaction
 *  is identified by the address four-tuple and tid.
 */
struct machine_pattern {
	Octstr *source_address;
	long source_port;
	Octstr *destination_address;
	long destination_port;
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

	return octstr_compare(m->source_address, pat->source_address) == 0 &&
               m->source_port == pat->source_port && 
               octstr_compare(m->destination_address, 
	                      pat->destination_address) == 0 &&
               m->destination_port == pat->destination_port &&
	       m->tid == pat->tid && 
	       m->in_use == 1;
}

static WTPMachine *wtp_machine_find(Octstr *source_address, long source_port,
       Octstr *destination_address, long destination_port, long tid,
       long mid){
	struct machine_pattern pat;
	WTPMachine *m;
	
	pat.source_address = source_address;
	pat.source_port = source_port;
	pat.destination_address = destination_address;
	pat.destination_port = destination_port;
	pat.tid = tid;
	pat.mid = mid;
	
	m = list_search(machines, &pat, is_wanted_machine);
	return m;
}

/*
 * Iniatilizes wtp machine and adds it to machines list. 
 */
static WTPMachine *wtp_machine_create_empty(void){
       WTPMachine *machine = NULL;

        machine = gw_malloc(sizeof(WTPMachine));
	machine->mid = counter_increase(machine_id_counter);
        
        #define INTEGER(name) machine->name = 0
        #define ENUM(name) machine->name = LISTEN
        #define MSG(name) machine->name = msg_create(wdp_datagram)
        #define OCTSTR(name) machine->name = NULL
        #define WSP_EVENT(name) machine->name = NULL
        #define TIMER(name) machine->name = wtp_timer_create()
        #define MACHINE(field) field
	#define LIST(name) machine->name = list_create()
        #include "wtp_machine-decl.h"

	list_append(machines, machine);

        return machine;
}

/*
 * Destroys a WTPMachine. Assumes it is safe to do so. Assumes it has already
 * been deleted from the machines list.
 */
static void wtp_machine_destroy(WTPMachine *machine){
	list_delete_equal(machines, machine);
        #define INTEGER(name) machine->name = 0
        #define ENUM(name) machine->name = LISTEN
        #define MSG(name) msg_destroy(machine->name)
        #define OCTSTR(name) octstr_destroy(machine->name)
        #define WSP_EVENT(name) machine->name = NULL
        #define TIMER(name) wtp_timer_destroy(machine->name)
        #define MACHINE(field) field
	#define LIST(name) list_destroy(machine->name)
        #include "wtp_machine-decl.h"
	gw_free(machine);
}

/*
 * Create a new WTPMachine for a given transaction, identified by the five-tuple 
 * in the arguments. In addition, update the transaction class field of the 
 * machine. If machines list is busy, just wait.
 */
WTPMachine *wtp_machine_create(Octstr *source_address, 
           long source_port, Octstr *destination_address, 
           long destination_port, long tid, long tcl) {

	   WTPMachine *machine = NULL;
	   
           machine = wtp_machine_create_empty();

           machine->source_address = octstr_duplicate(source_address);
           machine->source_port = source_port;
           machine->destination_address = octstr_duplicate(destination_address);
           machine->destination_port = destination_port;
           machine->tid = tid;
           machine->tcl = tcl;

           return machine;
} 

/*
 * Packs a wsp event. Fetches flags and user data from a wtp event. Address 
 * five-tuple and tid are fields of the wtp machine.
 */
static WAPEvent *pack_wsp_event(WAPEventName wsp_name, WAPEvent *wtp_event, 
         WTPMachine *machine){

         WAPEvent *event = wap_event_create(wsp_name);

         switch (wsp_name){
                
	        case TR_Invoke_Ind:
                     event->TR_Invoke_Ind.ack_type = machine->u_ack;
                     event->TR_Invoke_Ind.user_data =
                            octstr_duplicate(wtp_event->RcvInvoke.user_data);
                     event->TR_Invoke_Ind.tcl = wtp_event->RcvInvoke.tcl;
                     event->TR_Invoke_Ind.wsp_tid = wtp_tid_next();
                     event->TR_Invoke_Ind.machine = machine;
                break;

	        case TR_Invoke_Cnf:
                     event->TR_Invoke_Cnf.wsp_tid =
                            event->TR_Invoke_Ind.wsp_tid;
                     event->TR_Invoke_Cnf.machine = machine;
                break;
                
	        case TR_Result_Cnf:
                     event->TR_Result_Cnf.exit_info =
                            octstr_duplicate(wtp_event->RcvInvoke.exit_info);
                     event->TR_Result_Cnf.exit_info_present =
                            wtp_event->RcvInvoke.exit_info_present;
                     event->TR_Result_Cnf.wsp_tid =
                            event->TR_Invoke_Ind.wsp_tid;
                     event->TR_Result_Cnf.machine = machine;
                break;

	        case TR_Abort_Ind:
                     event->TR_Abort_Ind.abort_code =
                            wtp_event->RcvAbort.abort_reason;
                     event->TR_Abort_Ind.wsp_tid =
                            event->TR_Invoke_Ind.wsp_tid;
                     event->TR_Abort_Ind.machine = machine;
                break;
                
	        default:
                break;
         }

         return event;
} 
