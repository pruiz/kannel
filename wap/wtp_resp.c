 /*
 * wtp_resp.c - WTP responder implementation
 *
 * Aarno Syvänen
 * Lars Wirzenius
 */

#include "gwlib/gwlib.h"
#include "wtp_resp.h"
#include "wtp_pack.h"
#include "wtp_tid.h"
#include "wtp.h"
#include "timers.h"
#include "wap.h"

/***********************************************************************
 * Internal data structures.
 *
 * List of responder WTP machines.
 */
static List *resp_machines = NULL;


/*
 * Counter for responder WTP machine id numbers, to make sure they are unique.
 */
static Counter *resp_machine_id_counter = NULL;


/*
 * Give the status of wtp responder:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } resp_run_status = limbo;


wap_dispatch_func_t *dispatch_to_wdp;
wap_dispatch_func_t *dispatch_to_wsp;
wap_dispatch_func_t *dispatch_to_push;

/*
 * Queue of events to be handled by WTP responder.
 */
static List *resp_queue = NULL;


/*****************************************************************************
 *
 * Prototypes of internal functions:
 *
 * Create and destroy an uniniatilized wtp responder state machine.
 */

static WTPRespMachine *resp_machine_create(WAPAddrTuple *tuple, long tid, 
                                           long tcl);
static void resp_machine_destroy(void *sm);

/*
 * Checks whether wtp responser machines data structure includes a specific 
 * machine.
 * The machine in question is identified with with source and destination
 * address and port and tid. If the machine does not exist and the event is 
 * RcvInvoke, a new machine is created and added in the machines data 
 * structure. 
 * First test incoming events (WTP 10.2) (Exception is tests nro 4 and 5: if 
 * we have a memory error, we panic. Nro 4 is already checked)  If event was 
 * validated and If the event was RcvAck or RcvAbort, the event is ignored. 
 * If the event is RcvErrorPDU, new machine is created.
 */
static WTPRespMachine *resp_machine_find_or_create(WAPEvent *event);


/*
 * Feed an event to a WTP responder state machine. Handle all errors by 
 * itself, do not report them to the caller. WSP indication or confirmation 
 * is handled by an included state table.
 */
static void resp_event_handle(WTPRespMachine *machine, WAPEvent *event);

/*
 * Print a wtp responder machine state as a string.
 */
static unsigned char *name_resp_state(int name);

/*
 * Find the wtp responder machine from the global list of wtp responder 
 * structures that corresponds to the five-tuple of source and destination 
 * addresses and ports and the transaction identifier. Return a pointer to 
 * the machine, or NULL if not found.
 */
static WTPRespMachine *resp_machine_find(WAPAddrTuple *tuple, long tid, 
                                         long mid);
static void main_thread(void *);

/*
 * Start acknowledgement interval timer
 */
static void start_timer_A(WTPRespMachine *machine);

/*
 * Start retry interval timer
 */
static void start_timer_R(WTPRespMachine *machine);

/*
 * Start timeout interval timer.
 */
static void start_timer_W(WTPRespMachine *machine);
static WAPEvent *create_tr_invoke_ind(WTPRespMachine *sm, Octstr *user_data);
static WAPEvent *create_tr_abort_ind(WTPRespMachine *sm, long abort_reason);
static WAPEvent *create_tr_result_cnf(WTPRespMachine *sm);
static int erroneous_field_in(WAPEvent *event);
static void handle_no_sar(WAPEvent *event);
static void handle_wrong_version(WAPEvent *event);

/*
 * Create a datagram with an Abort PDU and send it to the WDP layer.
 */
static void send_abort(WTPRespMachine *machine, long type, long reason);

/*
 * Create a datagram with an Ack PDU and send it to the WDP layer.
 */
static void send_ack(WTPRespMachine *machine, long ack_type, int rid_flag);


/******************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 */

void wtp_resp_init(wap_dispatch_func_t *datagram_dispatch,
                   wap_dispatch_func_t *session_dispatch,
                   wap_dispatch_func_t *push_dispatch) 
{
    resp_machines = list_create();
    resp_machine_id_counter = counter_create();

    resp_queue = list_create();
    list_add_producer(resp_queue);

    dispatch_to_wdp = datagram_dispatch;
    dispatch_to_wsp = session_dispatch;
    dispatch_to_push = push_dispatch;

    timers_init();
    wtp_tid_cache_init();

    gw_assert(resp_run_status == limbo);
    resp_run_status = running;
    gwthread_create(main_thread, NULL);
}

void wtp_resp_shutdown(void) 
{
    gw_assert(resp_run_status == running);
    resp_run_status = terminating;
    list_remove_producer(resp_queue);
    gwthread_join_every(main_thread);

    debug("wap.wtp", 0, "wtp_resp_shutdown: %ld resp_machines left",
     	  list_len(resp_machines));
    list_destroy(resp_machines, resp_machine_destroy);
    list_destroy(resp_queue, wap_event_destroy_item);

    counter_destroy(resp_machine_id_counter);

    wtp_tid_cache_shutdown();
    timers_shutdown();
}

void wtp_resp_dispatch_event(WAPEvent *event) 
{
    list_produce(resp_queue, event);
}


/*****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 */

static void main_thread(void *arg) 
{
    WTPRespMachine *sm;
    WAPEvent *e;

    while (resp_run_status == running && 
           (e = list_consume(resp_queue)) != NULL) {
	sm = resp_machine_find_or_create(e);
	if (sm == NULL)
	    wap_event_destroy(e);
	else
	    resp_event_handle(sm, e);
	}
}

/*
 * Give the name of a responder state in a readable form. 
 */
static unsigned char *name_resp_state(int s)
{
       switch (s) {
       #define STATE_NAME(state) case state: return #state;
       #define ROW(state, event, condition, action, new_state)
       #include "wtp_resp_states.def"
       default:
           return "unknown state";
       }
}


/*
 * Feed an event to a WTP responder state machine. Handle all errors yourself,
 * do not report them to the caller. Note: Do not put {}s of the else block 
 * inside the macro definition. 
 */
static void resp_event_handle(WTPRespMachine *resp_machine, WAPEvent *event)
{
    WAPEvent *wsp_event = NULL;

    debug("wap.wtp", 0, "WTP: resp_machine %ld, state %s, event %s.", 
	  resp_machine->mid, 
	  name_resp_state(resp_machine->state), 
	  wap_event_name(event->type));

    #define STATE_NAME(state)
    #define ROW(wtp_state, event_type, condition, action, next_state) \
	 if (resp_machine->state == wtp_state && \
	     event->type == event_type && \
	     (condition)) { \
	     action \
	     resp_machine->state = next_state; \
	     debug("wap.wtp", 0, "WTP %ld: New state %s", resp_machine->mid,                      #next_state); \
	 } else 
    #include "wtp_resp_states.def"
	 {
	     error(0, "WTP: handle_event: unhandled event!");
	     debug("wap.wtp", 0, "WTP: handle_event: Unhandled event was:");
	     wap_event_dump(event);
             wap_event_destroy(event);
             return;
	 }

    if (event != NULL) {
	wap_event_destroy(event);  
    }

    if (resp_machine->state == LISTEN)
     	resp_machine_destroy(resp_machine);
}

static void handle_wrong_version(WAPEvent *event)
{       
    WAPEvent *ab;

    if (event->type == RcvInvoke) {
        ab = wtp_pack_abort(PROVIDER, WTPVERSIONZERO, event->u.RcvInvoke.tid, 
                            event->u.RcvInvoke.addr_tuple);
        dispatch_to_wdp(ab);
    }
}

/*
 * This function will be removed when we have SAR
 */
static void handle_no_sar(WAPEvent *event)
{
    WAPEvent *ab;

    if (event->type == RcvInvoke) {
        ab = wtp_pack_abort(PROVIDER, NOTIMPLEMENTEDSAR, 
                            event->u.RcvInvoke.tid,
                            event->u.RcvInvoke.addr_tuple);
        dispatch_to_wdp(ab);
    }
}

/*
 * Check for features 7 and 9 in WTP 10.2.
 */
static int erroneous_field_in(WAPEvent *event)
{
    return event->type == RcvInvoke && (event->u.RcvInvoke.version != 0 || 
           !event->u.RcvInvoke.ttr || !event->u.RcvInvoke.gtr);
}

/*
 * React features 7 and 9 in WTP 10.2, by aborting with an appropiate error 
 * message.
 */
static void handle_erroneous_field_in(WAPEvent *event)
{
    if (event->type == RcvInvoke){
        if (event->u.RcvInvoke.version != 0){
	   debug("wap.wtp_resp", 0, "WTP_RESP: wrong version, aborting"
                 "transaction");
	       handle_wrong_version(event);
        }

        if (!event->u.RcvInvoke.ttr || !event->u.RcvInvoke.gtr){
            debug("wap.wtp_resp", 0, "WTP_RESP: no sar implemented," 
                  "aborting transaction");
            handle_no_sar(event);
        }
    }
}

/*
 * Checks whether wtp machines data structure includes a specific machine.
 * The machine in question is identified with with source and destination
 * address and port and tid.  First test incoming events (WTP 10.2)
 * (Exception is tests nro 4 and 5: if we have a memory error, we panic. Nro 5 
 * is already checked)  If event was validated and if the machine does not 
 * exist and the event is RcvInvoke, a new machine is created and added in 
 * the machines data structure. If the event was RcvAck or RcvAbort, the 
 * event is ignored (test nro 3). If the event is RcvErrorPDU (test nro 4) 
 * new machine is created for handling this event. If the event is one of WSP 
 * primitives, we have an error.
 */
static WTPRespMachine *resp_machine_find_or_create(WAPEvent *event)
{
    WTPRespMachine *resp_machine = NULL;
    long tid, mid;
    WAPAddrTuple *tuple;

    tid = -1;
    tuple = NULL;
    mid = -1;

    switch (event->type) {
    case RcvInvoke:
	if (erroneous_field_in(event)) {
	    handle_erroneous_field_in(event);
            return NULL;
        } else {
            tid = event->u.RcvInvoke.tid;
	    tuple = event->u.RcvInvoke.addr_tuple;
        }
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

    case TR_Invoke_Res:
	mid = event->u.TR_Invoke_Res.handle;
    break;

    case TR_Result_Req:
	mid = event->u.TR_Result_Req.handle;
    break;

    case TR_Abort_Req:
	mid = event->u.TR_Abort_Req.handle;
    break;

    case TimerTO_A:
	mid = event->u.TimerTO_A.handle;
    break;

    case TimerTO_R:
	mid = event->u.TimerTO_R.handle;
    break;

    case TimerTO_W:
	mid = event->u.TimerTO_W.handle;
    break;

    default:
        debug("wap.wtp", 0, "WTP: resp_machine_find_or_create:"
             "unhandled event"); 
        wap_event_dump(event);
        return NULL;
    }

    gw_assert(tuple != NULL || mid != -1);
    resp_machine = resp_machine_find(tuple, tid, mid);
           
    if (resp_machine == NULL){
        switch (event->type){
/*
 * When PDU with an illegal header is received, its tcl-field is irrelevant 
 * (and possibly meaningless). In this case we must create a new machine, if 
 * there is any. There is a machine for all events handled stateful manner.
 */
	case RcvErrorPDU:
	    debug("wap.wtp_resp", 0, "an erronous pdu received");
            wap_event_dump(event);
            resp_machine = resp_machine_create(tuple, tid, 
                                               event->u.RcvInvoke.tcl); 
	break;
           
        case RcvInvoke:
	    resp_machine = resp_machine_create(tuple, tid, 
                                               event->u.RcvInvoke.tcl);
        break;
/*
 * This and the following branch implement test nro 3 in WTP 10.2.
 */
	case RcvAck: 
	    info(0, "WTP_RESP: resp_machine_find_or_create:"
                 " ack received, yet having no machine");
        break;

        case RcvAbort: 
	     info(0, "WTP_RESP: resp_machine_find_or_create:"
                 " abort received, yet having no machine");
        break;

	case TR_Invoke_Res: 
        case TR_Result_Req: 
        case TR_Abort_Req:
	    error(0, "WTP_RESP: resp_machine_find_or_create: WSP primitive to"
                  " a wrong WTP machine");
	break;

	case TimerTO_A: 
        case TimerTO_R: 
        case TimerTO_W:
            error(0, "WTP_RESP: resp_machine_find_or_create: timer event"
                  " without a corresponding machine");
        break;
                 
	default:
            error(0, "WTP_RESP: resp_machine_find_or_create: unhandled event");
            wap_event_dump(event);
	break;
        }
   } /* if machine == NULL */   
   return resp_machine;
}

static int is_wanted_resp_machine(void *a, void *b) 
{
    machine_pattern *pat;
    WTPRespMachine *m;
	
    m = a;
    pat = b;

    if (m->mid == pat->mid)
	return 1;

    if (pat->mid != -1)
	return 0;

    return m->tid == pat->tid && 
           wap_addr_tuple_same(m->addr_tuple, pat->tuple);
}


static WTPRespMachine *resp_machine_find(WAPAddrTuple *tuple, long tid, 
                                         long mid) 
{
    machine_pattern pat;
    WTPRespMachine *m;
	
    pat.tuple = tuple;
    pat.tid = tid;
    pat.mid = mid;
	
    m = list_search(resp_machines, &pat, is_wanted_resp_machine);
    return m;
}


static WTPRespMachine *resp_machine_create(WAPAddrTuple *tuple, long tid, 
                                           long tcl) 
{
    WTPRespMachine *resp_machine;
	
    resp_machine = gw_malloc(sizeof(WTPRespMachine)); 
        
    #define ENUM(name) resp_machine->name = LISTEN;
    #define EVENT(name) resp_machine->name = NULL;
    #define INTEGER(name) resp_machine->name = 0; 
    #define TIMER(name) resp_machine->name = gwtimer_create(resp_queue); 
    #define ADDRTUPLE(name) resp_machine->name = NULL; 
    #define MACHINE(field) field
    #include "wtp_resp_machine.def"

    list_append(resp_machines, resp_machine);

    resp_machine->mid = counter_increase(resp_machine_id_counter);
    resp_machine->addr_tuple = wap_addr_tuple_duplicate(tuple);
    resp_machine->tid = tid;
    resp_machine->tcl = tcl;
	
    debug("wap.wtp", 0, "WTP: Created WTPRespMachine %p (%ld)", 
	  (void *) resp_machine, resp_machine->mid);

    return resp_machine;
} 


/*
 * Destroys a WTPRespMachine. Assumes it is safe to do so. Assumes it has 
 * already been deleted from the machines list.
 */
static void resp_machine_destroy(void * p)
{
    WTPRespMachine *resp_machine;

    resp_machine = p;
    debug("wap.wtp", 0, "WTP: Destroying WTPRespMachine %p (%ld)", 
	  (void *) resp_machine, resp_machine->mid);
	
    list_delete_equal(resp_machines, resp_machine);
        
    #define ENUM(name) resp_machine->name = LISTEN;
    #define EVENT(name) wap_event_destroy(resp_machine->name);
    #define INTEGER(name) resp_machine->name = 0; 
    #define TIMER(name) gwtimer_destroy(resp_machine->name); 
    #define ADDRTUPLE(name) wap_addr_tuple_destroy(resp_machine->name); 
    #define MACHINE(field) field
    #include "wtp_resp_machine.def"
    gw_free(resp_machine);
}

/*
 * Create a TR-Invoke.ind event.
 */
static WAPEvent *create_tr_invoke_ind(WTPRespMachine *sm, Octstr *user_data) 
{
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
static WAPEvent *create_tr_result_cnf(WTPRespMachine *sm) 
{
    WAPEvent *event;
	
    event = wap_event_create(TR_Result_Cnf);
    event->u.TR_Result_Cnf.addr_tuple = 
	wap_addr_tuple_duplicate(sm->addr_tuple);
    event->u.TR_Result_Cnf.handle = sm->mid;
    return event;
}

/*
 * Creates TR-Abort.ind event from a responder state machine. In addition, set
 * the responder indication flag.
 */
static WAPEvent *create_tr_abort_ind(WTPRespMachine *sm, long abort_reason) {
    WAPEvent *event;
	
    event = wap_event_create(TR_Abort_Ind);
    event->u.TR_Abort_Ind.abort_code = abort_reason;
    event->u.TR_Abort_Ind.addr_tuple = 
	wap_addr_tuple_duplicate(sm->addr_tuple);
    event->u.TR_Abort_Ind.handle = sm->mid;
    event->u.TR_Abort_Ind.ir_flag = RESPONDER_INDICATION;

    return event;
}

/*
 * Start acknowledgement interval timer
 */
static void start_timer_A(WTPRespMachine *machine) 
{
    WAPEvent *timer_event;

    timer_event = wap_event_create(TimerTO_A);
    timer_event->u.TimerTO_A.handle = machine->mid;
    gwtimer_start(machine->timer, L_A_WITH_USER_ACK, timer_event);
}

/*
 * Start retry interval timer
 */
static void start_timer_R(WTPRespMachine *machine) 
{
    WAPEvent *timer_event;

    timer_event = wap_event_create(TimerTO_R);
    timer_event->u.TimerTO_R.handle = machine->mid;
    gwtimer_start(machine->timer, L_R_WITH_USER_ACK, timer_event);
}

/*
 * Start timeout interval timer
 */
static void start_timer_W(WTPRespMachine *machine)
{
    WAPEvent *timer_event;

    timer_event = wap_event_create(TimerTO_W);
    timer_event->u.TimerTO_W.handle = machine->mid;
    gwtimer_start(machine->timer, W_WITH_USER_ACK, timer_event);
}

static void send_abort(WTPRespMachine *machine, long type, long reason)
{
    WAPEvent *e;

    e = wtp_pack_abort(type, reason, machine->tid, machine->addr_tuple);
    dispatch_to_wdp(e);
}

static void send_ack(WTPRespMachine *machine, long ack_type, int rid_flag)
{
    WAPEvent *e;

    e = wtp_pack_ack(ack_type, rid_flag, machine->tid, machine->addr_tuple);
    dispatch_to_wdp(e);
}
