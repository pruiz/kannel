/*
 * wsp-session.c - Implement WSP session oriented service
 *
 * Lars Wirzenius
 */


#include <string.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_pdu.h"
#include "wsp_headers.h"

/* WAP standard defined values for capabilities */

#define WSP_CAPS_CLIENT_SDU_SIZE	0x00
#define WSP_CAPS_SERVER_SDU_SIZE	0x01
#define WSP_CAPS_PROTOCOL_OPTIONS	0x02
#define WSP_CAPS_METHOD_MOR		0x03
#define WSP_CAPS_PUSH_MOR		0x04
#define WSP_CAPS_EXTENDED_METHODS    	0x05
#define WSP_CAPS_HEADER_CODE_PAGES    	0x06
#define WSP_CAPS_ALIASES	   	0x07



enum {
	Bad_PDU = -1,
	Connect_PDU = 0x01,
	ConnectReply_PDU = 0x02,
	Redirect_PDU = 0x03,
	Reply_PDU = 0x04,
	Disconnect_PDU = 0x05,
	Push_PDU = 0x06,
	ConfirmedPush_PDU = 0x07,
	Suspend_PDU = 0x08,
	Resume_PDU = 0x09,
	Get_PDU = 0x40,
	Options_PDU = 0x41,
	Head_PDU = 0x42,
	Delete_PDU = 0x43,
	Trace_PDU = 0x44,
	Post_PDU = 0x60,
	Put_PDU = 0x61
};

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
static List *method_machines = NULL;
static Counter *session_id_counter = NULL;


static WSPMachine *find_session_machine(WAPEvent *event, WSP_PDU *pdu);
static void handle_session_event(WSPMachine *machine, WAPEvent *event, 
				 WSP_PDU *pdu);
static WSPMachine *machine_create(void);
static void machine_destroy(WSPMachine *p);
#if 0
static void machine_dump(WSPMachine *machine);
#endif

static WSPMethodMachine *find_method_machine(WAPEvent *event);
static void handle_method_event(WSPMethodMachine *machine, WAPEvent *event);
static WSPMethodMachine *method_machine_create(WAPAddrTuple *, long);
static void method_machine_destroy(WSPMethodMachine *msm);

static void unpack_caps(Octstr *caps, WSPMachine *m);

static int unpack_uint8(unsigned long *u, Octstr *os, int *off);
static int unpack_uintvar(unsigned long *u, Octstr *os, int *off);

static char *wsp_state_to_string(WSPState state);
static long wsp_next_session_id(void);

static void append_uint8(Octstr *pdu, long n);
static void append_octstr(Octstr *pdu, Octstr *os);

static Octstr *make_connectreply_pdu(WSPMachine *m, long session_id);

static long new_server_transaction_id(void);
static int transaction_belongs_to_session(void *session, void *tuple);
static int same_client(void *sm1, void *sm2);
static int same_method_machine(void *, void *);

static void main_thread(void *);



/***********************************************************************
 * Public functions.
 */


void wsp_session_init(void) {
	queue = list_create();
	list_add_producer(queue);
	session_machines = list_create();
	method_machines = list_create();
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

	debug("wap.wsp", 0, "WSP: %ld method machines left.",
		list_len(method_machines));
	while (list_len(method_machines) > 0)
		method_machine_destroy(list_get(method_machines, 0));
	list_destroy(method_machines);

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
	WSPMethodMachine *msm;
	WSP_PDU *pdu;
	
	while (run_status == running && (e = list_consume(queue)) != NULL) {
		wap_event_assert(e);
		switch (e->type) {
		case TR_Invoke_Ind:
			pdu = wsp_pdu_unpack(e->u.TR_Invoke_Ind.user_data);
			if (pdu == NULL) {
				warning(0, "WSP: Broken PDU ignored.");
				return;
			}
			break;
	
		default:
			pdu = NULL;
			break;
		}
	
		sm = find_session_machine(e, pdu);
		if (sm != NULL)
			handle_session_event(sm, e, pdu);
		else {
			msm = find_method_machine(e);
			if (msm != NULL)
				handle_method_event(msm, e);
			else {
				warning(0, "WSP: Ignoring event.");
				wap_event_dump(e);
			}
		}
		
		wsp_pdu_destroy(pdu);
	}
}



static WSPMachine *find_session_machine(WAPEvent *event, WSP_PDU *pdu) {
	WSPMachine *sm;
	long mid;
	WAPAddrTuple *tuple;
	
	tuple = NULL;
	mid = -1;
	
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
		mid = event->u.S_Connect_Res.mid;
		break;

	case S_MethodInvoke_Ind:
		mid = event->u.S_MethodInvoke_Ind.mid;
		break;

	default:
		return NULL;
	}
	
	gw_assert(mid != -1 || tuple != NULL);
	if (mid != -1 && wtp_get_address_tuple(mid, &tuple) == -1) {
		error(0, "Couldn't find WTP state machine %ld.", mid);
		error(0, "This is an internal error.");
		wap_event_dump(event);
		panic(0, "foo");
		return NULL;
	}
	gw_assert(tuple != NULL);

	/* XXX this should probably be moved to a condition function --liw */
	if (event->type == TR_Invoke_Ind &&
	    event->u.TR_Invoke_Ind.tcl == 2 &&
	    pdu->type == Connect) {
		/* Client wants to start new session. Igore existing
		   machines. */
		sm = NULL;
	} else {
		sm = list_search(session_machines, tuple,
				 transaction_belongs_to_session);
		if (sm != NULL &&
		    event->type == TR_Result_Cnf && 
		    event->u.TR_Result_Cnf.tid != sm->connect_tid) {
			wap_addr_tuple_destroy(tuple);
			return NULL; /* Must be for a method machine, then */
		}
	}

	if (sm == NULL) {
		sm = machine_create();
		sm->addr_tuple = tuple;
	} else
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
				debug("wap.wsp", 0, "WSP: New state: " \
					#next_state); \
				goto end; \
			} \
		}
	#include "wsp-session-state.h"
	
	if (current_event->type == TR_Invoke_Ind) {
		WAPEvent *abort;
		
		error(0, "WSP: Can't handle TR-Invoke.ind, aborting transaction.");
		abort = wap_event_create(TR_Abort_Req);
		abort->u.TR_Abort_Req.tid = current_event->u.TR_Invoke_Ind.tid;
		abort->u.TR_Abort_Req.abort_type = 0x01; /* USER */
		abort->u.TR_Abort_Req.abort_reason = 0xE0; /* PROTOERR */
		abort->u.TR_Abort_Req.mid = current_event->u.TR_Invoke_Ind.mid;

		wtp_dispatch_event(abort);
		sm->state = NULL_SESSION;
	} else {
		error(0, "WSP: Can't handle event.");
		debug("wap.wsp", 0, "WSP: The unhandled event:");
		wap_event_dump(current_event);
	}

end:
	wap_event_destroy(current_event);

	if (sm->state == NULL_SESSION)
		machine_destroy(sm);
}




static WSPMachine *machine_create(void) {
	WSPMachine *p;
	
	p = gw_malloc(sizeof(WSPMachine));
	debug("wap.wsp", 0, "WSP: Created WSPMachine %p", (void *) p);
	
	#define INTEGER(name) p->name = 0;
	#define OCTSTR(name) p->name = NULL;
	#define HTTPHEADERS(name) p->name = NULL;
	#define ADDRTUPLE(name) p->name = NULL;
	#define MACHINE(fields) fields
	#include "wsp-session-machine.h"
	
	p->state = NULL_SESSION;

	/* set capabilities to default values (defined in 1.1) */

	p->client_SDU_size = 1400;
	p->server_SDU_size = 1400;
        /* p->protocol_options = 0x00;	 */
	p->MOR_method = 1;
	p->MOR_push = 1;
	
	list_append(session_machines, p);

	return p;
}


static void machine_destroy(WSPMachine *p) {
	debug("wap.wsp", 0, "Destroying WSPMachine %p", (void *) p);
	list_delete_equal(session_machines, p);

	#define INTEGER(name) p->name = 0;
	#define OCTSTR(name) octstr_destroy(p->name);
	#define HTTPHEADERS(name) http_destroy_headers(p->name);
	#define ADDRTUPLE(name) wap_addr_tuple_destroy(p->name);
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
	#define SESSION_MACHINE(fields) fields
	#define METHOD_MACHINE(fields)
	#include "wsp_machine-decl.h"
	debug("wap.wsp", 0, "WSPMachine dump ends.");
}
#endif


struct msm_pattern {
	WAPAddrTuple *addr_tuple;
	long msmid, tid;
};


static WSPMethodMachine *find_method_machine(WAPEvent *event) {
	struct msm_pattern pat;

	pat.msmid = -1;
	pat.tid = -1;
	pat.addr_tuple = NULL;

	switch (event->type) {
	case Release:
		pat.msmid = event->u.Release.msmid;
		break;

	case S_MethodInvoke_Res:
		pat.msmid = event->u.S_MethodInvoke_Res.msmid;
		break;

	case S_MethodResult_Req:
		pat.msmid = event->u.S_MethodResult_Req.msmid;
		break;

	case TR_Result_Cnf:
		pat.addr_tuple = event->u.TR_Result_Cnf.addr_tuple;
		pat.tid = event->u.TR_Result_Cnf.tid;
		break;

	default:
		return NULL;
	}
	
	return list_search(method_machines, &pat, same_method_machine);
}


static void handle_method_event(WSPMethodMachine *msm, 
WAPEvent *current_event) {
	debug("wap.wsp", 0, "WSP: method machine %ld, state %s, event %s",
		msm->id, wsp_state_to_string(msm->state), 
		wap_event_name(current_event->type));

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
				debug("wap.wsp", 0, "WSP: New method state: " \
					#next_state); \
				goto end; \
			} \
		}
	#include "wsp-method-state.h"
	
	error(0, "WSP: Can't handle method event.");
	debug("wap.wsp", 0, "WSP: The unhandled event:");
	wap_event_dump(current_event);

end:
	wap_event_destroy(current_event);

	if (msm->state == NULL_METHOD)
		method_machine_destroy(msm);
}


static WSPMethodMachine *method_machine_create(WAPAddrTuple *addr_tuple,
long tid) {
	WSPMethodMachine *msm;
	
	msm = gw_malloc(sizeof(*msm));
	
	#define INTEGER(name) msm->name = 0;
	#define ADDRTUPLE(name) msm->name = NULL;
	#define MACHINE(fields) fields
	#include "wsp-method-machine.h"
	
	msm->state = NULL_METHOD;
	msm->id = counter_increase(session_id_counter);
	msm->addr_tuple = wap_addr_tuple_duplicate(addr_tuple);
	msm->tid = tid;

	return msm;
}



static void method_machine_destroy(WSPMethodMachine *msm) {
	if (msm == NULL)
		return;

	debug("wap.wsp", 0, "Destroying WSPMethodMachine %ld", msm->id);
	list_delete_equal(method_machines, msm),

	#define INTEGER(name)
	#define ADDRTUPLE(name) wap_addr_tuple_destroy(msm->name);
	#define MACHINE(fields) fields
	#include "wsp-method-machine.h"

	gw_free(msm);
}


static void unpack_caps(Octstr *caps, WSPMachine *m)
{
    int off, flags, next_off;
    unsigned long length, uiv, mor;
    int caps_id;

    debug("wap.wsp", 0, "capabilities dump starts.");
    octstr_dump(caps, 1);
    debug("wap.wsp", 0, "capabilities dump done.");
    
    next_off = off = 0;
    while (next_off < octstr_len(caps)) {
	off = next_off;
	
	unpack_uintvar(&length, caps, &off);
	next_off = off + length;

	/* XXX
	 * capablity identifier is defined as 'multiple octets'
	 * and encoded as Field-Name, but current supported
	 * capabilities can be identified via one number
	 */

	caps_id = octstr_get_char(caps, off);
	off++;

	if (caps_id & 0x80) {
	    caps_id &= 0x7F;
	} else {
	    warning(0, "Ignoring unknown token-text capability");
	    continue;
	}

	switch(caps_id) {
	    
	case WSP_CAPS_CLIENT_SDU_SIZE:
	    if (unpack_uintvar(&uiv, caps, &off) == -1)
		warning(0, "Problems getting client SDU size capability");
	    else {
		if (WSP_MAX_CLIENT_SDU && uiv > WSP_MAX_CLIENT_SDU) {
		    debug("wap.wsp", 0, "Client tried client SDU size %lu larger "
			  "than our max %d", uiv, WSP_MAX_CLIENT_SDU);
		} else if (!(m->set_caps & WSP_CSDU_SET)) {
		    debug("wap.wsp", 0, "Client SDU size negotiated to %lu", uiv);
		    /* Motorola Timeport / Phone.com hack */
		    if (uiv == 3) {
		    	uiv = 1350;
		        debug("wap.wsp", 0, "Client SDU size forced to %lu", uiv);
		    }
		    m->client_SDU_size = uiv;
		    m->set_caps |= WSP_CSDU_SET;
		}
	    }
	    break;
	case WSP_CAPS_SERVER_SDU_SIZE:
	    if (unpack_uintvar(&uiv, caps, &off) == -1)
		warning(0, "Problems getting server SDU size capability");
	    else {
		if (WSP_MAX_SERVER_SDU && uiv > WSP_MAX_SERVER_SDU) {
		    debug("wap.wsp", 0, "Client tried server SDU size %lu larger "
			  "than our max %d", uiv, WSP_MAX_SERVER_SDU);
		} else if (!(m->set_caps & WSP_SSDU_SET)) {
		    debug("wap.wsp", 0, "Server SDU size negotiated to %lu", uiv);
		    m->server_SDU_size = uiv;
		    m->set_caps |= WSP_SSDU_SET;
		}
	    }
	    break;
	case WSP_CAPS_PROTOCOL_OPTIONS:
	    /* XXX should be taken as octstr or something - and
		  * be sure, that there is that information */

	    flags = (octstr_get_char(caps,off));
	    off++;
	    if (!(m->set_caps & WSP_PO_SET)) {

		/* we do not support anything yet, so answer so */

		debug("wap.wsp", 0, "Client protocol option flags 0x%02X, not supported.", flags);
		     
		m->protocol_options = WSP_MAX_PROTOCOL_OPTIONS;
		m->set_caps |= WSP_PO_SET;
	    }
	    break;
	case WSP_CAPS_METHOD_MOR:
	    if (unpack_uint8(&mor, caps, &off) == -1)
		warning(0, "Problems getting MOR methods capability");
	    else {
		if (mor > WSP_MAX_METHOD_MOR) {
		    debug("wap.wsp", 0, "Client tried method MOR %lu larger "
			  "than our max %d", mor, WSP_MAX_METHOD_MOR);
		} else if (!(m->set_caps & WSP_MMOR_SET)) {
		    debug("wap.wsp", 0, "Method MOR negotiated to %lu", mor);
		    m->MOR_method = mor;
		    m->set_caps |= WSP_MMOR_SET;
		}
	    }
	    break;
	case WSP_CAPS_PUSH_MOR:
	    if (unpack_uint8(&mor, caps, &off) == -1)
		warning(0, "Problems getting MOR push capability");
	    else {
		if (mor > WSP_MAX_PUSH_MOR) {
		    debug("wap.wsp", 0, "Client tried push MOR %lu larger "
			  "than our max %d", mor, WSP_MAX_PUSH_MOR);
		} else if (!(m->set_caps & WSP_PMOR_SET)) {
		    debug("wap.wsp", 0, "Push MOR negotiated to %lu", mor);
		    m->MOR_push = mor;
		    m->set_caps |= WSP_PMOR_SET;
		}
	    }
	    break;
	case WSP_CAPS_EXTENDED_METHODS:
	    debug("wap.wsp", 0, "Extended methods capability ignored");
	    off += length - 1;
	    break;
	case WSP_CAPS_HEADER_CODE_PAGES:
	    debug("wap.wsp", 0, "Header code pages capability ignored");
	    off += length - 1;
	    break;
	case WSP_CAPS_ALIASES:
	    debug("wap.wsp", 0, "Aliases capability ignored");
	    off += length - 1;
	    break;
	default:
	    /* unassigned */
	    debug("wap.wsp", 0, "Unknown capability '0x%02X' ignored", caps_id);
	    off += length - 1;
	    break;
	}

	if (off != next_off) {
	    warning(0, "Problems extracting capability parameters, offset is %d, but should be %d",
		    off, next_off);
	}
    }
}


static int unpack_uint8(unsigned long *u, Octstr *os, int *off) {
	if (*off >= octstr_len(os)) {
		error(0, "WSP: Trying to unpack uint8 past PDU");
		return -1;
	}
	*u = octstr_get_char(os, *off);
	++(*off);
	return 0;
}


static int unpack_uintvar(unsigned long *u, Octstr *os, int *off) {
	unsigned long o;
	
	*u = 0;
	do {
		if (unpack_uint8(&o, os, off) == -1) {
			error(0, "WSP: unpack_uint failed in unpack_uintvar");
			return -1;
		}
		*u = ((*u) << 7) | (o & 0x7F);
	} while ((o & 0x80) != 0);

	return 0;
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


/* XXX this function is not thread safe. --liw 
 *     it is nowadays? --rpr */
/* Yes. --liw */
static long wsp_next_session_id(void) {
	return counter_increase(session_id_counter);
}


static void append_uint8(Octstr *pdu, long n) {
	unsigned char c;
	
	c = (unsigned char) n;
	octstr_insert_data(pdu, octstr_len(pdu), &c, 1);
}


static void append_octstr(Octstr *pdu, Octstr *os) {
	octstr_insert(pdu, os, octstr_len(pdu));
}


static Octstr *make_connectreply_pdu(WSPMachine *m, long session_id) {
	WSP_PDU *pdu;
	Octstr *os, *caps, *tmp;
	
	pdu = wsp_pdu_create(ConnectReply);

	pdu->u.ConnectReply.sessionid = session_id;


	if (m->set_caps) {
		caps = octstr_create_empty();
		tmp = octstr_create_empty();
		
		/* XXX put negotiated capabilities into octstr */
		
		if (m->set_caps & WSP_CSDU_SET) {
			octstr_truncate(tmp, 0);
			append_uint8(tmp, WSP_CAPS_SERVER_SDU_SIZE);
			octstr_append_uintvar(tmp, m->client_SDU_size);
		
			octstr_append_uintvar(caps, octstr_len(tmp));
			append_octstr(caps, tmp);
		}
		if (m->set_caps & WSP_SSDU_SET) {
			octstr_truncate(tmp, 0);
			append_uint8(tmp, WSP_CAPS_SERVER_SDU_SIZE);
			octstr_append_uintvar(tmp, m->server_SDU_size);
		
			octstr_append_uintvar(caps, octstr_len(tmp));
			append_octstr(caps, tmp);
		}
		
		if (m->set_caps & WSP_MMOR_SET) {
			octstr_append_uintvar(caps, 2);
			append_uint8(caps, WSP_CAPS_METHOD_MOR);
			append_uint8(caps, m->MOR_method);
		}
		if (m->set_caps & WSP_PMOR_SET) {
			octstr_append_uintvar(caps, 2);
			append_uint8(caps, WSP_CAPS_PUSH_MOR);
			append_uint8(caps, m->MOR_push);
		}
		/* rest are not supported, yet */
		
		pdu->u.ConnectReply.capabilities = caps;
		octstr_destroy(tmp);
	} else
		pdu->u.ConnectReply.capabilities = NULL;

	pdu->u.ConnectReply.headers = NULL;
	
	os = wsp_pdu_pack(pdu);
	wsp_pdu_destroy(pdu);

	return os;
}


/* XXX this function is not thread safe. --liw */
static long new_server_transaction_id(void) {
	static long next_id = 1;
	return next_id++;
}


static int transaction_belongs_to_session(void *wsp_ptr, void *tuple_ptr) {
	WSPMachine *wsp;
	WAPAddrTuple *tuple;
	
	wsp = wsp_ptr;
	tuple = tuple_ptr;

	return wap_addr_tuple_same(wsp->addr_tuple, tuple);
}



static int same_client(void *a, void *b) {
	WSPMachine *sm1, *sm2;
	
	sm1 = a;
	sm2 = b;
	return wap_addr_tuple_same(sm1->addr_tuple, sm2->addr_tuple);
}


static int same_method_machine(void *a, void *b) {
	WSPMethodMachine *msm;
	struct msm_pattern *pat;
	
	msm = a;
	pat = b;

	if (pat->msmid == -1) {
		return wap_addr_tuple_same(msm->addr_tuple, pat->addr_tuple) &&
			msm->tid == pat->tid;
	}

	return msm->id == pat->msmid;
}
