/*
 * wsp.c - Implement WSP
 *
 * Lars Wirzenius <liw@wapit.com>
 * Capabilities/headers by Kalle Marjola <rpr@wapit.com>
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
	#include "wsp_state-decl.h"
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


static void handle_event(WSPMachine *machine, WAPEvent *event, WSP_PDU *pdu);
static WSPMachine *machine_create(void);
static void machine_mark_unused(WSPMachine *p);
static void machine_destroy(WSPMachine *p);
#if 0
static void machine_dump(WSPMachine *machine);
#endif

static void unpack_caps(Octstr *caps, WSPMachine *m);

static int unpack_uint8(unsigned long *u, Octstr *os, int *off);
static int unpack_uintvar(unsigned long *u, Octstr *os, int *off);

static char *wsp_state_to_string(WSPState state);
static long wsp_next_session_id(void);

static void append_uint8(Octstr *pdu, long n);
static void append_uintvar(Octstr *pdu, long n);
static void append_octstr(Octstr *pdu, Octstr *os);

static Octstr *make_connectreply_pdu(WSPMachine *m, long session_id);

static Octstr *encode_http_headers(long content_type);
static long convert_http_status_to_wsp_status(long http_status);

static long new_server_transaction_id(void);
static int transaction_belongs_to_session(void *session, void *wtp);
static int same_client(void *sm1, void *sm2);

static void main_thread(void *);
static WSPMachine *find_machine(WAPEvent *event, WSP_PDU *pdu);



void wsp_init(void) {
	queue = list_create();
	list_add_producer(queue);
	session_machines = list_create();
	session_id_counter = counter_create();
	run_status = running;
	gwthread_create(main_thread, NULL);
}



void wsp_shutdown(void) {
	WAPEvent *e;
	
	gw_assert(run_status == running);
	run_status = terminating;
	list_remove_producer(queue);
	gwthread_join_all(main_thread);

	while ((e = list_extract_first(queue)) != e)
		wap_event_destroy(e);
	list_destroy(queue);
	while (list_len(session_machines) > 0)
		machine_destroy(list_get(session_machines, 0));
	list_destroy(session_machines);
	counter_destroy(session_id_counter);
}



void wsp_dispatch_event(WAPEvent *event) {
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
			pdu = wsp_pdu_unpack(e->TR_Invoke_Ind.user_data);
			if (pdu == NULL) {
				warning(0, "WSP: Broken PDU ignored.");
				return;
			}
			break;
	
		default:
			pdu = NULL;
			break;
		}
	
		sm = find_machine(e, pdu);
		debug("wap.wsp", 0, "WSP: Got event %p, for %p",
			(void *) e, (void *) sm);
		if (sm != NULL)
			handle_event(sm, e, pdu);
		
		wsp_pdu_destroy(pdu);
	}
}



static WSPMachine *find_machine(WAPEvent *event, WSP_PDU *pdu) {
	WSPMachine *sm;
	WTPMachine *wtp_sm;
	
	switch (event->type) {
	case TR_Invoke_Ind:
		wtp_sm = event->TR_Invoke_Ind.machine;
		break;

	case TR_Invoke_Cnf:
		wtp_sm = event->TR_Invoke_Cnf.machine;
		break;

	case TR_Result_Cnf:
		wtp_sm = event->TR_Result_Cnf.machine;
		break;

	case TR_Abort_Ind:
		wtp_sm = event->TR_Abort_Ind.machine;
		break;

	case S_Connect_Res:
		wtp_sm = event->S_Connect_Res.machine;
		break;

	case Release:
		wtp_sm = event->Release.machine;
		break;

	case S_MethodInvoke_Ind:
		wtp_sm = event->S_MethodInvoke_Ind.machine;
		break;

	case S_MethodInvoke_Res:
		wtp_sm = event->S_MethodInvoke_Res.machine;
		break;

	case S_MethodResult_Req:
		wtp_sm = event->S_MethodResult_Req.machine;
		break;

	default:
		error(0, "Don't know state machine for WAPEvent in WSP.");
		debug("wap.wsp", 0, "WAPEvent which we couldn't find SM for:");
		wap_event_dump(event);
		return NULL;
	}

	/* XXX this should probably be moved to a condition function --liw */
	if (event->type == TR_Invoke_Ind &&
	    event->TR_Invoke_Ind.tcl == 2 &&
	    pdu->type == Connect) {
		/* Client wants to start new session. Igore existing
		   machines. */
		sm = NULL;
	} else {
		sm = list_search(session_machines, wtp_sm,
				 transaction_belongs_to_session);
	}

	if (sm == NULL) {
		sm = machine_create();
#if 0
		debug("wap.wsp", 0, "WSP: wtp_sm:");
		wtp_machine_dump(wtp_sm);
#endif
		sm->client_address = octstr_duplicate(wtp_sm->source_address);
		sm->client_port = wtp_sm->source_port;
		sm->server_address = 
			octstr_duplicate(wtp_sm->destination_address);
		sm->server_port = wtp_sm->destination_port;
	}

	return sm;
}


static WSPMachine *machine_create(void) {
	WSPMachine *p;
	
	p = gw_malloc(sizeof(WSPMachine));
	debug("wap.wsp", 0, "WSP: Created WSPMachine %p", (void *) p);
	
	#define INTEGER(name) p->name = 0;
	#define OCTSTR(name) p->name = NULL;
	#define METHOD_POINTER(name) p->name = NULL;
	#define EVENT_POINTER(name) p->name = NULL;
	#define SESSION_POINTER(name) p->name = NULL;
	#define HTTPHEADER(name) p->name = NULL;
	#define LIST(name) p->name = list_create();
	#define SESSION_MACHINE(fields) fields
	#define METHOD_MACHINE(fields)
	#include "wsp_machine-decl.h"
	
	p->state = NULL_STATE;

	/* set capabilities to default values (defined in 1.1) */

	p->client_SDU_size = 1400;
	p->server_SDU_size = 1400;
        /* p->protocol_options = 0x00;	 */
	p->MOR_method = 1;
	p->MOR_push = 1;
	
	list_append(session_machines, p);

	return p;
}


static void machine_mark_unused(WSPMachine *p) {
	p->unused = 1;
}


static void machine_destroy(WSPMachine *p) {
	debug("wap.wsp", 0, "Destroying WSPMachine %p", (void *) p);
	list_delete_equal(session_machines, p);
	#define MUTEX(name) mutex_destroy(p->name);
	#define INTEGER(name) p->name = 0;
	#define OCTSTR(name) octstr_destroy(p->name);
	#define METHOD_POINTER(name) p->name = NULL;
	#define EVENT_POINTER(name) p->name = NULL;
	#define SESSION_POINTER(name) p->name = NULL;
	#define HTTPHEADER(name) http2_destroy_headers(p->name);
	#define LIST(name) list_destroy(p->name);
	#define SESSION_MACHINE(fields) fields
	#define METHOD_MACHINE(fields)
	#include "wsp_machine-decl.h"
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


static void handle_event(WSPMachine *sm, WAPEvent *current_event, WSP_PDU *pdu)
{
	debug("wap.wsp", 0, "WSP: machine %p, state %s, event %s",
		(void *) sm,
		wsp_state_to_string(sm->state), 
		wap_event_name(current_event->type));

	#define STATE_NAME(name)
	#define ROW(state_name, event, condition, action, next_state) \
		{ \
			struct event *e; \
			e = &current_event->event; \
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
	#include "wsp_state-decl.h"
	
	if (current_event->type == TR_Invoke_Ind) {
		WAPEvent *abort;
		
		error(0, "WSP: Can't handle TR-Invoke.ind, aborting transaction.");
		abort = wap_event_create(TR_Abort_Req);
		abort->TR_Abort_Req.tid = 
			current_event->TR_Invoke_Ind.machine->tid;
		abort->TR_Abort_Req.abort_type = 0x01; /* USER */
		abort->TR_Abort_Req.abort_reason = 0xE0; /* PROTOERR */
		abort->TR_Abort_Req.mid = 
			current_event->TR_Invoke_Ind.machine->mid;

		wtp_dispatch_event(abort);
		machine_mark_unused(sm);
	} else {
		error(0, "WSP: Can't handle event.");
		debug("wap.wsp", 0, "WSP: The unhandled event:");
		wap_event_dump(current_event);
	}

end:
	wap_event_destroy(current_event);

	if (sm->unused)
		machine_destroy(sm);
}




static void unpack_caps(Octstr *caps, WSPMachine *m)
{
    int off, flags;
    unsigned long length, uiv, mor, caps_id;
    
    off = 0;
    while (off < octstr_len(caps)) {
	unpack_uintvar(&length, caps, &off);

	/* XXX
	 * capablity identifier is defined as 'multiple octets'
	 * and encoded as Field-Name, but current supported
	 * capabilities can be identified via one number
	 */

	unpack_uintvar(&caps_id, caps, &off);

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
	    if (!(m->set_caps & WSP_PO_SET)) {

		/* we do not support anything yet, so answer so */

		debug("wap.wsp", 0, "Client protocol option flags %0xd, not supported.", flags);
		     
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
	    break;
	case WSP_CAPS_HEADER_CODE_PAGES:
	    debug("wap.wsp", 0, "Header code pages capability ignored");
	    break;
	case WSP_CAPS_ALIASES:
	    debug("wap.wsp", 0, "Aliases capability ignored");
	    break;
	default:
	    /* unassigned */
	    debug("wap.wsp", 0, "Unknown capability '%d' ignored",
		  octstr_get_char(caps,off-1));
	    break;
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
	#include "wsp_state-decl.h"
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


static void append_uintvar(Octstr *pdu, long n) {
	long bytes[5];
	unsigned long u;
	int i;
	
	u = n;
	for (i = 4; i >= 0; --i) {
		bytes[i] = u & 0x7F;
		u >>= 7;
	}
	for (i = 0; i < 4 && bytes[i] == 0; ++i)
		continue;
	for (; i < 4; ++i)
		append_uint8(pdu, 0x80 | bytes[i]);
	append_uint8(pdu, bytes[4]);
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
			append_uintvar(tmp, m->client_SDU_size);
		
			append_uintvar(caps, octstr_len(tmp));
			append_octstr(caps, tmp);
		}
		if (m->set_caps & WSP_SSDU_SET) {
			octstr_truncate(tmp, 0);
			append_uint8(tmp, WSP_CAPS_SERVER_SDU_SIZE);
			append_uintvar(tmp, m->server_SDU_size);
		
			append_uintvar(caps, octstr_len(tmp));
			append_octstr(caps, tmp);
		}
		
		if (m->set_caps & WSP_MMOR_SET) {
			append_uintvar(caps, 2);
			append_uint8(caps, WSP_CAPS_METHOD_MOR);
			append_uint8(caps, m->MOR_method);
		}
		if (m->set_caps & WSP_PMOR_SET) {
			append_uintvar(caps, 2);
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


static Octstr *encode_http_headers(long type) {
	Octstr *os;
	
	gw_assert(type >= 0x00);
	gw_assert(type < 0x80);

	os = octstr_create_empty();
	append_uint8(os, type | 0x80);
	
	return os;
}


static long convert_http_status_to_wsp_status(long http_status) {
	static struct {
		long http_status;
		long wsp_status;
	} tab[] = {
		{ 200, 0x20 },
		{ 413, 0x4D },
		{ 415, 0x4F },
		{ 500, 0x60 },
	};
	int num_items = sizeof(tab) / sizeof(tab[0]);
	int i;
	
	for (i = 0; i < num_items; ++i)
		if (tab[i].http_status == http_status)
			return tab[i].wsp_status;
	error(0, "WSP: Unknown status code used internally. Oops.");
	return 0x60; /* Status 500, or "Internal Server Error" */
}


/* XXX this function is not thread safe. --liw */
static long new_server_transaction_id(void) {
	static long next_id = 1;
	return next_id++;
}


static int transaction_belongs_to_session(void *wsp_ptr, void *wtp_ptr) {
	WSPMachine *wsp;
	WTPMachine *wtp;
	
	wsp = wsp_ptr;
	wtp = wtp_ptr;

	return
	  !wsp->unused &&
	  wtp->in_use &&
	  wtp->source_port == wsp->client_port &&
	  wtp->destination_port == wsp->server_port &&
	  octstr_compare(wtp->source_address, wsp->client_address) == 0 &&
	  octstr_compare(wtp->destination_address, wsp->server_address) == 0;
}



static int same_client(void *a, void *b) {
	WSPMachine *sm1, *sm2;
	
	sm1 = a;
	sm2 = b;
	return !sm1->unused &&
	       !sm2->unused && 
	       sm1->client_port == sm2->client_port &&
	       sm1->server_port == sm2->server_port &&
	       octstr_compare(sm1->client_address, sm2->client_address) == 0 &&
	       octstr_compare(sm1->server_address, sm2->server_address) == 0;
}
