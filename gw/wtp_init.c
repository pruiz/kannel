/*
 * wtp_init.c - WTP initiator implementation
 *
 * By Aarno Syvänen for Wapit Ltd
 */

#include "gwlib/gwlib.h"
#include "wtp_init.h"

/*****************************************************************************
 * Internal data structures.
 *
 * List of initiator WTP machines
 */
static List *init_machines = NULL;

/*
 * Counter for initiator WTP machine id numbers, to make sure they are unique.
 */
static Counter *init_machine_id_counter = NULL;

/*
 * When we restart an iniator, we must set tidnew flag to avoid excessive tid
 * validations (WTP 8.8.3.2). Only an iniator uses this flag.
 */
static int tidnew = 1;

/*
 * Queue of events to be handled by WTP initiator.
 */
static List *queue = NULL;

/*
 * Give the status of the wtp initiator:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } initiator_run_status = limbo;

/***************************************************************************
 *
 * Prototypes for internal functions:
 */
static void main_thread(void *arg);
 
/*
 * Create and destroy an uniniatilised wtp initiator state machine
 */
static WTPInitMachine *init_machine_create(WAPAddrTuple *tuple, unsigned short
                                           tid, int tidnew);
static void init_machine_destroy(void *sm);
static void handle_init_event(WTPInitMachine *machine, WAPEvent *event);

/*
 * Checks whether wtp initiator machines data structure includes a specific 
 * machine.
 * The machine in question is identified with with source and destination
 * address and port and tid. 
 */
static WTPInitMachine *init_machine_find_or_create(WAPEvent *event);
static WTPInitMachine *find_init_machine_using_mid(long mid);

/*
 * Creates TR-Abort.ind event.
 */
static WAPEvent *create_tr_abort_ind(WTPInitMachine *sm, long abort_reason);

/*
 * Creates TR-Invoke.cnf event 
 */
static WAPEvent *create_tr_invoke_cnf(WTPInitMachine *machine);
static int tid_wrapped(unsigned short new_tid, unsigned short old_tid);

/*
 * We use RcvTID consistently as a internal tid representation. So newly 
 * created tids are converted. SendTID = RcvTID ^ 0x8000 (WTP 10.4.3) and for 
 * an initiator, GenTID = SendTID (WTP 10.5). 
 */
static unsigned short rcv_tid(unsigned short tid);
static void start_initiator_timer_R(WTPInitMachine *machine); 
static void stop_initiator_timer(Timer *timer);

/**************************************************************************
 *
 * EXTERNAL FUNCTIONS
 */

void wtp_initiator_init(void) 
{
    init_machines = list_create();
    init_machine_id_counter = counter_create();
     
    queue = list_create();
    list_add_producer(queue);

    gw_assert(initiator_run_status == limbo);
    initiator_run_status = running;
    gwthread_create(main_thread, NULL);
}

void wtp_initiator_shutdown(void) 
{
    gw_assert(initiator_run_status == running);
    initiator_run_status = terminating;
    list_remove_producer(queue);
    gwthread_join_every(main_thread);

    debug("wap.wtp", 0, "wtp_initiator_shutdown: %ld init_machines left",
     	  list_len(init_machines));
    list_destroy(init_machines, init_machine_destroy);
    list_destroy(queue, wap_event_destroy_item);

    counter_destroy(init_machine_id_counter);
}

void wtp_initiator_dispatch_event(WAPEvent *event) 
{
    list_produce(queue, event);
}

int wtp_initiator_get_address_tuple(long mid, WAPAddrTuple **tuple) 
{
    WTPInitMachine *sm;
	
    sm = find_init_machine_using_mid(mid);
    if (sm == NULL)
	return -1;

    *tuple = wap_addr_tuple_duplicate(sm->addr_tuple);

    return 0;
}

/**************************************************************************
 *
 * INTERNAL FUNCTIONS:
 */

static void main_thread(void *arg) 
{
    WTPInitMachine *sm;
    WAPEvent *e;

    while (initiator_run_status == running && 
          (e = list_consume(queue)) != NULL) {
        sm = init_machine_find_or_create(e);
	if (sm == NULL)
	    wap_event_destroy(e);
	else
	    handle_init_event(sm, e);
    }
}

static WTPInitMachine *init_machine_create(WAPAddrTuple *tuple, unsigned short
                                           tid, int tidnew)
{
     WTPInitMachine *init_machine;
	
     init_machine = gw_malloc(sizeof(WTPInitMachine)); 
        
     #define ENUM(name) init_machine->name = INITIATOR_NULL_STATE;
     #define INTEGER(name) init_machine->name = 0; 
     #define MSG(name) init_machine->name = msg_create(wdp_datagram); 
     #define TIMER(name) init_machine->name = gwtimer_create(queue); 
     #define ADDRTUPLE(name) init_machine->name = NULL; 
     #define MACHINE(field) field
     #include "wtp_init_machine-decl.h"

     list_append(init_machines, init_machine);

     init_machine->mid = counter_increase(init_machine_id_counter);
     init_machine->addr_tuple = wap_addr_tuple_duplicate(tuple);
     init_machine->tid = tid;
     init_machine->tidnew = tidnew;
	
     debug("wap.wtp", 0, "WTP: Created WTPInitMachine %p (%ld)", 
	   (void *) init_machine, init_machine->mid);

     return init_machine;
}

/*
 * Destroys a WTPInitMachine. Assumes it is safe to do so. Assumes it has 
 * already been deleted from the machines list.
 */
static void init_machine_destroy(void *p)
{
     WTPInitMachine *init_machine;

     init_machine = p;
     debug("wap.wtp", 0, "WTP: Destroying WTPInitMachine %p (%ld)", 
	    (void *) init_machine, init_machine->mid);
	
     list_delete_equal(init_machines, init_machine);
        
     #define ENUM(name) init_machine->name = INITIATOR_NULL_STATE;
     #define INTEGER(name) init_machine->name = 0; 
     #define MSG(name) msg_destroy(init_machine->name); 
     #define TIMER(name) gwtimer_destroy(init_machine->name); 
     #define ADDRTUPLE(name) wap_addr_tuple_destroy(init_machine->name); 
     #define MACHINE(field) field
     #include "wtp_init_machine-decl.h"
     gw_free(init_machine);
}

/*
 * Give the name of an initiator state in a readable form. 
 */
static unsigned char *name_init_state(int s)
{
       switch (s){
       #define INIT_STATE_NAME(state) case state: return #state;
       #define ROW(state, event, condition, action, new_state)
       #include "wtp_init_state-decl.h"
       default:
           return "unknown state";
       }
}

/*
 * Feed an event to a WTP initiator state machine. Handle all errors by do not
 * report them to the caller. WSP indication or conformation is handled by an
 * included state table. Note: Do not put {}s of the else block inside the 
 * macro definition . 
 */
static void handle_init_event(WTPInitMachine *init_machine, WAPEvent *event)
{
     WAPEvent *wsp_event = NULL;

     debug("wap.wtp", 0, "WTP_INIT: initiator machine %ld, state %s,"
           " event %s.", 
	   init_machine->mid, 
	   name_init_state(init_machine->state), 
	   wap_event_name(event->type));
       
     #define INIT_STATE_NAME(state)
     #define ROW(init_state, event_type, condition, action, next_state) \
	 if (init_machine->state == init_state && \
	     event->type == event_type && \
	     (condition)) { \
	     action \
	     init_machine->state = next_state; \
	     debug("wap.wtp", 0, "WTP_INIT %ld: New state %s", \
                   init_machine->mid, #next_state); \
	 } else 
      #include "wtp_init_state-decl.h"
	 {
	     error(1, "WTP_INIT: handle_init_event: unhandled event!");
	     debug("wap.wtp.init", 0, "WTP_INIT: handle_init_event:"
                   "Unhandled event was:");
	     wap_event_dump(event);
             wap_event_destroy(event);
             return;
	 }

      if (event != NULL) {
	  wap_event_destroy(event);  
      }

      if (init_machine->state == INITIATOR_NULL_STATE)
     	  init_machine_destroy(init_machine);      
}

static int is_wanted_init_machine(void *a, void *b) 
{
    struct machine_pattern *pat;
    WTPInitMachine *m;
	
    m = a;
    pat = b;

    if (m->mid == pat->mid)
	return 1;

    if (pat->mid != -1)
	return 0;

    return m->tid == pat->tid && 
	   wap_addr_tuple_same(m->addr_tuple, pat->tuple);
}

static WTPInitMachine *init_machine_find(WAPAddrTuple *tuple, long tid, 
                                         long mid) 
{
    struct machine_pattern pat;
    WTPInitMachine *m;
	
    pat.tuple = tuple;
    pat.tid = tid;
    pat.mid = mid;
	
    m = list_search(init_machines, &pat, is_wanted_init_machine);
    return m;
}

/*
 * Checks whether wtp initiator machines data structure includes a specific 
 * machine. The machine in question is identified with with source and 
 * destination address and port and tid.  First test incoming events 
 * (WTP 10.2) (Exception are tests nro 4 and 5: if we have a memory error, 
 * we panic (nro 4); nro 5 is already checked). If we have an ack with tid 
 * verification flag set and no corresponding transaction, we abort.(case nro 
 * 2). If the event was a normal ack or an abort, it is ignored (error nro 3).
 * In the case of TR-Invoke.req a new machine is created, in the case of 
 * TR-Abort.req we have a serious error. We must create a new tid for a new
 * transaction here, because machines are identified by an address tuple and a
 * tid. This tid is GenTID (WTP 10.4.2), which is used only by the iniator 
 * thread.
 * Note that as internal tid representation, module uses RcvTID (as required
 * by module wtp_send). So we we turn the first bit of the tid stored by the
 * init machine.
 */
static WTPInitMachine *init_machine_find_or_create(WAPEvent *event)
{
    WTPInitMachine *machine = NULL;
    long mid;
    static unsigned short old_tid = 0;
    static unsigned short tid = -1;
    WAPAddrTuple *tuple;

    mid = -1;
    tuple = NULL;

    switch (event->type) {
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
/*
 * When we are receiving an invoke requirement, we must create a new trans-
 * action and generate a new tid. This can be wrapped, and should have its 
 * first bit turned.
 */
    case TR_Invoke_Req:
        old_tid = tid;
	++tid;
        if (tid_wrapped(tid, old_tid))
	    tidnew = 1;
                   
        tid = rcv_tid(tid);
	tuple = event->u.TR_Invoke_Req.addr_tuple;
        mid = event->u.TR_Invoke_Req.handle;
    break;

    case TR_Abort_Req:
        mid = event->u.TR_Abort_Req.handle;
    break;

    case TimerTO_R:
	mid = event->u.TimerTO_A.handle;
    break;

    default:
	error(0, "WTP_INIT: machine_find_or_create, unhandled"
              "event");
        wap_event_dump(event);
        return NULL;
    }

    gw_assert(tuple != NULL || mid != -1);
    machine = init_machine_find(tuple, tid, mid);

    if (machine == NULL){

	switch (event->type){
	case RcvAck:   
   
/* 
 * Case nro 2 If we do not have a tid asked for, we send a negative answer, 
 * i.e. an abort with reason INVALIDTID. 
 */
	     if (event->u.RcvAck.tid_ok)
		 wtp_send_abort(PROVIDER, INVALIDTID, tid, tuple);

/* Case nro 3, normal ack */
             else
                 info(0, "WTP_INIT: machine_find_or_create: ack "
                     "received, yet having no machine");
	break;

/* Case nro 3, abort */
        case RcvAbort:
            info(0, "WTP_INIT: machine_find_or_create: abort "
                 "received, yet having no machine");
	break;

	case TR_Invoke_Req:
	    machine = init_machine_create(tuple, tid, tidnew);
            machine->mid = event->u.TR_Invoke_Req.handle;
	break;

	case TR_Abort_Req:
            error(0, "WTP_INIT: machine_find_or_create: WSP "
                  "primitive to a wrong WTP machine");
	break;

	case TimerTO_R:
	    error(0, "WTP_INIT: machine_find_or_create: timer "
                       "event without a corresponding machine");
        break;
       
        default:
            error(0, "WTP_INIT: machine_find_or_create: unhandled"
                  "event");
            wap_event_dump(event);
        break; 
        }
   } /* if machine == NULL */

   return machine;
}

/*
 * Creates TR-Invoke.cnf event
 */
static WAPEvent *create_tr_invoke_cnf(WTPInitMachine *init_machine)
{
    WAPEvent *event;

    gw_assert(init_machine != NULL);
    event = wap_event_create(TR_Invoke_Cnf);
    event->u.TR_Invoke_Cnf.handle = init_machine->mid;

    return event;
}

/*
 * Creates TR-Abort.ind event from an initiator state machine. 
 */
static WAPEvent *create_tr_abort_ind(WTPInitMachine *sm, long abort_reason) 
{
    WAPEvent *event;
	
    event = wap_event_create(TR_Abort_Ind);

    event->u.TR_Abort_Ind.abort_code = abort_reason;
    event->u.TR_Abort_Ind.addr_tuple = 
	wap_addr_tuple_duplicate(sm->addr_tuple);
    event->u.TR_Abort_Ind.handle = sm->mid;

    return event;
}


static int init_machine_has_mid(void *a, void *b) 
{
    WTPInitMachine *sm;
    long mid;
	
    sm = a;
    mid = *(long *) b;
    return sm->mid == mid;
}

static WTPInitMachine *find_init_machine_using_mid(long mid) 
{
    return list_search(init_machines, &mid, init_machine_has_mid);
}

static int tid_wrapped(unsigned short new_tid, unsigned short old_tid)
{
    return new_tid < old_tid;
}

static unsigned short rcv_tid(unsigned short tid)
{
    return tid ^ 0x8000;
}

/*
 * Start retry interval timer (strictly speaking, timer iniatilised with retry
 * interval).
 */
static void start_initiator_timer_R(WTPInitMachine *machine) 
{
    WAPEvent *timer_event;
    int seconds;

    timer_event = wap_event_create(TimerTO_R);
    timer_event->u.TimerTO_R.handle = machine->mid;
    if (machine->u_ack)
        seconds = S_R_WITH_USER_ACK;
    else
        seconds = S_R_WITHOUT_USER_ACK;
    gwtimer_start(machine->timer, seconds, timer_event);
}

static void stop_initiator_timer(Timer *timer)
{
    debug("wap.wtp_init", 0, "stopping timer");
    gw_assert(timer);
    gwtimer_stop(timer);
}
