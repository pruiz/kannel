/*
 * Push PPG implementation. This module implements the general logic of a push
 * proxy gateway, as specified in WAP PPG Service.
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#include <time.h>
#include <ctype.h>

#include "wap_push_ppg.h"
#include "gwlib/gwlib.h"
#include "wap/wap_events.h"
#include "wap/wsp_caps.h"
#include "wml_compiler.h"
#include "wap-appl.h"
#include "wap/wsp.h"
#include "wap/wsp_strings.h"

enum {
    TIME_EXPIRED = 0,
    TIME_TOO_EARLY = 1,
    NO_CONSTRAINTS = 2
};

/*****************************************************************************
 *
 * Internal data structures
 */

/*
 * Give the status of the push ppg module:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum {limbo, running, terminating} run_status = limbo;

/*
 * The event queue for this module
 */
static List *ppg_queue = NULL;

/*
 * List of ppg session machines (it is, of currently active sessions)
 */
static List *ppg_machines = NULL;

/*
 * List of currently active unit pushes (we need a threadsafe storage for them,
 * because pushes can be cancelled and queried):
 */
static List *ppg_unit_pushes = NULL;

/*
 * Counter to store our internal push id.
 */
static Counter *push_id_counter = NULL;

/*
 * Push content packed for compilers (wml, si, sl, co).
 */
struct content {
    Octstr *body;
    Octstr *type;
    Octstr *charset;
};

wap_dispatch_func_t *dispatch_to_ota;
wap_dispatch_func_t *dispatch_to_pap;
wap_dispatch_func_t *dispatch_to_appl;

/*****************************************************************************
 *
 * Prototypes of internal functions
 *
 * Event handling
 */
static void main_thread(void *arg);
static void handle_ppg_event(WAPEvent *e);

/*
 * Constructors and destructors for machines.
 */
static PPGSessionMachine *session_machine_create(WAPAddrTuple *tuple, 
                                                     WAPEvent *e);
static void session_machine_destroy(void *p);
static PPGPushMachine *push_machine_create(WAPEvent *e, 
    WAPAddrTuple *tuple);
static void push_machine_destroy(void *pm);
static void push_machines_list_destroy(List *pl);

/*
 * Communicating with other modules
 */
static void create_session(WAPEvent *e, PPGPushMachine *pm);
static void request_confirmed_push(long last, PPGPushMachine *pm, 
                                   PPGSessionMachine *sm);
static void request_unit_push(long last, PPGPushMachine *pm);
static void request_push(long last, PPGPushMachine *sm);
#if 0
static void request_abort(long reason, long push_id, long session_id);
#endif
static int response_push_connection(WAPEvent *e, PPGSessionMachine *sm);
static void response_push_message(PPGPushMachine *pm, long code);
#if 0
static void send_progress_note(Octstr *stage, Octstr *note);
#endif

/*
 * Functions to find machines using various identifiers, and related help 
 * functions.
 */
static PPGSessionMachine *session_find_using_pi_client_address(Octstr *addr);
static PPGPushMachine *find_ppg_push_machine_using_pid(PPGSessionMachine *sm, 
                                                   long pid);
static PPGPushMachine *find_ppg_push_machine_using_pi_push_id(
    PPGSessionMachine *sm, Octstr *pi_push_id);
static PPGPushMachine *find_unit_ppg_push_machine_using_pi_push_id(
    Octstr *pi_push_id);
static int push_has_pi_push_id(void *a, void *b);
static int push_has_pid(void *a, void *b);
static int session_has_pi_client_address(void *a, void *b);
static int session_has_addr(void *a, void *b);
static int session_has_sid(void *a, void *b);

/*
 * Main logic of PPG.
 */
static int check_capabilities(List *requested, List *assumed);
static int transform_message(WAPEvent **e, WAPAddrTuple **tuple, 
                             int connected, Octstr **type);
static void check_x_wap_application_id_header(List **push_headers);
static int convert_content(struct content *content);
static int select_bearer_network(WAPEvent *e);
static int delivery_time_constraints(WAPEvent *e, PPGPushMachine *pm);
static void deliver_confirmed_push(long last, PPGPushMachine *pm, 
                                   PPGSessionMachine *sm);
static PPGPushMachine *deliver_unit_push(long last, PPGPushMachine *pm,
    PPGSessionMachine *sm, int session_exists);
static PPGPushMachine *store_push_data(PPGSessionMachine *sm, WAPEvent *e, 
    WAPAddrTuple *tuple, int cless);
static PPGPushMachine *update_push_data_with_attribute(PPGSessionMachine **sm, 
    PPGPushMachine *pm, long reason, long status);
static void remove_push_data(PPGSessionMachine *sm, PPGPushMachine *pm, 
                             int cless);
static void remove_session_data(PPGSessionMachine *sm);
static void remove_pushless_session(PPGSessionMachine *sm);
static PPGSessionMachine *store_session_data(PPGSessionMachine *sm,
    WAPEvent *e, WAPAddrTuple *tuple, int *session_exists);
static PPGSessionMachine *update_session_data_with_headers(
    PPGSessionMachine *sm, PPGPushMachine *pm);
static void deliver_pending_pushes(PPGSessionMachine *sm, int last);
static PPGPushMachine *abort_delivery(PPGSessionMachine *sm);
static PPGSessionMachine *update_session_data(PPGSessionMachine *sm, long sid,
                                              long port, List *caps);
static int confirmation_requested(WAPEvent *e);
static int cless_accepted(WAPEvent *e, PPGSessionMachine *sm);

/*
 * Various utility functions
 */
static Octstr *set_time(void);
static int deliver_before_test_cleared(Octstr *before, struct tm now);
static int deliver_after_test_cleared(Octstr *after, struct tm now);
static void session_machine_assert(PPGSessionMachine *sm);
static void push_machine_assert(PPGPushMachine *pm);
static Octstr *tell_ppg_name(void);
static Octstr *describe_code(long code);
static long ota_abort_to_pap(long reason);
static int content_transformable(List *push_headers);
static WAPAddrTuple *set_addr_tuple(Octstr *address, long cliport, 
                                    long servport);
static WAPAddrTuple *addr_tuple_change_cliport(WAPAddrTuple *tuple, long port);
static Octstr *convert_wml_to_wmlc(struct content *content);
static void initialize_time_item_array(long time_data[], struct tm now);
static int date_item_compare(Octstr *before, long time_data, long pos);

/*
 * Parsing functions
 */
static long accept_escaped(Octstr **address, long pos);
static long accept_safe(Octstr **address, long pos);
static long rest_unescaped(Octstr **address, long pos);
static int create_peek_window(Octstr **address, long *pos);
static int issafe(Octstr **address, long pos);
static long drop_character(Octstr **address, long pos);
static Octstr *prepend_char(Octstr *address, unsigned char c);
static long handle_two_terminators(Octstr **adress, long pos, 
    unsigned char comma, unsigned char point, unsigned char c, 
    long fragment_parsed, long fragment_length);
static long parse_ipv6_fragment(Octstr **address, long pos);
static long parse_ipv4_fragment(Octstr **address, long pos);
static long parse_type(Octstr **address, Octstr **type_value, long pos);
static long parse_ext_qualifiers(Octstr **address, long pos, Octstr *type);
static long parse_global_phone_number(Octstr **address, long pos);
static long parse_ipv4(Octstr **address, long pos);
static long parse_ipv6(Octstr **address, long pos);
static long parse_escaped_value(Octstr **address, long pos);
static long parse_client_specifier(Octstr **address, long pos);
static long parse_constant(const char *field_name, Octstr **address, long pos);
static long parse_wappush_client_address(Octstr **address, long pos);
static long parse_ppg_specifier(Octstr **address, long pos);
static long parse_client_address(Octstr **cliaddr);
static int wina_bearer_identifier(Octstr *type_value);
static int qualifiers(Octstr *address, long pos, Octstr *type);
static long parse_qualifier_keyword(Octstr **address, long pos);
static long parse_qualifier_value(Octstr **address, long pos);
static long parse_dom_fragment(Octstr **address, long pos);
static void parse_appid_header(Octstr **assigned_code);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS
 */

void wap_push_ppg_init(wap_dispatch_func_t *ota_dispatch, 
                       wap_dispatch_func_t *pap_dispatch,
                       wap_dispatch_func_t *appl_dispatch)
{
    ppg_queue = list_create();
    list_add_producer(ppg_queue);
    push_id_counter = counter_create();
    ppg_machines = list_create();
    ppg_unit_pushes = list_create();

    dispatch_to_ota = ota_dispatch;
    dispatch_to_pap = pap_dispatch;
    dispatch_to_appl = appl_dispatch;

    gw_assert(run_status == limbo);
    run_status = running;
    gwthread_create(main_thread, NULL);
}

void wap_push_ppg_shutdown(void)
{
     gw_assert(run_status == running);
     run_status = terminating;
     list_remove_producer(ppg_queue);
     
     gwthread_join_every(main_thread);

     list_destroy(ppg_queue, wap_event_destroy_item);
     counter_destroy(push_id_counter);
     
     debug("wap.push.ppg", 0, "PPG: %ld push session machines left.",
           list_len(ppg_machines));
     list_destroy(ppg_machines, session_machine_destroy);

     debug("wap_push_ppg", 0, "PPG: %ld unit pushes undone", 
           list_len(ppg_unit_pushes));
     list_destroy(ppg_unit_pushes, push_machine_destroy);
}

void wap_push_ppg_dispatch_event(WAPEvent *e)
{
    gw_assert(run_status == running);
    list_produce(ppg_queue, e);
}

/*
 * We cannot know port the client is using when it establish the connection.
 * However, we must link session creation with a pending push request. Only
 * data available is the client address, so we check it here.
 * Return non-NULL (pointer to the session machine found), if we have one.
 */
PPGSessionMachine *wap_push_ppg_have_push_session_for(WAPAddrTuple *tuple)
{
    PPGSessionMachine *sm;

    gw_assert(tuple);
    sm = list_search(ppg_machines, tuple->remote->address, session_has_addr);

    return sm;
}

/*
 * Now initiators are identified by their session id. Return non-NULL (pointer
 * to the session machine found), if we have one. This function are used after 
 * wsp has indicated session establishment, giving us a session id.
 */
PPGSessionMachine *wap_push_ppg_have_push_session_for_sid(long sid)
{
    PPGSessionMachine *sm;

    gw_assert(sid >= 0);
    sm = list_search(ppg_machines, &sid, session_has_sid);

    return sm;
}

/*****************************************************************************
 *
 * INTERNAL FUNCTIONS
 */

static void main_thread(void *arg)
{
    WAPEvent *e;

    while (run_status == running && (e = list_consume(ppg_queue)) != NULL) {
        handle_ppg_event(e);
    } 
}

static void handle_ppg_event(WAPEvent *e)
{
    int cless,
        session_exists,
        bearer_supported,
        dummy,
        constraints,
        message_transformable;
    long sid,
         pid,
         reason,
         port;

    PPGPushMachine *pm;
    PPGSessionMachine *sm;
    WAPAddrTuple *tuple;
    List *caps;
    Octstr *push_data,
           *cliaddr,
           *type;
        
    switch (e->type) {
/*
 * Operations needed when push proxy gateway receives a new push message are 
 * defined in PPG Services, Chapter 6.
 */
    case Push_Message:
        debug("wap.push.ppg", 0, "PPG: have a push request from pap");
        push_data = e->u.Push_Message.push_data;
        cliaddr = e->u.Push_Message.address_value;
        session_exists = 0;

        sm = session_find_using_pi_client_address(cliaddr);
        cless = cless_accepted(e, sm);
        message_transformable = transform_message(&e, &tuple, cless, &type);

        if (!sm && !cless) {
            sm = store_session_data(sm, e, tuple, &session_exists); 
        }

        if ((pm = store_push_data(sm, e, tuple, cless)) == NULL) {
            warning(0, "PPG: we had a duplicate push id");
            response_push_message(pm, DUPLICATE_PUSH_ID);
            goto no_start;
        }

        if (!message_transformable) {
	    pm = update_push_data_with_attribute(&sm, pm, 
                 TRANSFORMATION_FAILURE, UNDELIVERABLE1);  
            if (tuple != NULL)   
	        response_push_message(pm, TRANSFORMATION_FAILURE);
            else
	        response_push_message(pm, ADDRESS_ERROR);
            goto no_start;
        }

        dummy = 0;
        pm = update_push_data_with_attribute(&sm, pm, dummy, PENDING);

        bearer_supported = select_bearer_network(e);
        if (!bearer_supported) {
            pm = update_push_data_with_attribute(&sm, pm, dummy, 
                 UNDELIVERABLE2);
            response_push_message(pm, REQUIRED_BEARER_NOT_AVAIBLE);
	    goto no_start;
        }
     
        if ((constraints = delivery_time_constraints(e, pm)) == TIME_EXPIRED) {
            pm = update_push_data_with_attribute(&sm, pm, FORBIDDEN, EXPIRED);
            response_push_message(pm, FORBIDDEN);
	    goto no_start;
        }
/*
 * If time is to early for delivering the push message, we do not remove push
 * data. We response PI here, so that "accepted for processing" means "no 
 * error messages to come".
 */
        response_push_message(pm, ACCEPTED_FOR_PROCESSING);
        info(0, "PPG: push message accepted for processing");

        if (constraints == TIME_TOO_EARLY)
	    goto store_push;

        if (constraints == NO_CONSTRAINTS) {
	    http_header_mark_transformation(pm->push_headers,
                                            pm->push_data, type);
            if (sm)
                sm = update_session_data_with_headers(sm, pm); 

            if (!confirmation_requested(e)) {
                pm = deliver_unit_push(NOT_LAST, pm, sm, session_exists);
                goto unit_push_delivered;
	    } 
	      
            if (session_exists) {
                deliver_confirmed_push(NOT_LAST, pm, sm);   
            } else {  
	        http_header_remove_all(e->u.Push_Message.push_headers, 
                                       "Content-Type");  
                create_session(e, pm);
            }
        }

        wap_addr_tuple_destroy(tuple);
        octstr_destroy(type);
        wap_event_destroy(e);
        return;
unit_push_delivered:
        wap_addr_tuple_destroy(tuple);
        remove_push_data(sm, pm, cless);
        octstr_destroy(type);
        wap_event_destroy(e);
        return;
store_push:
        wap_addr_tuple_destroy(tuple);
        octstr_destroy(type);
        wap_event_destroy(e);
        return;
no_start:
       wap_addr_tuple_destroy(tuple);
       remove_push_data(sm, pm, cless);
       if (sm)
           remove_pushless_session(sm);
       octstr_destroy(type);
       wap_event_destroy(e);
       return;
/*
 * PAP, Chapter 11.1.3 states that if client is incapable, we should abort the
 * push and inform PI. We do this here.
 * In addition, we store session id used as an alias for address tuple and do
 * all pushes pending for this initiator (or abort them).
 */
    case Pom_Connect_Ind:
         debug("wap.push.ppg", 0, "PPG: having connect indication from OTA");
         sid = e->u.Pom_Connect_Ind.session_id;
         tuple = e->u.Pom_Connect_Ind.addr_tuple;
         port = tuple->remote->port;
         caps = e->u.Pom_Connect_Ind.requested_capabilities;

         sm = wap_push_ppg_have_push_session_for(tuple);
         sm = update_session_data(sm, sid, port, caps);
        
         if (!response_push_connection(e, sm)) {
	     pm = abort_delivery(sm);
             wap_event_destroy(e);
             return;
         }

/* 
 * hard-coded until we have bearer control implemented
 */
         deliver_pending_pushes(sm, NOT_LAST);  
         wap_event_destroy(e);
    break;

    case Pom_Disconnect_Ind:
        debug("wap.push.ppg", 0, "PPG: having a disconnection indication from"
              " OTA");
        sm = wap_push_ppg_have_push_session_for_sid(
                 e->u.Pom_Disconnect_Ind.session_handle);
        remove_session_data(sm);
        wap_event_destroy(e);
    break;

/*
 * Only the client can close a session. So we leave session open, even when 
 * there are no active pushes. Note that we do not store PAP attribute very
 * long time. Point is that result notification message, if asked, will rep-
 * ort this fact to PI, after which there is no need to store it any more.
 */
    case Po_ConfirmedPush_Cnf:
        debug("wap.push.ppg", 0, "PPG: having push conformation from OTA");
        sid = e->u.Po_ConfirmedPush_Cnf.session_handle;
        pid = e->u.Po_ConfirmedPush_Cnf.server_push_id;

        sm = wap_push_ppg_have_push_session_for_sid(sid);
        pm = find_ppg_push_machine_using_pid(sm, pid);
        pm = update_push_data_with_attribute(&sm, pm, CONFIRMED, DELIVERED2);
        wap_event_destroy(e);
    break;

/*
 * Again, PAP attribute will be reported to PI by using result notification.
 */
    case Po_PushAbort_Ind:
        debug("wap.push.ppg", 0, "PPG: having abort indication from OTA");
        sid = e->u.Po_PushAbort_Ind.session_handle;
        pid = e->u.Po_PushAbort_Ind.push_id;

        sm = wap_push_ppg_have_push_session_for_sid(sid);
        pm = find_ppg_push_machine_using_pid(sm, pid);
        session_machine_assert(sm);
        push_machine_assert(pm);
        reason = e->u.Po_PushAbort_Ind.reason;
        reason = ota_abort_to_pap(reason);
        pm = update_push_data_with_attribute(&sm, pm, reason, ABORTED);
        remove_session_data(sm);
        wap_event_destroy(e);
    break;

/*
 * FIXME TRU: Add timeout ( a mandatory feature !)
 */
    default:
        debug("wap.ppg", 0, "PPG: handle_ppg_event: an unhandled event");
        wap_event_dump(e);
        wap_event_destroy(e);
    break;
    }
}

/*
 * We do not set session id here: it is told to us by wsp.
 * FIXME: Preferconfirmed value is hard coded to NOT_SPECIFIED
 */
static PPGSessionMachine *session_machine_create(WAPAddrTuple *tuple, 
                                                 WAPEvent *e)
{
    PPGSessionMachine *m;

    gw_assert(e->type = Push_Message);

    m = gw_malloc(sizeof(PPGSessionMachine));
    
    #define INTEGER(name) m->name = 0;
    #define OCTSTR(name) m->name = NULL;
    #define ADDRTUPLE(name) m->name = NULL;
    #define PUSHMACHINES(name) m->name = list_create();
    #define CAPABILITIES(name) m->name = NULL;
    #define MACHINE(fields) fields
    #include "wap_ppg_session_machine.def"

    m->pi_client_address = octstr_duplicate(e->u.Push_Message.address_value);
    m->addr_tuple = wap_addr_tuple_duplicate(tuple);
    m->assumed_capabilities = 
        wsp_cap_duplicate_list(e->u.Push_Message.pi_capabilities);
    m->preferconfirmed_value = NOT_SPECIFIED;    

    list_append(ppg_machines, m);
    debug("wap.push.ppg", 0, "PPG: Created PPGSessionMachine %ld",
          m->session_id);

    return m;
}

static void session_machine_destroy(void *p)
{
    PPGSessionMachine *sm;

    if (p == NULL)
        return;

    sm = p;
    debug("wap.push.ppg", 0, "PPG: destroying PPGSEssionMachine %ld", 
          sm->session_id);
    
    #define OCTSTR(name) octstr_destroy(sm->name);
    #define ADDRTUPLE(name) wap_addr_tuple_destroy(sm->name);
    #define INTEGER(name) sm->name = 0;
    #define PUSHMACHINES(name) push_machines_list_destroy(sm->name);
    #define CAPABILITIES(name) wsp_cap_destroy_list(sm->name);
    #define MACHINE(fields) fields
    #include "wap_ppg_session_machine.def"
    gw_free(sm);
}

/*
 * FIXME: PPG's trust policy (flags authenticated and trusted).
 * We return pointer to the created push machine and push id it uses.
 */
static PPGPushMachine *push_machine_create(WAPEvent *e, WAPAddrTuple *tuple)
{
    PPGPushMachine *m;

    m = gw_malloc(sizeof(PPGPushMachine));

    #define INTEGER(name) m->name = 0;
    #define OCTSTR(name) m->name = NULL;
    #define ADDRTUPLE(name) m->name = NULL;
    #define CAPABILITIES m->name = NULL;
    #define HTTPHEADER(name) m->name = NULL;
    #define MACHINE(fields) fields
    #include "wap_ppg_push_machine.def"

    m->addr_tuple = wap_addr_tuple_duplicate(tuple);
    m->pi_push_id = octstr_duplicate(e->u.Push_Message.pi_push_id);

    m->push_id = counter_increase(push_id_counter);
    m->delivery_method = e->u.Push_Message.delivery_method;
   
    if (e->u.Push_Message.deliver_after_timestamp)
        m->deliver_after_timestamp = 
            octstr_duplicate(e->u.Push_Message.deliver_after_timestamp);

    m->priority = e->u.Push_Message.priority;
    m->push_headers = http_header_duplicate(e->u.Push_Message.push_headers);

    if (e->u.Push_Message.push_data) 
        m->push_data = octstr_duplicate(e->u.Push_Message.push_data);

    m->network_required = e->u.Push_Message.network_required;

    if (e->u.Push_Message.network_required)
        m->network = octstr_duplicate(e->u.Push_Message.network);

    m->bearer_required = e->u.Push_Message.bearer_required;

    if (e->u.Push_Message.bearer_required)
        m->bearer = octstr_duplicate(e->u.Push_Message.bearer);

    m->progress_notes_requested = e->u.Push_Message.progress_notes_requested;

    if (e->u.Push_Message.ppg_notify_requested_to)
        m->ppg_notify_requested_to = 
            octstr_duplicate(e->u.Push_Message.ppg_notify_requested_to);
    debug("wap.push.ppg", 0, "PPG: push machine %ld created", m->push_id);

    return m;
}

/*
 * Contrary to the normal Kannel style, we do not remove from a list here. 
 * That because we now have two different push lists.
 */
static void push_machine_destroy(void *p)
{
    PPGPushMachine *pm;

    if (p == NULL)
        return;

    pm = p;

    debug("wap.push.ppg", 0, "PPG: destroying push machine %ld", 
          pm->push_id); 
    #define OCTSTR(name) octstr_destroy(pm->name);
    #define INTEGER(name)
    #define ADDRTUPLE(name) wap_addr_tuple_destroy(pm->name);
    #define CAPABILITIES(name) wap_cap_destroy_list(pm->name);
    #define HTTPHEADER(name) http_destroy_headers(pm->name);
    #define MACHINE(fields) fields
    #include "wap_ppg_push_machine.def"

    gw_free(p);
}

static void push_machines_list_destroy(List *machines)
{
    if (machines == NULL)
        return;

    list_destroy(machines, push_machine_destroy);
}

static int session_has_addr(void *a, void *b)
{
    Octstr *cliaddr;
    PPGSessionMachine *sm;

    cliaddr = b;
    sm = a;
    
    return octstr_compare(sm->addr_tuple->remote->address, cliaddr) == 0;
}

static int session_has_sid(void *a, void *b)
{
     PPGSessionMachine *sm;
     long *sid;

     sid = b;
     sm = a;

     return *sid == sm->session_id;
}

/*
 * Here session machine address tuples have connection-oriented ports, because
 * these are used when establishing the connection an doing pushes. But session
 * creation request must be to the the connectionless push port of the client.
 * So we change ports here.
 */
static void create_session(WAPEvent *e, PPGPushMachine *pm)
{
    WAPEvent *ota_event;
    List *push_headers;

    gw_assert(e->type == Push_Message);
    push_machine_assert(pm);
    
    push_headers = http_header_duplicate(e->u.Push_Message.push_headers);

    ota_event = wap_event_create(Pom_SessionRequest_Req);
    ota_event->u.Pom_SessionRequest_Req.addr_tuple =
        addr_tuple_change_cliport(pm->addr_tuple,
                               CONNECTIONLESS_PUSH_CLIPORT);
    ota_event->u.Pom_SessionRequest_Req.push_headers = push_headers;
    ota_event->u.Pom_SessionRequest_Req.push_id = pm->push_id;
     
    dispatch_to_ota(ota_event);
}

/*
 * We store data to push machine, because it is possible that we do not have
 * a session when push request happens.
 */
static void request_confirmed_push(long last, PPGPushMachine *pm, 
                                   PPGSessionMachine *sm)
{
    WAPEvent *ota_event;
    List *push_headers;

    gw_assert(last == 0 || last == 1);
    push_machine_assert(pm);
    session_machine_assert(sm);
    
    push_headers = http_header_duplicate(pm->push_headers);

    ota_event = wap_event_create(Po_ConfirmedPush_Req);
    ota_event->u.Po_ConfirmedPush_Req.server_push_id = pm->push_id;
    ota_event->u.Po_ConfirmedPush_Req.push_headers = push_headers;
    ota_event->u.Po_ConfirmedPush_Req.authenticated = pm->authenticated;
    ota_event->u.Po_ConfirmedPush_Req.trusted = pm->trusted;
    ota_event->u.Po_ConfirmedPush_Req.last = last;
 
    if (pm->push_data != NULL)
        ota_event->u.Po_ConfirmedPush_Req.push_body = 
            octstr_duplicate(pm->push_data);
    else
        ota_event->u.Po_ConfirmedPush_Req.push_body = NULL;

    ota_event->u.Po_ConfirmedPush_Req.session_handle = sm->session_id;
    debug("wap.push.ota", 0, "PPG: making confirmed push request to OTA");
    
    dispatch_to_ota(ota_event);
}

static void request_unit_push(long last, PPGPushMachine *pm)
{
    WAPEvent *ota_event;
    List *push_headers;

    gw_assert(last == 0 || last == 1);
    push_machine_assert(pm);
    
    push_headers = http_header_duplicate(pm->push_headers);

    ota_event = wap_event_create(Po_Unit_Push_Req);
    ota_event->u.Po_Unit_Push_Req.addr_tuple = 
        wap_addr_tuple_duplicate(pm->addr_tuple);
    ota_event->u.Po_Unit_Push_Req.push_id = pm->push_id;
    ota_event->u.Po_Unit_Push_Req.push_headers = push_headers;
    ota_event->u.Po_Unit_Push_Req.authenticated = pm->authenticated;
    ota_event->u.Po_Unit_Push_Req.trusted = pm->trusted;
    ota_event->u.Po_Unit_Push_Req.last = last;

    if (pm->push_data != NULL)
        ota_event->u.Po_Unit_Push_Req.push_body = 
            octstr_duplicate(pm->push_data);
    else
        ota_event->u.Po_Unit_Push_Req.push_body = NULL;

    dispatch_to_ota(ota_event);
    debug("wap.push.ppg", 0, "PPG: made OTA request for unit push");
}

static void request_push(long last, PPGPushMachine *pm)
{
    WAPEvent *ota_event;
    List *push_headers;

    gw_assert(last == 0 || last == 1);
    push_machine_assert(pm);
    
    push_headers = http_header_duplicate(pm->push_headers);

    ota_event = wap_event_create(Po_Push_Req);
    ota_event->u.Po_Push_Req.push_headers = push_headers;
    ota_event->u.Po_Push_Req.authenticated = pm->authenticated;
    ota_event->u.Po_Push_Req.trusted = pm->trusted;
    ota_event->u.Po_Push_Req.last = last;

    if (pm->push_data != NULL)
        ota_event->u.Po_Push_Req.push_body = 
            octstr_duplicate(pm->push_data);
    else
        ota_event->u.Po_Push_Req.push_body = NULL;        

    ota_event->u.Po_Push_Req.session_handle = pm->session_id;
    debug("wap.push.ppg", 0, "PPG: making push request to OTA");
    
    dispatch_to_ota(ota_event);
}

#if 0
static void request_abort(long reason, long push_id, long session_id)
{
    WAPEvent *ota_event;
 
    gw_assert(push_id > 0);
    gw_assert(session_id > 0);

    ota_event = wap_event_create(Po_PushAbort_Req);
    ota_event->u.Po_PushAbort_Req.push_id = push_id;
    ota_event->u.Po_PushAbort_Req.reason = reason;
    ota_event->u.Po_PushAbort_Req.session_id = session_id;

    dispatch_to_ota(ota_event);
}
#endif
/*
 * According to Push Access Protocol, Chapter 11, capabilities can be 
 *    
 *                a) queried by PI
 *                b) told to PI when a client is subscribing
 *                c) assumed
 *
 * In case c) we got capabilities from third part of the push message (other
 * cases PI knows what it is doing), and we check is the client capable to
 * handle the message.
 * Requested capabilities are client capabilities, assumed capabilities are
 * PI capabilities. If there is no assumed capabilities, PI knows client capab-
 * ilities by method a) or method b).
 * Returns 1, if the client is capable, 0 when it is not.
 */

static int response_push_connection(WAPEvent *e, PPGSessionMachine *sm)
{
    WAPEvent *appl_event;

    gw_assert(e->type == Pom_Connect_Ind);

    if (sm->assumed_capabilities != NULL && check_capabilities(
            e->u.Pom_Connect_Ind.requested_capabilities, 
            sm->assumed_capabilities) == 0)
       return 0;

    appl_event = wap_event_create(Pom_Connect_Res);
    appl_event->u.Pom_Connect_Res.negotiated_capabilities = 
        wsp_cap_duplicate_list(e->u.Pom_Connect_Ind.requested_capabilities);
    appl_event->u.Pom_Connect_Res.session_id = e->u.Pom_Connect_Ind.session_id;

    dispatch_to_appl(appl_event);

    return 1;
}

/*
 * Push response, from Push Access Protocol, 9.3. Inputs error code, in PAP 
 * format.
 */
static void response_push_message(PPGPushMachine *pm, long code)
{
    WAPEvent *pap_event;

    push_machine_assert(pm);

    pap_event = wap_event_create(Push_Response);
    pap_event->u.Push_Response.pi_push_id =
        octstr_duplicate(pm->pi_push_id);
    pap_event->u.Push_Response.sender_name = tell_ppg_name();
    pap_event->u.Push_Response.reply_time = set_time();
/*
 * Data to generate response result, from 9.3.2. No more progress notes for 
 * current pushed message.
 */
    pap_event->u.Push_Response.code = code;
    pap_event->u.Push_Response.desc = describe_code(code);

    dispatch_to_pap(pap_event);
}

/* 
 * progress note, from 9.3.1. Used for debugging PIs.
 */
#if 0
static void send_progress_note(Octstr *stage, Octstr *note)
{
    WAPEvent *pap_event;

    pap_event = wap_event_create(Progress_Note);
    pap_event->u.Progress_Note.stage = octstr_duplicate(stage);
    pap_event->u.Progress_Note.note = octstr_duplicate(note);
    pap_event->u.Progress_Note.time = set_time();

    dispatch_to_pap(pap_event);
}
#endif
static int check_capabilities(List *requested, List *assumed)
{
    int is_capable;

    is_capable = 1;

    return is_capable;
}

/*
 * Time of creation of the response (see Push Access Protocol, chapter 9.3).
 * We convert UNIX time to ISO8601, it is, YYYY-MM-DDThh:mm:ssZ, T and Z being
 * literal strings (we use gw_gmtime to turn UNIX time to broken time).
 */
static Octstr *set_time(void)
{
    Octstr *current_time;
    struct tm now;

    now = gw_gmtime(time(NULL));
    current_time = octstr_format("%04d-%02d-%02dT%02d:%02d:%02dZ", 
                                 now.tm_year + 1899, now.tm_mon + 1, 
                                 now.tm_mday, now.tm_hour, now.tm_min, 
                                 now.tm_sec);

    return current_time;
}

static void session_machine_assert(PPGSessionMachine *sm)
{
    gw_assert(sm);
    gw_assert(sm->session_id >= 0);
    gw_assert(sm->addr_tuple);
    gw_assert(sm->pi_client_address);
}

static void push_machine_assert(PPGPushMachine *pm)
{
    gw_assert(pm);
    gw_assert(pm->pi_push_id);
    gw_assert(pm->push_id >= 0);
    gw_assert(pm->session_id >= 0);
    gw_assert(pm->addr_tuple);
    gw_assert(pm->trusted == 1 || pm->trusted == 0);
    gw_assert(pm->authenticated  == 1 || pm->authenticated == 0);
}

/*
 * Message transformations performed by PPG are defined in PPG, 6.1.2.1. We 
 * do not do any (optional) header conversions to the binary format here, 
 * these are responsibility of our OTA module (gw/wap_push_ota.c). FIXME: Re-
 * move all headers which default values are known to the client. 
 *
 * Return message, either transformed or not (if there is no-transform cache 
 * directive or wml code is erroneous) separately the transformed gw address 
 * tuple and message content type and body. In addition, a flag telling was 
 * the transformation (if any) successfull or not.
 */
static int transform_message(WAPEvent **e, WAPAddrTuple **tuple, 
                             int cless_accepted, Octstr **type)
{
    List *push_headers;
    int message_deliverable;
    struct content content;
    Octstr *cliaddr;
    long cliport,
         servport;

    gw_assert((**e).type == Push_Message);
    gw_assert((**e).u.Push_Message.push_headers);

    cliaddr = (**e).u.Push_Message.address_value;
    push_headers = (**e).u.Push_Message.push_headers;
    
    check_x_wap_application_id_header(&push_headers);

    if (!cless_accepted) {
        cliport = CONNECTED_CLIPORT;
        servport = CONNECTED_SERVPORT;
    } else {
        cliport = CONNECTIONLESS_PUSH_CLIPORT;
        servport = CONNECTIONLESS_SERVPORT;
    }

    if ((*tuple = set_addr_tuple(cliaddr, cliport, servport)) == NULL) {
        warning(0, "PPG: push client address unpossible to understand");
        goto error;
    }
    if (!content_transformable(push_headers)) 
        goto no_transform;
    content.body = (**e).u.Push_Message.push_data; 
    if (content.body == NULL)
        goto no_transform;

    http_header_get_content_type(push_headers, &content.type,
                                 &content.charset);   
    message_deliverable = convert_content(&content);

    if (message_deliverable) {
        *type = content.type;        
    } else
        goto error;

    (**e).u.Push_Message.push_data = content.body;
    octstr_destroy(content.charset);

    debug("wap.push.ppg", 0, "PPG: push message content and headers valid");
    return 1;

error:
    warning(0, "PPG: push content erroneous, cannot accept it");
    octstr_destroy(content.type);
    octstr_destroy(content.charset);
    return 0;

no_transform:
    info(0, "PPG: non transformable push content, not compiling");
    octstr_destroy(content.type);
    octstr_destroy(content.charset);
    return 1;
}

/*
 * Transform X-WAP-Application headers as per PPG 6.1.2.1. AbsoluteURI format
 * for X-Wap-Application-Id is defined in PushMessage, 6.2.2.1. Note: handling
 * default application id is missing (an optional feature).
 */
static void check_x_wap_application_id_header(List **push_headers)
{
    Octstr *appid_content;
    char *header_value;
    
    appid_content = http_header_find_first(*push_headers, 
        "X-WAP-Application-Id");
    
    if (appid_content == NULL) {
        header_value = "0x02";                 /* assigned number for wml ua */
        http_header_add(*push_headers, "X-WAP-Application-Id", header_value);
        return;
    }

    parse_appid_header(&appid_content);
    http_header_remove_all(*push_headers, "X-WAP-Application-Id");
    http_header_add(*push_headers, "X-WAP-Application-Id", 
                    octstr_get_cstr(appid_content));

    octstr_destroy(appid_content);   
}

/*
 * Check do we have a no-transform cache directive amongst the headers.
 */
static int content_transformable(List *push_headers)
{
    List *cache_directives;
    long i;
    Octstr *header_name, 
           *header_value;

    gw_assert(push_headers);

    cache_directives = http_header_find_all(push_headers, "Cache-Control");
    if (list_len(cache_directives) == 0) {
        http_destroy_headers(cache_directives);
        return 1;
    }

    i = 0;
    while (i < list_len(cache_directives)) {
        http_header_get(cache_directives, i, &header_name, &header_value);
        if (octstr_compare(header_value, octstr_imm("no-transform")) == 0) {
            http_destroy_headers(cache_directives);
            octstr_destroy(header_name);
            octstr_destroy(header_value);
	    return 0;
        }
        ++i;
    }

    http_destroy_headers(cache_directives);
    octstr_destroy(header_name);
    octstr_destroy(header_value);
 
    return 1;
}

/*
 * Convert push content to compact binary format (this can be wml, si, sl or 
 * co.
 */
static Octstr *convert_wml_to_wmlc(struct content *content)
{
    Octstr *wmlc;

    if (wml_compile(content->body, content->charset, &wmlc) == 0)
        return wmlc;
    warning(0, "PPG: wml compilation failed");
    return NULL;
}

static struct {
    char *type;
    char *result_type;
    Octstr *(*convert) (struct content *);
} converters[] = {
    { "text/vnd.wap.wml",
    "application/vnd.wap.wmlc",
    convert_wml_to_wmlc }
};

#define NUM_CONVERTERS ((long) (sizeof(converters) / sizeof(converters[0])))

static int convert_content(struct content *content)
{
    long i;
    Octstr *new_body;

    for (i = 0; i < NUM_CONVERTERS; i++) {
        if (octstr_compare(content->type, 
	        octstr_imm(converters[i].type)) == 0) {
	    new_body = converters[i].convert(content);
            if (new_body == NULL)
	        return 0;
            octstr_destroy(content->body);
            content->body = new_body;
            octstr_destroy(content->type); 
            content->type = octstr_create(converters[i].result_type);
            return 1;
        }
    }

    return 0;
}

/*
 * Now we support only one bearer and one network, so we must reject others. 
 * Bearer and network types are defined in WDP, Appendix C.
 */
int select_bearer_network(WAPEvent *e)
{
    Octstr *bearer,
           *network;
    int bearer_required;
    int network_required;
    int ret;

    gw_assert(e->type == Push_Message);

    bearer_required = e->u.Push_Message.bearer_required;
    network_required = e->u.Push_Message.network_required;
    bearer = e->u.Push_Message.bearer;
    network = e->u.Push_Message.network;
    ret = (!network_required && !bearer_required) || 
           (network_required && 
                octstr_compare(network, octstr_imm("GSM")) == 0) ||
           (bearer_required && 
                octstr_compare(bearer, octstr_imm("CSD")) == 0);
    if (!ret)
        warning(0, "PPG: requested bearer is not avaible");

    return ret;
}

static int session_has_pi_client_address(void *a, void *b)
{
    Octstr *caddr;
    PPGSessionMachine *sm;

    caddr = b;
    sm = a;

    return octstr_compare(caddr, sm->pi_client_address) == 0;
}

/*
 * PI client address is composed of a client specifier and a PPG specifier (see
 * PPG, chapter 7). So it is equivalent with gw address quadruplet.
 */
PPGSessionMachine *session_find_using_pi_client_address(Octstr *caddr)
{
    PPGSessionMachine *sm;
    
    sm = list_search(ppg_machines, caddr, session_has_pi_client_address);

    return sm;
}

/*
 * Give PPG a human readable name.
 */
static Octstr *tell_ppg_name(void)
{
     return octstr_format("WAP/1.3 %S (Kannel/%s)", get_official_name(), 
                          VERSION);
}

/*
 * Delivery time constraints are a) deliver before and b) deliver after. It is
 * possible that service required is after some time and before other. So we 
 * test first condition a).
 * Returns: 0 delivery time expired
 *          1 too early to send the message
 *          2 no constraints
 */
static int delivery_time_constraints(WAPEvent *e, PPGPushMachine *pm)
{
    Octstr *before,
           *after;
    struct tm now;
   
    gw_assert(e->type = Push_Message);
    
    before = e->u.Push_Message.deliver_before_timestamp;
    after = pm->deliver_after_timestamp;
    now = gw_gmtime(time(NULL));

    if (!deliver_before_test_cleared(before, now)) {
        info(0, "PPG: delivery deadline expired, dropping the push message");
        return 0;
    }

    if (!deliver_after_test_cleared(after, now)) {
        debug("wap.push.ppg", 0, "PPG: too early to push the message,"
              " waiting");
        return 1;
    }

    return 2;
}

/*
 * Give verbose description of the result code. Conversion table for descrip-
 * tion.
 */
struct description_t {
    long reason;
    char *description;
};

typedef struct description_t description_t;

static description_t pap_desc[] = {
    { ACCEPTED_FOR_PROCESSING, "The request has been accepted for processing"},
    { BAD_REQUEST, "Not understood due to malformed syntax"},
    { FORBIDDEN, "Request was refused"},
    { ADDRESS_ERROR, "The client specified not recognised"},
    { CAPABILITIES_MISMATCH, "Capabilities assumed by PI were not acceptable"
                              " for the client specified"},
    { DUPLICATE_PUSH_ID, "Push id supplied was not unique"},
    { TRANSFORMATION_FAILURE, "PPG was unable to perform a transformation on"
                              " the message"},
    { REQUIRED_BEARER_NOT_AVAIBLE, "Required bearer not avaible"},
    { WSP_ABORT_USERREQ, "User requested abort"},
    { WSP_ABORT_USERRFS, "User refused push message. Do not try again"},
    { WSP_ABORT_USERPND, "Push message cannot be delivered to intended"
                         "destination"},
    { WSP_ABORT_USERDCR, "Push message discarded due to resource shortage"},
    { WSP_ABORT_USERDCU, "Content type of the push message cannot be"
                         " processed"}
};

static size_t desc_tab_size = sizeof(pap_desc) / sizeof(pap_desc[0]);
    
static Octstr *describe_code(long code)
{
    Octstr *desc;
    long i;

    for (i = 0; i < desc_tab_size; i++) {
        if (pap_desc[i].reason == code) {
            desc = octstr_create(pap_desc[i].description);
            return desc;
        }
    }

    return octstr_imm("unknown PAP code");
}

/*
 * Remove push data from the list of connectionless pushes, if cless is true, 
 * otherwise from the list of pushes belonging to session machine sm.
 */
static void remove_push_data(PPGSessionMachine *sm, PPGPushMachine *pm, 
                             int cless)
{
    push_machine_assert(pm);

    if (cless) {
        list_delete_equal(ppg_unit_pushes, pm);
    } else {
        session_machine_assert(sm);
        list_delete_equal(sm->push_machines, pm);
    }

    push_machine_destroy(pm);
}

/*
 * If there is no push with a similar push id, store push data. If cless is 
 * true, store it in the list connectionless pushes, otherwise in the push 
 * list of the session machine sm.
 * Return pointer to push data, if no error, pointer  to NULL otherwise. In 
 * addition, return push id for the stored push.
 */
static PPGPushMachine *store_push_data(PPGSessionMachine *sm, WAPEvent *e, 
                                       WAPAddrTuple *tuple, int cless)
{
    PPGPushMachine *pm;  
    Octstr *pi_push_id;  
    
    gw_assert(e->type == Push_Message);

    pi_push_id = e->u.Push_Message.pi_push_id;

    if (!cless) {
        session_machine_assert(sm);
        pm = find_ppg_push_machine_using_pi_push_id(sm, pi_push_id);
    } else
        pm = find_unit_ppg_push_machine_using_pi_push_id(pi_push_id);

    if (pm == NULL)
        pm = push_machine_create(e, tuple);
    else 
        return NULL;
 
    if (!cless) {
       list_append(sm->push_machines, pm);
       debug("wap.push.ppg", 0, "PPG: push machine %ld appended to push list"
             " of sm machine %ld", pm->push_id, sm->session_id);
       list_append(ppg_machines, sm);
       debug("wap.push.ppg", 0, "PPG: session machine %ld appended to ppg"
             "machines list", sm->session_id);
    } else {
       list_append(ppg_unit_pushes, pm);
       debug("wap.push.ppg", 0, "PPG: push machine %ld append to unit push"
             " list", pm->push_id);
    }

    return pm;
}

/*
 * Deliver confirmed push. Note that if push is confirmed, PAP attribute is up-
 * dated only after an additional event (confirmation, abort or time-out). 
 */
static void deliver_confirmed_push(long last, PPGPushMachine *pm, 
                                   PPGSessionMachine *sm)
{
    request_confirmed_push(last, pm, sm);
}

/*
 * PPG, chapter 6.1.2.2 , subchapter delivery, says that if push is unconform-
 * ed, we can use either Po-Unit-Push.req or Po-Push.req primitive. We use Po-
 * Push.req, if have an already established session (other words, sm == NULL).
 * In addition, update PAP attribute. Return pointer to the updated push mach-
 * ine.
 */
static PPGPushMachine *deliver_unit_push(long last, PPGPushMachine *pm, 
    PPGSessionMachine *sm, int session_exists)
{
    push_machine_assert(pm);
    
    if (!session_exists)
        request_unit_push(last, pm);
    else
        request_push(last, pm);

    pm = update_push_data_with_attribute(&sm, pm, UNCONFIRMED, DELIVERED1);
    info(0, "PPG: unconfirmed push delivered to OTA");

    return pm;
}

/*
 * Deliver all pushes queued by session machine sm (it is, make a relevant OTA
 * request). Update PAP attribute, if push is unconfirmed.
 */
static void deliver_pending_pushes(PPGSessionMachine *sm, int last)
{
    PPGPushMachine *pm;    
    long i;

    session_machine_assert(sm);
    gw_assert(list_len(sm->push_machines) > 0);

    for (i = 0; i < list_len(sm->push_machines); i++) {
        pm = list_get(sm->push_machines, i);
        push_machine_assert(pm);

        if (pm->delivery_method == UNCONFIRMED) {
            request_push(last, pm); 
            pm = update_push_data_with_attribute(&sm, pm, UNCONFIRMED, 
                 DELIVERED1);
            remove_push_data(sm, pm, sm == NULL);
        } else {
	    request_confirmed_push(last, pm, sm);
        }
    }
}     

/*
 * Abort all pushes queued by session machine sm. In addition, update PAP
 * attribute and notify PI.
 */
static PPGPushMachine *abort_delivery(PPGSessionMachine *sm)
{
    PPGPushMachine *pm;
    long i,
         reason,
         code;

    session_machine_assert(sm);

    i = 0;
    pm = NULL;
    reason = PAP_ABORT_USERPND;
    code = CAPABILITIES_MISMATCH;
    
    while (list_len(sm->push_machines) > 0) {
        pm = list_get(sm->push_machines, i);
        push_machine_assert(pm);

        pm = update_push_data_with_attribute(&sm, pm, reason, ABORTED);
        response_push_message(pm, code);

        remove_push_data(sm, pm, sm == NULL);
        ++i;
    }

    return pm;
}

/*
 * Remove a session, even if it have active pushes. These are aborted, and we
 * must inform PI about this. Client abort codes are defined in PAP, 9.14.5,
 * which refers to WSP, Appendix A, table 35.
 */
static void remove_session_data(PPGSessionMachine *sm)
{
    long i,
         code;
    PPGPushMachine *pm;

    session_machine_assert(sm);
    i = 0;

    code = PAP_ABORT_USERPND;
    
    while (list_len(sm->push_machines) > 0) {
        pm = list_get(sm->push_machines, i);
        response_push_message(pm, code);
        remove_push_data(sm, pm, sm == NULL);
        ++i;
    }

    list_delete_equal(ppg_machines, sm);
    session_machine_destroy(sm);
}

/*
 * Remove session, if it has no active pushes.
 */
static void remove_pushless_session(PPGSessionMachine *sm)
{
    session_machine_assert(sm);

    if (list_len(sm->push_machines) == 0) {
        list_delete_equal(ppg_machines, sm);
        session_machine_destroy(sm);
    }
}

/*
 * If session machine not exist, create a session machine and store session 
 * data. If session exists, ignore. 
 * Return pointer to the session machine, and a flag did we have a session 
 * before executing this function. (Session data is needed to implement the 
 * PAP attribute. It does not mean that a session exists.)
 */
static PPGSessionMachine *store_session_data(PPGSessionMachine *sm,
    WAPEvent *e, WAPAddrTuple *tuple, int *session_exists)
{
    gw_assert(e->type == Push_Message);

    if (sm == NULL) {
        sm = session_machine_create(tuple, e);
        *session_exists = 0;
    } else
        *session_exists = 1;
    
    return sm;
}

static PPGSessionMachine *update_session_data_with_headers(
    PPGSessionMachine *sm, PPGPushMachine *pm)
{
    list_delete_matching(sm->push_machines, &pm->push_id, push_has_pid);
    list_append(sm->push_machines, pm);

    return sm;
}

/*
 * PPG 6.1.2.2, subchapter delivery, states that if the delivery method is not
 * confirmed or unconfirmed, PPG may select an implementation specific type of
 * the  primitive. We use an unconfirmed push, if the attribute is not specifi-
 * ed. 
 * FIXME: add handling of the preferconfirmed attribute.
 */
static int confirmation_requested(WAPEvent *e)
{
    gw_assert(e->type = Push_Message);

    return e->u.Push_Message.delivery_method == CONFIRMED;
}

static int push_has_pid(void *a, void *b)
{
    long *pid;
    PPGPushMachine *pm;

    pid = b;
    pm = a;
    
    return *pid == pm->push_id;
}

static PPGPushMachine *find_ppg_push_machine_using_pid(PPGSessionMachine *sm, 
                                                   long pid)
{
    PPGPushMachine *pm;

    gw_assert(pid >= 0);
    session_machine_assert(sm);

    pm = list_search(sm->push_machines, &pid, push_has_pid);

    return pm;
}

static int push_has_pi_push_id(void *a, void *b)
{
    Octstr *pi_push_id;
    PPGPushMachine *pm;

    pi_push_id = b;
    pm = a;

    return octstr_compare(pm->pi_push_id, pi_push_id) == 0;
}

static PPGPushMachine *find_ppg_push_machine_using_pi_push_id(
    PPGSessionMachine *sm, Octstr *pi_push_id)
{
    PPGPushMachine *pm;

    gw_assert(pi_push_id);
    session_machine_assert(sm);

    pm = list_search(sm->push_machines, &pi_push_id, push_has_pi_push_id);

    return pm;
}

static PPGPushMachine *find_unit_ppg_push_machine_using_pi_push_id(
    Octstr *pi_push_id)
{
    PPGPushMachine *pm;

    gw_assert(pi_push_id);
    pm = list_search(ppg_unit_pushes, &pi_push_id, push_has_pi_push_id);

    return pm;
}

/*
 * Store a new value of the push attribute into a push machine. It is to be 
 * found from the list of unit pushes, if connectionless push was asked 
 * (sm == NULL), otherwise from the the push list of the session machine sm. 
 * Returns updated push machine and session machine (this one has an updated
 * push machines list). 
 */
static PPGPushMachine *update_push_data_with_attribute(PPGSessionMachine **sm, 
    PPGPushMachine *qm, long reason, long status)
{
    push_machine_assert(qm);
   
    switch (status) {
    case UNDELIVERABLE1:
         qm->message_state = UNDELIVERABLE;
         qm->code = BAD_REQUEST;
    break;

    case UNDELIVERABLE2:
        qm->code = reason;
        qm->message_state = UNDELIVERABLE;
        qm->desc = describe_code(reason);
    break;

    case ABORTED:
        qm->message_state = status;
        qm->code = ota_abort_to_pap(reason);
        qm->event_time = set_time();
        qm->desc = describe_code(reason);
    break;

    case DELIVERED1:
        qm->message_state = DELIVERED;
        qm->delivery_method = UNCONFIRMED;
        qm->event_time = set_time();
    break;

    case DELIVERED2:
        qm->message_state = DELIVERED;
        qm->delivery_method = CONFIRMED;
        qm->event_time = set_time();
    break;

    case EXPIRED:
        qm->message_state = EXPIRED;
        qm->event_time = set_time();
        qm->desc = describe_code(reason);
    break;

    case PENDING:
        qm->message_state = PENDING;
    break;

    default:
        error(0, "WAP_PUSH_PPG: Non existing push machine status: %ld", 
              status);
    break;
    }

    if (*sm != NULL){
        list_delete_matching((**sm).push_machines, &qm->push_id, push_has_pid);
        list_append((**sm).push_machines, qm);
        list_delete_equal(ppg_machines, *sm);
        list_append(ppg_machines, *sm);
    } else {
        list_delete_matching(ppg_unit_pushes, &qm->push_id, push_has_pid);
        list_append(ppg_unit_pushes, qm);
    }

    return qm;
}

/*
 * Store session id, client port and caps list received from application layer.
 */
static PPGSessionMachine *update_session_data(PPGSessionMachine *m, 
                                              long sid, long port, List *caps)
{
    session_machine_assert(m);
    gw_assert(sid >= 0);

    m->session_id = sid;
    m->addr_tuple->remote->port = port;
    m->client_capabilities = wsp_cap_duplicate_list(caps);

    list_delete_equal(ppg_machines, m);
    list_append(ppg_machines, m);

    return m;
}

/*
 * Convert OTA abort codes (OTA 6.3.3) to corresponding PAP status codes. These
 * are defined in 9.14.5.
 */
static long ota_abort_to_pap(long reason)
{
    long offset;

    offset = reason - 0xEA;
    reason = 5026 + offset;

    return reason;
}

/*
 * Accept connectionless push, it is, this is preferconfirmed or PI wants con-
 * nectionless push and there is no sessions open.
 * FIXME: preferconfirmed messages.
 */
static int cless_accepted(WAPEvent *e, PPGSessionMachine *sm)
{
    gw_assert(e->type == Push_Message);
    return e->u.Push_Message.delivery_method == UNCONFIRMED && sm == NULL;
}

/*
 * Compare PAP message timestamp, in PAP message format, and stored in octstr,
 * to gm (UTC) broken time. Return true, if before is after now, or if the 
 * service in question was not requested by PI. PAP time format is defined in 
 * PAP, chapter 9.2.
 */

static void initialize_time_item_array(long time_data[], struct tm now) 
{
    time_data[0] = now.tm_year + 1899;
    time_data[1] = now.tm_mon + 1;
    time_data[2] = now.tm_mday;
    time_data[3] = now.tm_hour;
    time_data[4] = now.tm_min;
    time_data[5] = now.tm_sec;
}

static int date_item_compare(Octstr *before, long time_data, long pos)
{
    long data;

    if (octstr_parse_long(&data, before, pos, 10) < 0)
        return 0;
    if (data < time_data)
        return -1;
    if (data > time_data)
        return 1;

    return 0;
}

/*
 * We do not accept timestamps equalling now. Return true, if the service was
 * not requested.
 */
static int deliver_before_test_cleared(Octstr *before, struct tm now)
{  
    long time_data[6];
    long j;

    if (before == NULL)
        return 1;
    
    initialize_time_item_array(time_data, now);
    if (date_item_compare(before, time_data[0], 0) == 1)
        return 1;
    if (date_item_compare(before, time_data[0], 0) == -1)
        return 0;

    for (j = 5; j < octstr_len(before); j += 3) {
        if (date_item_compare(before, time_data[(j-5)/3 + 1], j) == 1)
            return 1;
        if (date_item_compare(before, time_data[(j-5)/3 + 1], j) == -1)
            return 0;
    }

    return 0;
}

/* 
 * Ditto. Return true, if after is before now (or the service was not request-
 * ed). Do not accept timestamps equalling now.
 */
static int deliver_after_test_cleared(Octstr *after, struct tm now)
{
    long time_data[6];
    long j;

    if (after == NULL)
        return 1;

    initialize_time_item_array(time_data, now);
    if (date_item_compare(after, time_data[0], 0) == -1)
        return 1;
    if (date_item_compare(after, time_data[0], 0) == 1)
        return 0;

    for (j = 5; j < octstr_len(after); j += 3) {
        if (date_item_compare(after, time_data[(j-5)/3 + 1], j) == -1)
            return 1;
        if (date_item_compare(after, time_data[(j-5)/3 + 1], j) == 1)
            return 0;
    }

    return 0;
}

static long accept_escaped(Octstr **address, long pos)
{
    Octstr *temp;
    long i;
    unsigned char c;

    pos = drop_character(address, pos);
    temp = octstr_create("");

    for (i = 2; i > 0; i--) {
        c = octstr_get_char(*address, pos + i);
        temp = prepend_char(temp, c);
        pos = drop_character(address, pos + i);
        if (pos > 0)
	  --pos;
    }

    if (octstr_hex_to_binary(temp) < 0) {
        octstr_destroy(temp);
        return -2;
    }

    octstr_insert(*address, temp, pos + 2);   /* To the end of the window */

    octstr_destroy(temp);
    return pos + 1;                           /* The position preceding the 
                                                 inserted character */
              
}

static long accept_safe(Octstr **address, long pos)
{
    unsigned char c;

    c = octstr_get_char(*address, pos);
    if ((isalnum(c) || c == '+' || c == '-' || c == '.' || c == '_') && 
            pos >= 0)
	--pos;
    else if (c == '=')
        return -1;
    else
        return -2;

    return pos;
}

static long rest_unescaped(Octstr **address, long pos)
{
    long i,
         ret;

    for (i = 2; i > 0; i--) {
         if ((ret = accept_safe(address, pos)) == -2)
	     return -2;
         else if (ret == -1)
	     return pos;
    }

    return pos;
}

/*
 * Return -1, it was impossible to create the window because of there is no
 * more enough characters left and 0 if OK.
 */
static int create_peek_window(Octstr **address, long *pos)
{
    long i;
    unsigned char c;

    i = 0;
    while (i < 2 && (c = octstr_get_char(*address, *pos)) != '=') {
        if (*pos > 0)
            --*pos;
        ++i;
    }

    if (c == '=')
        return 0;

    return 1;
}

static int issafe(Octstr **address, long pos){
    
    if (octstr_get_char(*address, pos) == '%')
        return 0;
    else
        return 1;
}

static long drop_character(Octstr **address, long pos)
{
    if (pos >= 0) {
        octstr_delete(*address, pos, 1);
        if (pos > 0)
            --pos;
    }

    return pos;
}

/*
 * Point ends the string, comma separates string fragments.
 */
static long handle_two_terminators (Octstr **address, long pos, 
    unsigned char comma, unsigned char point, unsigned char c, 
    long fragment_parsed, long fragment_length)
{
    if (fragment_parsed == fragment_length && c != comma && c != point)
        return -2;

    if (c == point) 
        octstr_delete(*address, pos, 1);

    --pos;

    return pos;
}

static Octstr *prepend_char(Octstr *os, unsigned char c)
{
    Octstr *tmp;

    tmp = octstr_format("%c", c);
    octstr_insert(os, tmp, 0);
    octstr_destroy(tmp);
    return os;
}

static long parse_dom_fragment(Octstr **address, long pos)
{
    unsigned char c;

    if (pos >= 0) { 
        if (isalnum(octstr_get_char(*address, pos))) {
	    pos = drop_character(address, pos);
        } else
	    return -2;
    }

    while ((c = octstr_get_char(*address, pos)) != '@' && 
               octstr_get_char(*address, pos) != '.' && pos >= 0)  {
        if (isalnum(c) || c == '-') {
	    pos = drop_character(address, pos);
        } else
	    return -2;
    } 

    return pos;
}

/*
 * Ext qualifiers contain /, ipv4 address contains . , ipv6 address contains :.
 * phone number contains + and escaped-value contain no specific tokens. They 
 * are for future extansions, but we must parse them.
 */
static int qualifiers(Octstr *address, long pos, Octstr *type)
{
    unsigned char term,
         c;
    long i;

    i = pos;
    c = '+';

    if (octstr_compare(type, octstr_imm("PLMN")) == 0)
        term = '+';
    else if (octstr_compare(type, octstr_imm("IPv4")) == 0)
        term = '.';
    else if (octstr_compare(type, octstr_imm("IPv6")) == 0)
        term = ':';
    else
        term = 'N';

    if (term != 'N')
        while ((c = octstr_get_char(address, i)) != term) {
            if (c == '/')
                return 1;
            --i;
    }

    if (term == 'N') {
        while (i != 0) {
            if (c == '/')
                return 1;
            --i;
        }
    } 

    return 0;
}

static long parse_qualifier_value(Octstr **address, long pos)
{
    unsigned char c;

    while ((c = octstr_get_char(*address, pos)) != '=' && pos >= 0) {
        if (c < 0x20 || (c > 0x2e && c < 0x30) || (c > 0x3c && c < 0x3e) ||
            c > 0x7e)
            return -2;

        pos = drop_character(address, pos);
    }

    pos = drop_character(address, pos);
  
    return pos;
}

static long parse_qualifier_keyword(Octstr **address, long pos)
{
    unsigned char c;  

    while ((c = octstr_get_char(*address, pos)) != '/') {
        if (isalnum(c) || c == '-') {
	    pos = drop_character(address, pos);
        } else
	    return -2;
    }

    pos = drop_character(address, pos);       

    return pos;
}

/*
 * WINA web page does not include address type identifiers. Following ones are
 * from WDP, Appendix C.
 */

static char *bearer_address[] = {
    "GSM_MSISDN",
    "ANSI_136_MSISDN",
    "IS_637_MSISDN",
    "iDEN_MSISDN",
    "FLEX_MSISDN",
    "PHS_MSISDN",
    "GSM_Service_Code",
    "TETRA_ITSI",
    "TETRA_MSISDN",
    "ReFLEX_MSIDDN",
    "MAN",
};

static size_t bearer_address_size = sizeof(bearer_address) / 
                                    sizeof(bearer_address[0]);

static int wina_bearer_identifier(Octstr *type_value)
{
    long i;

    i = 0;
    while (i < bearer_address_size) {
        if (octstr_compare(type_value, octstr_imm(bearer_address[i])) == 0)
	    return 1;
        ++i;
    }

    return 0;
}

static long parse_type(Octstr **address, Octstr **type_value, long pos) 
{
    unsigned char c;

    while ((c = octstr_get_char(*address, pos)) != '=' && pos >= 0) {   
        *type_value = prepend_char(*type_value, c);
        pos = drop_character(address, pos);
    } 

    if (pos < 0)
        return -2;

    return pos;
}

static long parse_ext_qualifiers(Octstr **address, long pos, Octstr *type)
{
    while (qualifiers(*address, pos, type)) {
        if ((pos = parse_qualifier_value(address, pos)) < 0)
            return pos;

        if ((pos = parse_qualifier_keyword(address, pos)) < 0)
            return pos;
    }

    return pos;
}

static long parse_global_phone_number(Octstr **address, long pos)
{
    unsigned char c;

    while ((c = octstr_get_char(*address, pos)) != '+' && pos >= 0) {
        if (!isdigit(c) && c != '-' && c != '.')
             return -2;
        else
	     --pos;
    }

    if (pos > 0)
        --pos;

    pos = drop_character(address, pos);

    return pos;
}

static long parse_ipv4_fragment(Octstr **address, long pos) 
{
    long i;
    unsigned char c;

    i = 0;
    c = '=';

    if (isdigit(octstr_get_char(*address, pos)) && pos >= 0) {
        --pos;
        ++i;
    } else {
        return -2;
    }
    
    while (i <= 3 && ((c = octstr_get_char(*address, pos)) != '.' &&  c != '=')
            && pos >= 0) {
        if (isdigit(c)) {
	    --pos;
            ++i;
        } else {
	    return -2;
        }
    }

    pos = handle_two_terminators(address, pos, '.', '=', c, i, 3);

    return pos;
}

static long parse_ipv4(Octstr **address, long pos)
{
    long i;

    if ((pos = parse_ipv4_fragment(address, pos)) < 0) 
        return -2;

    i = 1;

    while (i <= 3 && octstr_get_char(*address, pos) != '=' && pos >= 0) {
        pos = parse_ipv4_fragment(address, pos);
        ++i;
    }

    return pos;
}

static long parse_ipv6_fragment(Octstr **address, long pos) {
    long i;
    unsigned char c;

    i = 0;

    if (isxdigit(octstr_get_char(*address, pos)) && pos >= 0) {
        --pos;
        ++i;
    } else {
        return -2;
    }

    c = '=';

    while (i <= 4 && ((c = octstr_get_char(*address, pos)) != ':' && c != '=')
            && pos >= 0) {
        if (isxdigit(c)) {
	    --pos;
            ++i;
        } else {
	    return -2;
        }
    }

    pos = handle_two_terminators(address, pos, ':', '=', c, i, 4);

    return pos;
}

static long parse_ipv6(Octstr **address, long pos) 
{
    long i;

    if ((pos = parse_ipv6_fragment(address, pos)) < 0)
        return -2;

    i = 1;

    while (i <= 7 && pos >= 0) {
        pos = parse_ipv6_fragment(address, pos);
        ++i;
    }

    return pos;
}

/*
 * Note that we parse backwards. First we create a window of three characters
 * (representing a possible escaped character). If the first character of the 
 * window is not escape, we handle the last character and move the window one
 * character backwards; if it is, we handle escaped sequence and create a new
 * window. If we cannot create a window, rest of characters are unescaped.
 */
static long parse_escaped_value(Octstr **address, long pos) 
{
    int ret;

    if (create_peek_window(address, &pos) == 0)
         if ((pos = rest_unescaped(address, pos)) == -2)
             return -2;

    while (octstr_get_char(*address, pos) != '=' && pos >= 0) {
        if ((ret = issafe(address, pos)) == 1) {
	    pos = accept_safe(address, pos);

        } else if (ret == 0) {
	    if ((pos = accept_escaped(address, pos)) < 0)
                return -2;  
            if (create_peek_window(address, &pos) == 0)
                if ((pos = rest_unescaped(address, pos)) == -2)
                    return -2;
        }
    }

    pos = drop_character(address, pos);

    return pos;
}

static long parse_client_specifier(Octstr **address, long pos)
{
    Octstr *type_value;

    type_value = octstr_create("");

    if ((pos = parse_type(address, &type_value, pos)) < 0) {
        goto parse_error;
    }

    pos = drop_character(address, pos);

    if ((pos = parse_constant("/TYPE", address, pos)) < 0) {
        goto parse_error;
    }

    if (octstr_compare(type_value, octstr_imm("USER")) == 0)
        goto not_implemented;

    if ((pos = parse_ext_qualifiers(address, pos, type_value)) < 0)
        goto parse_error;

    if (octstr_compare(type_value, octstr_imm("PLMN")) == 0) {
        pos = parse_global_phone_number(address, pos);
    }

    else if (octstr_compare(type_value, octstr_imm("IPv4")) == 0) {
        pos = parse_ipv4(address, pos);
    }

    else if (octstr_compare(type_value, octstr_imm("IPv6")) == 0) {
        pos = parse_ipv6(address, pos);
    }

    else if (wina_bearer_identifier(type_value)) {
        pos = parse_escaped_value(address, pos); 
    }    

    else
        goto parse_error; 

    octstr_destroy(type_value);
    return pos;

not_implemented:
    octstr_destroy(type_value);
    return -1;

parse_error:
    octstr_destroy(type_value);
    return -2;
}

static long parse_constant(const char *field_name, Octstr **address, long pos)
{
    long i;    
    size_t size;

    size = strlen(field_name);
    i = 0;
    
    while (octstr_get_char(*address, pos - i)  == field_name[size-1 - i] && 
            i <  size) {
        ++i;
    }

    while (octstr_get_char(*address, pos) != field_name[0] && pos >= 0) {
        pos = drop_character(address, pos);
    }

    pos = drop_character(address, pos);    

    if (pos < 0 || i != size) {
        return -2;
    }

    return pos;
}

static long parse_wappush_client_address(Octstr **address, long pos)
{
    if ((pos = parse_client_specifier(address, pos)) < 0) {
        return pos;
    }

    pos = parse_constant("WAPPUSH", address, pos);
    
    return pos;
}

/*
 * We are not interested of ppg specifier, but we must check its format.
 */
static long parse_ppg_specifier(Octstr **address, long pos)
{
    if (pos >= 0) {
        pos = parse_dom_fragment(address, pos);
    }

    while (octstr_get_char(*address, pos) != '@' && pos >= 0) {
        if (octstr_get_char(*address, pos) == '.') {
	    octstr_delete(*address, pos, 1);
            --pos;
        } else
	    return -2;

        pos = parse_dom_fragment(address, pos);
    } 

    pos = drop_character(address, pos);

    if (octstr_get_char(*address, pos) == '/' && pos >= 0) {
        octstr_delete(*address, pos, 1);
        if (pos > 0)
            --pos;
    }

    if (pos < 0)
       return -2;

    return pos;
}

static long parse_client_address(Octstr **address)
{
    long pos;

    pos = octstr_len(*address) - 1;
/*
 * Delete first separator, if there is one. This will help our parsing later.
 */
    if (octstr_get_char(*address, 0) == '/')
        octstr_delete(*address, 0, 1);

    if ((pos = parse_ppg_specifier(address, pos)) < 0) {
        return -2;
    }

    pos = parse_wappush_client_address(address, pos);
    
    return pos;
}

/*
 * We exchange here server and client addresses and ports, because our WDP,
 * written for pull, exchange them, too. Similarly server address INADDR_ANY is
 * used for compability reasons.
 */
static WAPAddrTuple *set_addr_tuple(Octstr *address, long cliport, 
                                    long servport)
{
    Octstr *cliaddr,
           *servaddr;
    int ret;
    WAPAddrTuple *tuple;
    
    gw_assert(address);

    if ((ret = parse_client_address(&address)) < 0)
        return NULL;

    servaddr = address;
    cliaddr = octstr_imm("0.0.0.0");
    tuple = wap_addr_tuple_create(servaddr, cliport, cliaddr, servport);

    return tuple;
}

/*
 * We are not interested about parsing URI fully - we check only does it cont-
 * ains application id reserved by WINA or the part containing assigned code. 
 * Otherwise (regardless of it being an URI or assigned code) we simply pass 
 * it forward.
 */
#define NUMBER_OF_WINA_URIS 4

static char *wina_uri[] =
{   "*",
    "push.sia",
    "wml.ua",
    "push.mms"
};

static void parse_appid_header(Octstr **appid_content)
{
    long pos,
         coded_value,
         i;

    if ((pos = octstr_search(*appid_content, octstr_imm(";"), 0)) >= 0) {
        octstr_delete(*appid_content, pos, 
                      octstr_len(octstr_imm(";app-encoding=")));
        octstr_delete(*appid_content, 0, pos);         /* the URI part */
	return;
    } 

    i = 0;
    while (i < NUMBER_OF_WINA_URIS) {
        if ((pos = octstr_search(*appid_content, 
                octstr_imm(wina_uri[i]), 0)) >= 0)
            break;
        ++i;
    }

    if (i == NUMBER_OF_WINA_URIS)
        return;
    
    octstr_delete(*appid_content, 0, pos);             /* again the URI */
    if ((coded_value = wsp_string_to_application_id(*appid_content)) >= 0) {
        octstr_destroy(*appid_content);
        *appid_content = octstr_format("%ld", coded_value);
        return;
    }
}

static WAPAddrTuple *addr_tuple_change_cliport(WAPAddrTuple *tuple, long port)
{
    WAPAddrTuple *dubble;

    if (tuple == NULL)
        return NULL;

    dubble = wap_addr_tuple_create(tuple->remote->address,
                                   port,
                                   tuple->local->address,
                                   tuple->local->port);

    return dubble;
}



