/*
 * wsp-session.c - Implement WSP session oriented service
 *
 * Lars Wirzenius
 */


#include <string.h>
#include <limits.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_pdu.h"
#include "wsp_headers.h"
#include "wsp_caps.h"
#include "cookies.h"

/* WAP standard defined values for capabilities */

#define WSP_CAPS_CLIENT_SDU_SIZE	0x00
#define WSP_CAPS_SERVER_SDU_SIZE	0x01
#define WSP_CAPS_PROTOCOL_OPTIONS	0x02
#define WSP_CAPS_METHOD_MOR		0x03
#define WSP_CAPS_PUSH_MOR		0x04
#define WSP_CAPS_EXTENDED_METHODS    	0x05
#define WSP_CAPS_HEADER_CODE_PAGES    	0x06
#define WSP_CAPS_ALIASES	   	0x07



typedef enum {
	#define STATE_NAME(name) name,
	#define ROW(state, event, condition, action, next_state)
	#include "wsp-session-state.h"

	#define STATE_NAME(name) name,
	#define ROW(state, event, condition, action, next_state)
	#include "wsp-method-state.h"

	WSPState_count
} WSPState;


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
static List *session_machines = NULL;
static Counter *session_id_counter = NULL;


static WSPMachine *find_session_machine(WAPEvent *event, WSP_PDU *pdu);
static void handle_session_event(WSPMachine *machine, WAPEvent *event, 
				 WSP_PDU *pdu);
static WSPMachine *machine_create(void);
static void machine_destroy(WSPMachine *p);
#if 0
static void machine_dump(WSPMachine *machine);
#endif

static void handle_method_event(WSPMachine *session, WSPMethodMachine *machine, WAPEvent *event, WSP_PDU *pdu);
static void cant_handle_event(WSPMachine *sm, WAPEvent *event);
static WSPMethodMachine *method_machine_create(WSPMachine *, long);
static void method_machine_destroy(WSPMethodMachine *msm);

static char *wsp_state_to_string(WSPState state);
static long wsp_next_session_id(void);

static List *make_capabilities_reply(WSPMachine *m);
static Octstr *make_connectreply_pdu(WSPMachine *m);

static int transaction_belongs_to_session(void *session, void *tuple);
static int find_by_session_id(void *session, void *idp);
static int same_client(void *sm1, void *sm2);
static WSPMethodMachine *wsp_find_method_machine(WSPMachine *, long id);

static void wsp_disconnect_other_sessions(WSPMachine *sm);
static void wsp_send_abort(long reason, long handle);
static void wsp_abort_session(WSPMachine *sm, long reason);
static void wsp_indicate_disconnect(WSPMachine *sm, long reason);

static void wsp_release_holding_methods(WSPMachine *sm);
static void wsp_abort_methods(WSPMachine *sm, long reason);

static void wsp_method_abort(WSPMethodMachine *msm, long reason);
static void wsp_indicate_method_abort(WSPMethodMachine *msm, long reason);

static void main_thread(void *);
static int id_belongs_to_session (void *, void *);



/***********************************************************************
 * Public functions.
 */


void wsp_session_init(void) {
	queue = list_create();
	list_add_producer(queue);
	session_machines = list_create();
	session_id_counter = counter_create();
	run_status = running;
	gwthread_create(main_thread, NULL);
}


void wsp_session_shutdown(void) {
	WAPEvent *e;
	
	gw_assert(run_status == running);
	run_status = terminating;
	list_remove_producer(queue);
	gwthread_join_every(main_thread);

	while ((e = list_extract_first(queue)) != e)
		wap_event_destroy(e);
	list_destroy(queue);

	debug("wap.wsp", 0, "WSP: %ld session machines left.",
		list_len(session_machines));
	while (list_len(session_machines) > 0)
		machine_destroy(list_get(session_machines, 0));
	list_destroy(session_machines);

	counter_destroy(session_id_counter);
}


void wsp_session_dispatch_event(WAPEvent *event) {
	wap_event_assert(event);
	list_produce(queue, event);
}


/***********************************************************************
 * Local functions
 */


static void main_thread(void *arg) {
	WAPEvent *e;
	WSPMachine *sm;
	WSP_PDU *pdu;
	
	while (run_status == running && (e = list_consume(queue)) != NULL) {
		wap_event_assert(e);
		switch (e->type) {
		case TR_Invoke_Ind:
			pdu = wsp_pdu_unpack(e->u.TR_Invoke_Ind.user_data);
			if (pdu == NULL) {
				warning(0, "WSP: Broken PDU ignored.");
				wap_event_destroy(e);
				continue;
			}
			break;
	
		default:
			pdu = NULL;
			break;
		}
	
		sm = find_session_machine(e, pdu);
		if (sm == NULL) {
			wap_event_destroy(e);
		} else {
			handle_session_event(sm, e, pdu);
		}
		
		wsp_pdu_destroy(pdu);
	}
}


static WSPMachine *find_session_machine(WAPEvent *event, WSP_PDU *pdu) {
	WSPMachine *sm;
	long session_id;
	long handle;
	WAPAddrTuple *tuple;
	
	tuple = NULL;
	session_id = -1;
	handle = -1;
	
	switch (event->type) {
	case TR_Invoke_Ind:
		tuple = wap_addr_tuple_duplicate(
				event->u.TR_Invoke_Ind.addr_tuple);
		break;

	case TR_Result_Cnf:
		tuple = wap_addr_tuple_duplicate(
				event->u.TR_Result_Cnf.addr_tuple);
		break;

	case TR_Abort_Ind:
		tuple = wap_addr_tuple_duplicate(
				event->u.TR_Abort_Ind.addr_tuple);
		break;

	case S_Connect_Res:
		session_id = event->u.S_Connect_Res.session_id;
		break;

	case S_Resume_Res:
		session_id = event->u.S_Resume_Res.session_id;
		break;

	case Disconnect_Event:
		session_id = event->u.Disconnect_Event.session_id;
		break;

	case Suspend_Event:
		session_id = event->u.Suspend_Event.session_id;
		break;

	case S_MethodInvoke_Res:
		session_id = event->u.S_MethodInvoke_Res.session_id;
		break;

	case S_MethodResult_Req:
		session_id = event->u.S_MethodResult_Req.session_id;
		break;

	default:
		error(0, "WSP: Cannot find machine for %s event",
			wap_event_name(event->type));
	}
	
	gw_assert(session_id != -1 || handle != -1 || tuple != NULL);
	if (handle != -1 && wtp_get_address_tuple(handle, &tuple) == -1) {
		error(0, "Couldn't find WTP state machine %ld.", handle);
		error(0, "This is an internal error.");
		wap_event_dump(event);
		panic(0, "foo");
		return NULL;
	}
	gw_assert(tuple != NULL || session_id != -1);

	/* Pre-state-machine tests, according to 7.1.5.  After the tests,
	 * caller will pass the event to sm if sm is not NULL. */
	sm = NULL;
	/* First test is for MRUEXCEEDED, and we don't have a MRU */

	/* Second test is for class 2 TR-Invoke.ind with Connect PDU */
	if (event->type == TR_Invoke_Ind &&
	    event->u.TR_Invoke_Ind.tcl == 2 &&
	    pdu->type == Connect) {
			/* Create a new session, even if there is already
			 * a session open for this address.  The new session
			 * will take care of killing the old ones. */
			sm = machine_create();
			gw_assert(tuple != NULL);
			sm->addr_tuple = wap_addr_tuple_duplicate(tuple);
			sm->connect_handle = event->u.TR_Invoke_Ind.handle;
	/* Third test is for class 2 TR-Invoke.ind with Resume PDU */
	} else if (event->type == TR_Invoke_Ind &&
		   event->u.TR_Invoke_Ind.tcl == 2 &&
	  	   pdu->type == Resume) {
		/* Pass to session identified by session id, not
		 * the address tuple. */
		session_id = pdu->u.Resume.sessionid;
		sm = list_search(session_machines, &session_id,
				find_by_session_id);
		if (sm == NULL) {
			/* No session; TR-Abort.req(DISCONNECT) */
			wsp_send_abort(WSP_ABORT_DISCONNECT,
				event->u.TR_Invoke_Ind.handle);
		}
	/* Fourth test is for a class 1 or 2 TR-Invoke.Ind with no
	 * session for that address tuple.  We also handle class 0
	 * TR-Invoke.ind here by ignoring them; this seems to be
	 * an omission in the spec table. */
	} else if (event->type == TR_Invoke_Ind) {
		sm = list_search(session_machines, tuple,
				 transaction_belongs_to_session);
		if (sm == NULL && (event->u.TR_Invoke_Ind.tcl == 1 ||
				event->u.TR_Invoke_Ind.tcl == 2)) {
			wsp_send_abort(WSP_ABORT_DISCONNECT,
				event->u.TR_Invoke_Ind.handle);
		}
	/* Other tests are for events not handled by the state tables;
	 * do those later, after we've tried to handle them. */
	} else {
		if (session_id != -1) {
			sm = list_search(session_machines, &session_id,
				find_by_session_id);
		} else {
			sm = list_search(session_machines, tuple,
				transaction_belongs_to_session);
		}
		/* The table doesn't really say what we should do with
		 * non-Invoke events for which there is no session.  But
		 * such a situation means there is an error _somewhere_
		 * in the gateway. */
		if (sm == NULL) {
			error(0, "WSP: Cannot find session machine for event.");
			wap_event_dump(event);
		}
	}

	wap_addr_tuple_destroy(tuple);
	return sm;
}


static void handle_session_event(WSPMachine *sm, WAPEvent *current_event, 
WSP_PDU *pdu) {
	debug("wap.wsp", 0, "WSP: machine %p, state %s, event %s",
		(void *) sm,
		wsp_state_to_string(sm->state), 
		wap_event_name(current_event->type));

	#define STATE_NAME(name)
	#define ROW(state_name, event, condition, action, next_state) \
		{ \
			struct event *e; \
			e = &current_event->u.event; \
			if (sm->state == state_name && \
			   current_event->type == event && \
			   (condition)) { \
				action \
				sm->state = next_state; \
				debug("wap.wsp", 0, "WSP %ld: New state %s", \
					sm->session_id, #next_state); \
				goto end; \
			} \
		}
	#include "wsp-session-state.h"
	
	cant_handle_event(sm, current_event);

end:
	wap_event_destroy(current_event);

	if (sm->state == NULL_SESSION)
		machine_destroy(sm);
}


static void cant_handle_event(WSPMachine *sm, WAPEvent *event) {
	/* We do the rest of the pre-state-machine tests here.  The first
	 * four were done in find_session_machine().  The fifth is a
	 * class 1 or 2 TR-Invoke.ind not handled by the state tables. */
	if (event->type == TR_Invoke_Ind &&
	    (event->u.TR_Invoke_Ind.tcl == 1 ||
	     event->u.TR_Invoke_Ind.tcl == 2)) {
		warning(0, "WSP: Can't handle TR-Invoke.ind, aborting transaction.");
		debug("wap.wsp", 0, "WSP: The unhandled event:");
		wap_event_dump(event);
		wsp_send_abort(WSP_ABORT_PROTOERR,
			event->u.TR_Invoke_Ind.handle);
	/* The sixth is a class 0 TR-Invoke.ind not handled by state tables. */
	} else if (event->type == TR_Invoke_Ind) {
		warning(0, "WSP: Can't handle TR-Invoke.ind, ignoring.");
		debug("wap.wsp", 0, "WSP: The ignored event:");
		wap_event_dump(event);
	/* The seventh is any other event not handled by state tables. */
	} else {
		error(0, "WSP: Can't handle event. Aborting session.");
		debug("wap.wsp", 0, "WSP: The unhandled event:");
		wap_event_dump(event);
		/* TR-Abort.req(PROTOERR) if it is some other transaction
		 * event than abort. */
		/* Currently that means TR-Result.cnf, because we already
		 * tested for Invoke. */
		/* XXX We need a better way to get at event values than
		 * by hardcoding the types. */
		if (event->type == TR_Result_Cnf) {
			wsp_send_abort(WSP_ABORT_PROTOERR,
				event->u.TR_Result_Cnf.handle);
		}
		/* Abort(PROTOERR) all method and push transactions */
		wsp_abort_methods(sm, WSP_ABORT_PROTOERR);
		/* S-Disconnect.ind(PROTOERR) */
		wsp_indicate_disconnect(sm, WSP_ABORT_PROTOERR);
	}
}


static WSPMachine *machine_create(void) {
	WSPMachine *p;
	
	p = gw_malloc(sizeof(WSPMachine));
	debug("wap.wsp", 0, "WSP: Created WSPMachine %p", (void *) p);
	
	#define INTEGER(name) p->name = 0;
	#define OCTSTR(name) p->name = NULL;
	#define HTTPHEADERS(name) p->name = NULL;
	#define ADDRTUPLE(name) p->name = NULL;
	#define METHODMACHINES(name) p->name = list_create();
	#define CAPABILITIES(name) p->name = NULL;
	#define COOKIES(name) p->name = NULL;
	#define MACHINE(fields) fields
	#include "wsp-session-machine.h"
	
	p->state = NULL_SESSION;

	/* set capabilities to default values (defined in 1.1) */

	p->client_SDU_size = 1400;
	p->MOR_push = 1;
	
	/* Insert new machine at the _front_, because 1) it's more likely
	 * to get events than old machines are, so this speeds up the linear
	 * search, and 2) we want the newest machine to get any method
	 * invokes that come through before the Connect is established. */
	list_insert(session_machines, 0, p);

	return p;
}


static void wsp_session_destroy_methods(List *machines) {
	if (list_len(machines) > 0) {
		warning(0, "Destroying WSP session with %ld active methods\n",
			list_len(machines));
	}

	while (list_len(machines) > 0)
		method_machine_destroy(list_extract_first(machines));
	list_destroy(machines);
}


static void machine_destroy(WSPMachine *p) {
	debug("wap.wsp", 0, "Destroying WSPMachine %p", (void *) p);
	list_delete_equal(session_machines, p);

	#define INTEGER(name) p->name = 0;
	#define OCTSTR(name) octstr_destroy(p->name);
	#define HTTPHEADERS(name) http_destroy_headers(p->name);
	#define ADDRTUPLE(name) wap_addr_tuple_destroy(p->name);
	#define METHODMACHINES(name) wsp_session_destroy_methods(p->name);
	#define CAPABILITIES(name) wsp_cap_destroy_list(p->name);
	#define COOKIES(name) cookies_destroy(p->name);
	#define MACHINE(fields) fields
	#include "wsp-session-machine.h"
	gw_free(p);
}


#if 0
static void machine_dump(WSPMachine *machine) {
	WSPMachine *p;

	p = machine;
	debug("wap.wsp", 0, "WSPMachine %p dump starts:", (void *) p);
	#define MUTEX(name) 
	#define INTEGER(name) debug("wap.wsp", 0, "  %s: %ld", #name, p->name);
	#define OCTSTR(name) \
		debug("wap.wsp", 0, "  %s:", #name); \
		octstr_dump(p->name, 1);
	#define METHOD_POINTER(name) \
		debug("wap.wsp", 0, "  %s: %p", #name, (void *) p->name);
	#define EVENT_POINTER(name) \
		debug("wap.wsp", 0, "  %s: %p", #name, (void *) p->name);
	#define SESSION_POINTER(name) \
		debug("wap.wsp", 0, "  %s: %p", #name, (void *) p->name);
	#define HTTPHEADER(name) \
		debug("wap.wsp", 0, "  %s: %p", #name, (void *) p->name);
	#define LIST(name) \
		debug("wap.wsp", 0, "  %s: %p", #name, (void *) p->name);
	#define METHOD_MACHINE(fields)
	#include "wsp_machine-decl.h"
	debug("wap.wsp", 0, "WSPMachine dump ends.");
}
#endif


struct msm_pattern {
	WAPAddrTuple *addr_tuple;
	long msmid, tid;
};


/* This function does NOT consume its event; it leaves that task up
 * to the parent session */
static void handle_method_event(WSPMachine *sm, WSPMethodMachine *msm, 
WAPEvent *current_event, WSP_PDU *pdu) {

	if (msm == NULL) {
		warning(0, "No method machine for event.");
		wap_event_dump(current_event);
		return;
	}
		
	debug("wap.wsp", 0, "WSP: method %ld, state %s, event %s",
		msm->transaction_id, wsp_state_to_string(msm->state), 
		wap_event_name(current_event->type));

	gw_assert(sm->session_id == msm->session_id);

	#define STATE_NAME(name)
	#define ROW(state_name, event, condition, action, next_state) \
		{ \
			struct event *e; \
			e = &current_event->u.event; \
			if (msm->state == state_name && \
			   current_event->type == event && \
			   (condition)) { \
				action \
				msm->state = next_state; \
				debug("wap.wsp", 0, "WSP %ld/%ld: New method state %s", \
					msm->session_id, msm->transaction_id, #next_state); \
				goto end; \
			} \
		}
	#include "wsp-method-state.h"
	
	cant_handle_event(sm, current_event);

end:
	if (msm->state == NULL_METHOD) {
		method_machine_destroy(msm);
		list_delete_equal(sm->methodmachines, msm);
	}
}


static WSPMethodMachine *method_machine_create(WSPMachine *sm,
			long wtp_handle) {
	WSPMethodMachine *msm;
	
	msm = gw_malloc(sizeof(*msm));
	
	#define INTEGER(name) msm->name = 0;
	#define ADDRTUPLE(name) msm->name = NULL;
	#define EVENT(name) msm->name = NULL;
	#define MACHINE(fields) fields
	#include "wsp-method-machine.h"
	
	msm->transaction_id = wtp_handle;
	msm->state = NULL_METHOD;
	msm->addr_tuple = wap_addr_tuple_duplicate(sm->addr_tuple);
	msm->session_id = sm->session_id;

	list_append(sm->methodmachines, msm);

	return msm;
}



static void method_machine_destroy(WSPMethodMachine *msm) {
	if (msm == NULL)
		return;

	debug("wap.wsp", 0, "Destroying WSPMethodMachine %ld",
			msm->transaction_id);

	#define INTEGER(name)
	#define ADDRTUPLE(name) wap_addr_tuple_destroy(msm->name);
	#define EVENT(name) wap_event_destroy(msm->name);
	#define MACHINE(fields) fields
	#include "wsp-method-machine.h"

	gw_free(msm);
}


static char *wsp_state_to_string(WSPState state) {
	switch (state) {
	#define STATE_NAME(name) case name: return #name;
	#define ROW(state, event, cond, stmt, next_state)
	#include "wsp-session-state.h"

	#define STATE_NAME(name) case name: return #name;
	#define ROW(state, event, cond, stmt, next_state)
	#include "wsp-method-state.h"

	default:
		return "unknown wsp state";
	}
}


static long wsp_next_session_id(void) {
	return counter_increase(session_id_counter);
}


static void sanitize_capabilities(List *caps, WSPMachine *m) {
	long i;
	Capability *cap;
	unsigned long uint;

	for (i = 0; i < list_len(caps); i++) {
		cap = list_get(caps, i);

		/* We only know numbered capabilities.  Let the application
		 * layer negotiate whatever it wants for unknown ones. */
		if (cap->name != NULL)
			continue;

		switch (cap->id) {
		case WSP_CAPS_CLIENT_SDU_SIZE:
			/* Check if it's a valid uintvar.  The value is the
			 * max SDU size we will send, and there's no
			 * internal limit to that, so accept any value. */
			if (cap->data != NULL &&
			    octstr_extract_uintvar(cap->data, &uint, 0) < 0)
				goto bad_cap;
			else
				m->client_SDU_size = uint;
			break;

		case WSP_CAPS_SERVER_SDU_SIZE:
			/* Check if it's a valid uintvar */
			if (cap->data != NULL &&
			    (octstr_extract_uintvar(cap->data, &uint, 0) < 0))
				goto bad_cap;
			/* XXX Our MRU is not quite unlimited, since we
			 * use signed longs in the library functions --
			 * should we make sure we limit the reply value
			 * to LONG_MAX?  (That's already a 2GB packet) */
			break;

		case WSP_CAPS_PROTOCOL_OPTIONS:
			/* Currently we don't support any Push, nor
			 * session resume, nor acknowledgement headers,
			 * so make sure those bits are not set. */
			if (cap->data != NULL && octstr_len(cap->data) > 0
			   && (octstr_get_char(cap->data, 0) & 0xf0) != 0) {
				warning(0, "WSP: Application layer tried to "
					"negotiate protocol options.");
				octstr_set_bits(cap->data, 0, 4, 0);
			}
			break;

		case WSP_CAPS_EXTENDED_METHODS:
			/* XXX Check format here */
			break;

		
		case WSP_CAPS_HEADER_CODE_PAGES:
			/* We don't support any yet, so don't let this
			 * be negotiated. */
			if (cap->data)
				goto bad_cap;
			break;
		}
		continue;

	bad_cap:
		error(0, "WSP: Found illegal value in capabilities reply.");
		wsp_cap_dump(cap);
		list_delete(caps, i, 1);
		i--;
		wsp_cap_destroy(cap);
		continue;
	}
}


static void reply_known_capabilities(List *caps, List *req, WSPMachine *m) {
	unsigned long uint;
	Capability *cap;
	Octstr *data;

	if (wsp_cap_count(caps, WSP_CAPS_CLIENT_SDU_SIZE, NULL) == 0) {
		if (wsp_cap_get_client_sdu(req, &uint) > 0) {
			/* Accept value if it is not silly. */
			if ((uint >= 256 && uint < LONG_MAX) || uint == 0) {
				m->client_SDU_size = uint;
			}
		}
		/* Reply with the client SDU we decided on */
		data = octstr_create_empty();
		octstr_append_uintvar(data, m->client_SDU_size);
		cap = wsp_cap_create(WSP_CAPS_CLIENT_SDU_SIZE,
			NULL, data);
		list_append(caps, cap);
	}

	if (wsp_cap_count(caps, WSP_CAPS_SERVER_SDU_SIZE, NULL) == 0) {
		/* We don't care what the client sent us, but we can
		 * handle any size packet, so we tell the client that. */
		data = octstr_create_empty();
		octstr_append_uintvar(data, 0);
		cap = wsp_cap_create(WSP_CAPS_SERVER_SDU_SIZE, NULL, data);
		list_append(caps, cap);
	}

	/* Currently we cannot handle any protocol options */
	if (wsp_cap_count(caps, WSP_CAPS_PROTOCOL_OPTIONS, NULL) == 0) {
		data = octstr_create_empty();
		octstr_append_char(data, 0);
		cap = wsp_cap_create(WSP_CAPS_PROTOCOL_OPTIONS, NULL, data);
		list_append(caps, cap);
	}

	/* Accept any Method-MOR the client sent; if it sent none,
	 * reply that we can handle any number of Method requests.
	 * ("any" is 255 because the encoding doesn't go higher) */
	if (wsp_cap_count(caps, WSP_CAPS_METHOD_MOR, NULL) == 0) {
		if (wsp_cap_get_method_mor(req, &uint) <= 0) {
			uint = 255;
		}
		data = octstr_create_empty();
		octstr_append_char(data, uint);
		cap = wsp_cap_create(WSP_CAPS_METHOD_MOR, NULL, data);
		list_append(caps, cap);
	}

	/* We will never send any Push requests because we don't support
	 * that yet.  But we already specified that in protocol options;
	 * so, pretend we do, and handle the value that way. */
	if (wsp_cap_count(caps, WSP_CAPS_PUSH_MOR, NULL) == 0) {
		if (wsp_cap_get_push_mor(req, &uint) > 0) {
			m->MOR_push = uint;
		}
		data = octstr_create_empty();
		octstr_append_char(data, m->MOR_push);
		cap = wsp_cap_create(WSP_CAPS_PUSH_MOR, NULL, data);
		list_append(caps, cap);
	}

	/* Supporting extended methods is up to the application layer,
	 * not up to us.  If the application layer didn't specify any,
	 * then we refuse whatever the client requested.  The default
	 * is to support none, so we don't really have to add anything here. */

	/* We do not support any header code pages.  sanitize_capabilities
	 * must have already deleted any reply that indicates otherwise.
	 * Again, not adding anything here is the same as refusing support. */

	/* Listing aliases is something the application layer can do if
	 * it wants to.  We don't care. */
}


/* Generate a refusal for all requested capabilities that are not
 * replied to. */
static void refuse_unreplied_capabilities(List *caps, List *req) {
	long i, len;
	Capability *cap;

	len = list_len(req);
	for (i = 0; i < len; i++) {
		cap = list_get(req, i);
		if (wsp_cap_count(caps, cap->id, cap->name) == 0) {
			cap = wsp_cap_create(cap->id, cap->name, NULL);
			list_append(caps, cap);
		}
	}
}


static int is_default_cap(Capability *cap) {
	unsigned long uint;

	/* All unknown values are empty by default */
	if (cap->name != NULL || cap->id < 0 || cap->id >= WSP_NUM_CAPS)
		return cap->data == NULL || octstr_len(cap->data) == 0;

	switch (cap->id) {
	case WSP_CAPS_CLIENT_SDU_SIZE:
	case WSP_CAPS_SERVER_SDU_SIZE:
		return (cap->data != NULL &&
		    octstr_extract_uintvar(cap->data, &uint, 0) >= 0 &&
		    uint == 1400);
	case WSP_CAPS_PROTOCOL_OPTIONS:
		return cap->data != NULL && octstr_get_char(cap->data, 0) == 0;
	case WSP_CAPS_METHOD_MOR:
	case WSP_CAPS_PUSH_MOR:
		return cap->data != NULL && octstr_get_char(cap->data, 0) == 1;
	case WSP_CAPS_EXTENDED_METHODS:
	case WSP_CAPS_HEADER_CODE_PAGES:
	case WSP_CAPS_ALIASES:
		return cap->data == NULL || octstr_len(cap->data) == 0;
	default:
		return 0;
	}
}


/* Remove any replies that have no corresponding request and that
 * are equal to the default. */
static void strip_default_capabilities(List *caps, List *req) {
	long i;
	Capability *cap;
	int count;

	/* Hmm, this is an O(N*N) operation, which may be bad. */

	i = 0;
	while (i < list_len(caps)) {
		cap = list_get(caps, i);

		count = wsp_cap_count(req, cap->id, cap->name);
		if (count == 0 && is_default_cap(cap)) {
			list_delete(caps, i, 1);
			wsp_cap_destroy(cap);
		} else {
			i++;
		}
	}
}


static List *make_capabilities_reply(WSPMachine *m) {
	List *caps;

	/* In principle, copy the application layer's capabilities
	 * response, add refusals for all unknown requested capabilities,
	 * and add responses for all known capabilities that are
	 * not already responded to.  Then eliminate any replies that
 	 * would have no effect because they are equal to the default. */

	caps = wsp_cap_duplicate_list(m->reply_caps);

	/* Don't let the application layer negotiate anything we
	 * cannot handle.  Also parse the values it set if we're
	 * interested. */
	sanitize_capabilities(caps, m);

	/* Add capability records for all capabilities we know about
	 * that are not already in the reply list. */
	reply_known_capabilities(caps, m->request_caps, m);

	/* All remaining capabilities in the request list that are
	 * not in the reply list at this point must be unknown ones
	 * that we want to refuse. */
	refuse_unreplied_capabilities(caps, m->request_caps);

	/* Now eliminate replies that would be equal to the requested
	 * value, or (if there was none) to the default value. */
	strip_default_capabilities(caps, m->request_caps);

	return caps;
}


static Octstr *make_connectreply_pdu(WSPMachine *m) {
	WSP_PDU *pdu;
	Octstr *os;
	List *caps;
	
	pdu = wsp_pdu_create(ConnectReply);

	pdu->u.ConnectReply.sessionid = m->session_id;

	caps = make_capabilities_reply(m);
	pdu->u.ConnectReply.capabilities = wsp_cap_pack_list(caps);
	wsp_cap_destroy_list(caps);
	pdu->u.ConnectReply.headers = NULL;
	
	os = wsp_pdu_pack(pdu);
	wsp_pdu_destroy(pdu);

	return os;
}


static int transaction_belongs_to_session(void *wsp_ptr, void *tuple_ptr) {
	WSPMachine *wsp;
	WAPAddrTuple *tuple;
	
	wsp = wsp_ptr;
	tuple = tuple_ptr;

	return wap_addr_tuple_same(wsp->addr_tuple, tuple);
}


static int find_by_session_id(void *wsp_ptr, void *id_ptr) {
	WSPMachine *wsp = wsp_ptr;
	long *idp = id_ptr;
	
	return wsp->session_id == *idp;
}


static int find_by_method_id(void *wspm_ptr, void *id_ptr) {
	WSPMethodMachine *msm = wspm_ptr;
	long *idp = id_ptr;

	return msm->transaction_id == *idp;
}


static WSPMethodMachine *wsp_find_method_machine(WSPMachine *sm, long id) {
	return list_search(sm->methodmachines, &id, find_by_method_id);
}


static int same_client(void *a, void *b) {
	WSPMachine *sm1, *sm2;
	
	sm1 = a;
	sm2 = b;
	return wap_addr_tuple_same(sm1->addr_tuple, sm2->addr_tuple);
}


static void wsp_disconnect_other_sessions(WSPMachine *sm) {
	List *old_sessions;
	WAPEvent *disconnect;
	WSPMachine *sm2;
	long i;

	old_sessions = list_search_all(session_machines, sm, same_client);
	if (old_sessions == NULL)
		return;

	for (i = 0; i < list_len(old_sessions); i++) {
		sm2 = list_get(old_sessions, i);
		if (sm2 != sm) {
			disconnect = wap_event_create(Disconnect_Event);
			handle_session_event(sm2, disconnect, NULL);
		}
	}

	list_destroy(old_sessions);
}


static void wsp_send_abort(long reason, long handle) {
	WAPEvent *wtp_event;

	wtp_event = wap_event_create(TR_Abort_Req);
	wtp_event->u.TR_Abort_Req.abort_type = 0x01;
	wtp_event->u.TR_Abort_Req.abort_reason = reason;
	wtp_event->u.TR_Abort_Req.handle = handle;
	wtp_dispatch_event(wtp_event);
}


static void wsp_abort_session(WSPMachine *sm, long reason) {
	wsp_send_abort(reason, sm->connect_handle);
}


static void wsp_indicate_disconnect(WSPMachine *sm, long reason) {
	WAPEvent *new_event;

	new_event = wap_event_create(S_Disconnect_Ind);
	new_event->u.S_Disconnect_Ind.reason_code = reason;
	new_event->u.S_Disconnect_Ind.redirect_security = 0;
	new_event->u.S_Disconnect_Ind.redirect_addresses = 0;
	new_event->u.S_Disconnect_Ind.error_headers = NULL;
	new_event->u.S_Disconnect_Ind.error_body = NULL;
	new_event->u.S_Disconnect_Ind.session_id = sm->session_id;
	wap_appl_dispatch(new_event);
}


static void wsp_method_abort(WSPMethodMachine *msm, long reason) {
	WAPEvent *wtp_event;

	/* Send TR-Abort.req(reason) */
	wtp_event = wap_event_create(TR_Abort_Req);
	/* FIXME: Specs are unclear about this; we may indeed have to
	 * guess abort whether this is a WSP or WTP level abort code */
	if (reason < WSP_ABORT_PROTOERR) {
		wtp_event->u.TR_Abort_Req.abort_type = 0x00;
	} else {
		wtp_event->u.TR_Abort_Req.abort_type = 0x01;
	}
	wtp_event->u.TR_Abort_Req.abort_reason = reason;
	wtp_event->u.TR_Abort_Req.handle = msm->transaction_id;

	wtp_dispatch_event(wtp_event);
}


static void wsp_indicate_method_abort(WSPMethodMachine *msm, long reason) {
	WAPEvent *new_event;

	/* Send S-MethodAbort.ind(reason) */
	new_event = wap_event_create(S_MethodAbort_Ind);
	new_event->u.S_MethodAbort_Ind.transaction_id = msm->transaction_id;
	new_event->u.S_MethodAbort_Ind.reason = reason;
	new_event->u.S_MethodAbort_Ind.session_id = msm->session_id;
	wap_appl_dispatch(new_event);
}

	
static int method_is_holding(void *item, void *pattern) {
	WSPMethodMachine *msm = item;

	return msm->state == HOLDING;
}


static void wsp_release_holding_methods(WSPMachine *sm) {
	WAPEvent *release;
	WSPMethodMachine *msm;
	List *holding;
	long i, len;

	holding = list_search_all(sm->methodmachines, NULL, method_is_holding);
	if (holding == NULL)
		return;

	/* We can re-use this because wsp_handle_method_event does not
	 * destroy its event */
	release = wap_event_create(Release_Event);

	len = list_len(holding);
	for (i = 0; i < len; i++) {
		msm = list_get(holding, i);
		handle_method_event(sm, msm, release, NULL);
	}
	list_destroy(holding);
	wap_event_destroy(release);
}


static void wsp_abort_methods(WSPMachine *sm, long reason) {
	WAPEvent *ab;
	WSPMethodMachine *msm;
	long i, len;

	ab = wap_event_create(Abort_Event);
	ab->u.Abort_Event.reason = reason;

	/* This loop goes backward because it has to deal with the
	 * possibility of method machines disappearing after their event. */
	len = list_len(sm->methodmachines);
	for (i = len - 1; i >= 0; i--) {
		msm = list_get(sm->methodmachines, i);
		handle_method_event(sm, msm, ab, NULL);
	}

	wap_event_destroy(ab);
}

WSPMachine *find_session_machine_by_id (int id) {

	return list_search(session_machines, &id, id_belongs_to_session);
}

static int id_belongs_to_session (void *wsp_ptr, void *pid) {
	WSPMachine *wsp;
	int *id;

	wsp = wsp_ptr;
	id = (int *) pid;

	if (*id == wsp->session_id) return 1;
	return 0;
}
