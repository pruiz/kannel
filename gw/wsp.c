/*
 * wsp.c - Implement WSP
 *
 * Lars Wirzenius <liw@wapit.com>
 * Capabilities/headers by Kalle Marjola <rpr@wapit.com>
 */


#include <assert.h>

#include "gwlib.h"
#include "wsp.h"
#include "wsp_headers.h"
#include "wml.h"
#include "ws.h"

/* and WAP standard defined values for capabilities */

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
	Put_PDU = 0x61,
};

typedef enum {
	#define STATE_NAME(name) name,
	#define ROW(state, event, condition, action, next_state)
	#include "wsp_state-decl.h"
} WSPState;


static WSPMachine *session_machines = NULL;


static void append_to_event_queue(WSPMachine *machine, WSPEvent *event);
static WSPEvent *remove_from_event_queue(WSPMachine *machine);

static int unpack_connect_pdu(WSPMachine *m, Octstr *user_data);
static int unpack_get_pdu(Octstr **url, Octstr **headers, Octstr *pdu);
static int unpack_post_pdu(Octstr **url, Octstr **headers, Octstr *pdu);

static int unpack_uint8(unsigned long *u, Octstr *os, int *off);
static int unpack_uintvar(unsigned long *u, Octstr *os, int *off);
static int unpack_octstr(Octstr **ret, int len, Octstr *os, int *off);

static char *wsp_state_to_string(WSPState state);
static long wsp_next_session_id(void);

static void append_uint8(Octstr *pdu, long n);
static void append_uintvar(Octstr *pdu, long n);
static void append_octstr(Octstr *pdu, Octstr *os);

static Octstr *make_connectionmode_pdu(long type);
static Octstr *make_connectreply_pdu(WSPMachine *m, long session_id);
static Octstr *make_reply_pdu(long status, long type, Octstr *body);

static long convert_http_status_to_wsp_status(long http_status);

static long new_server_transaction_id(void);
static int transaction_belongs_to_session(WTPMachine *wtp, WSPMachine *session);

static void *wsp_http_thread(void *arg);


WSPEvent *wsp_event_create(WSPEventType type) {
	WSPEvent *event;
	
	event = gw_malloc(sizeof(WSPEvent));
	event->type = type;
	event->next = NULL;

	#define INTEGER(name) p->name = 0
	#define OCTSTR(name) p->name = NULL
	#define WTP_MACHINE(name) p->name = NULL
	#define SESSION_MACHINE(name) p->name = NULL
	#define WSP_EVENT(name, fields) \
		{ struct name *p = &event->name; fields }
	#include "wsp_events-decl.h"

	return event;
}


void wsp_event_destroy(WSPEvent *event) {
	if (event != NULL) {
		#define INTEGER(name) p->name = 0
		#define OCTSTR(name) octstr_destroy(p->name)
		#define WTP_MACHINE(name) p->name = NULL
		#define SESSION_MACHINE(name) p->name = NULL
		#define WSP_EVENT(name, fields) \
			{ struct name *p = &event->name; fields }
		#include "wsp_events-decl.h"

		gw_free(event);
	}
}

char *wsp_event_name(WSPEventType type) {
	switch (type) {
	#define WSP_EVENT(name, fields) case name: return #name;
	#include "wsp_events-decl.h"
	default:
		return "unknown WSPEvent type";
	}
}


void wsp_event_dump(WSPEvent *event) {
	debug(0, "Dump of WSPEvent %p follows:", (void *) event);
	debug(0, "  type: %s (%d)", wsp_event_name(event->type), event->type);
	#define INTEGER(name) debug(0, "  %s.%s: %d", t, #name, p->name)
	#define OCTSTR(name) debug(0, "  %s.%s:", t, #name); octstr_dump(p->name)
	#define WTP_MACHINE(name) \
		debug(0, "  %s.%s at %p", t, #name, (void *) p->name)
	#define SESSION_MACHINE(name) \
		debug(0, "  %s.%s at %p", t, #name, (void *) p->name)
	#define WSP_EVENT(tt, fields) \
		if (tt == event->type) \
			{ char *t = #tt; struct tt *p = &event->tt; fields }
	#include "wsp_events-decl.h"
	debug(0, "Dump of WSPEvent %p ends.", (void *) event);
}


void wsp_dispatch_event(WTPMachine *wtp_sm, WSPEvent *event) {
	/* XXX this now always creates a new machine for each event, boo */
	
	WSPMachine *sm;
	
	if (event->type == TRInvokeIndication &&
	    event->TRInvokeIndication.tcl == 2 &&
	    wsp_deduce_pdu_type(event->TRInvokeIndication.user_data, 0) == Connect_PDU) {
		/* Client wants to start new session. Igore existing
		   machines. */
		sm = NULL;
	} else {
		for (sm = session_machines; sm != NULL; sm = sm->next)
			if (transaction_belongs_to_session(wtp_sm, sm))
				break;
	}

	if (sm == NULL) {
		sm = wsp_machine_create();
		sm->client_address = octstr_duplicate(wtp_sm->source_address);
		sm->client_port = wtp_sm->source_port;
		sm->server_address = 
			octstr_duplicate(wtp_sm->destination_address);
		sm->server_port = wtp_sm->destination_port;
	}

	wsp_handle_event(sm, event);
}


WSPMachine *wsp_machine_create(void) {
	WSPMachine *p;
	
	p = gw_malloc(sizeof(WSPMachine));
	
	#define MUTEX(name) p->name = mutex_create()
	#define INTEGER(name) p->name = 0
	#define OCTSTR(name) p->name = NULL
	#define METHOD_POINTER(name) p->name = NULL
	#define EVENT_POINTER(name) p->name = NULL
	#define SESSION_POINTER(name) p->name = NULL
	#define SESSION_MACHINE(fields) fields
	#define METHOD_MACHINE(fields)
	#include "wsp_machine-decl.h"
	
	p->state = NULL_STATE;

	/* set capabilities to default values */

	p->client_SDU_size = 1400;
	p->server_SDU_size = 1400;
        /* p->protocol_options = 0x00;	 */
	p->MOR_method = 1;
	p->MOR_push = 1;
	
	/* XXX this should be locked */
	p->next = session_machines;
	session_machines = p;
	
	return p;
}


void wsp_machine_destroy(WSPMachine *machine) {
	debug(0, "Destroying WSPMachine not yet implemented.");
}


void wsp_machine_dump(WSPMachine *machine) {
	debug(0, "Dumping WSPMachine not yet implemented.");
}


void wsp_handle_event(WSPMachine *sm, WSPEvent *current_event) {
	/* 
	 * If we're already handling events for this machine, add the
	 * event to the queue.
	 */
	if (mutex_try_lock(sm->mutex) == -1) {
		append_to_event_queue(sm, current_event);
		return;
	}

	do {
		debug(0, "WSP: state is %s, event is %s",
			wsp_state_to_string(sm->state), 
			wsp_event_name(current_event->type));
#if 0
		debug(0, "WSP: event is:");
		wsp_event_dump(current_event);
#endif

		#define STATE_NAME(name)
		#define ROW(state_name, event, condition, action, next_state) \
			{ \
				struct event *e; \
				e = &current_event->event; \
				if (sm->state == state_name && \
				   current_event->type == event && \
				   (condition)) { \
					debug(0, "WSP: Doing action for %s", \
						#state_name); \
					action \
					debug(0, "WSP: Setting state to %s", \
						#next_state); \
					sm->state = next_state; \
					goto end; \
				} \
			}
		#include "wsp_state-decl.h"
		
		if (current_event->type == TRInvokeIndication) {
			WTPEvent *abort;
			
			error(0, "WSP: Can't handle TR-Invoke.ind, aborting transaction.");
			abort = wtp_event_create(TRAbort);
			abort->TRAbort.tid = 
				current_event->TRInvokeIndication.machine->tid;
			abort->TRAbort.abort_type = 0x01; /* USER */
			abort->TRAbort.abort_reason = 0x01; /* PROTOERR */

			wtp_handle_event(current_event->TRInvokeIndication.machine,
					 abort);
			sm->client_port = -1;
		} else {
			error(0, "WSP: Can't handle event.");
			debug(0, "WSP: The unhandled event:");
			wsp_event_dump(current_event);
		}

	end:
		current_event = remove_from_event_queue(sm);
	} while (current_event != NULL);
	
	mutex_unlock(sm->mutex);
}




int wsp_deduce_pdu_type(Octstr *pdu, int connectionless) {
	int off;
	unsigned long o;

	if (connectionless)
		off = 1;
	else
		off = 0;
	if (unpack_uint8(&o, pdu, &off) == -1)
		o = Bad_PDU;
	return o;
}


/***********************************************************************
 * Local functions
 */



static int unpack_caps(Octstr *caps, WSPMachine *m)
{
    int off, flags;
    unsigned long length, uiv, mor;
    
    off = 0;
    while (off < octstr_len(caps)) {
	unpack_uintvar(&length, caps, &off);

	/* XXX
	 * capablity identifier is defined as 'multiple octets'
	 * and encoded as Field-Name, but current supported
	 * capabilities can be identified via one number
	 */

	off++;
	switch(octstr_get_char(caps,off-1)) {
	case WSP_CAPS_CLIENT_SDU_SIZE:
	    if (unpack_uintvar(&uiv, caps, &off) == -1)
		warning(0, "Problems getting client SDU size capability");
	    else {
		if (WSP_MAX_CLIENT_SDU && uiv < WSP_MAX_CLIENT_SDU) {
		    debug(0, "Client tried client SDU size %lu larger "
			  "than our max %d", uiv, WSP_MAX_CLIENT_SDU);
		} else if (!(m->set_caps & WSP_CSDU_SET)) {
		    debug(0, "Client SDU size negotiated to %lu", uiv);
		    m->client_SDU_size = uiv;
		    m->set_caps |= WSP_CSDU_SET;
		}
	    }
	    break;
	case WSP_CAPS_SERVER_SDU_SIZE:
	    if (unpack_uintvar(&uiv, caps, &off) == -1)
		warning(0, "Problems getting server SDU size capability");
	    else {
		if (WSP_MAX_SERVER_SDU && uiv < WSP_MAX_SERVER_SDU) {
		    debug(0, "Client tried server SDU size %lu larger "
			  "than our max %d", uiv, WSP_MAX_SERVER_SDU);
		} else if (!(m->set_caps & WSP_SSDU_SET)) {
		    debug(0, "Server SDU size negotiated to %lu", uiv);
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

		debug(0, "Client protocol option flags %0xd, not supported.", flags);
		     
		m->protocol_options = WSP_MAX_PROTOCOL_OPTIONS;
		m->set_caps |= WSP_SSDU_SET;
	    }
	    break;
	case WSP_CAPS_METHOD_MOR:
	    if (unpack_uint8(&mor, caps, &off) == -1)
		warning(0, "Problems getting MOR methods capability");
	    else {
		if (mor < WSP_MAX_METHOD_MOR) {
		    debug(0, "Client tried method MOR %lu larger "
			  "than our max %d", mor, WSP_MAX_METHOD_MOR);
		} else if (!(m->set_caps & WSP_MMOR_SET)) {
		    debug(0, "Method MOR negotiated to %lu", mor);
		    m->MOR_method = mor;
		    m->set_caps |= WSP_MMOR_SET;
		}
	    }
	    break;
	case WSP_CAPS_PUSH_MOR:
	    if (unpack_uint8(&mor, caps, &off) == -1)
		warning(0, "Problems getting MOR push capability");
	    else {
		if (mor < WSP_MAX_PUSH_MOR) {
		    debug(0, "Client tried push MOR %lu larger "
			  "than our max %d", mor, WSP_MAX_PUSH_MOR);
		} else if (!(m->set_caps & WSP_PMOR_SET)) {
		    debug(0, "Push MOR negotiated to %lu", mor);
		    m->MOR_push = mor;
		    m->set_caps |= WSP_PMOR_SET;
		}
	    }
	    break;
	case WSP_CAPS_EXTENDED_METHODS:
	    debug(0, "Extended methods capability ignored");
	    break;
	case WSP_CAPS_HEADER_CODE_PAGES:
	    debug(0, "Header code pages capability ignored");
	    break;
	case WSP_CAPS_ALIASES:
	    debug(0, "Aliases capability ignored");
	    break;
	default:
	    /* unassigned */
	    debug(0, "Unknown capability ignored");
	    break;
	}
    }
    return 0;
}

static void append_to_event_queue(WSPMachine *machine, WSPEvent *event) {
	mutex_lock(machine->queue_lock);
	if (machine->event_queue_head == NULL) {
		machine->event_queue_head = event;
		machine->event_queue_tail = event;
		event->next = NULL;
	} else {
		machine->event_queue_tail->next = event;
		machine->event_queue_tail = event;
		event->next = NULL;
	}
	mutex_unlock(machine->queue_lock);
}

static WSPEvent *remove_from_event_queue(WSPMachine *machine) {
	WSPEvent *event;
	
	mutex_lock(machine->queue_lock);
	if (machine->event_queue_head == NULL)
		event = NULL;
	else {
		event = machine->event_queue_head;
		machine->event_queue_head = event->next;
		event->next = NULL;
	}
	mutex_unlock(machine->queue_lock);
	return event;
}


static int unpack_connect_pdu(WSPMachine *m, Octstr *user_data) {
	int off;
	unsigned long version, caps_len, headers_len;
	Octstr *caps, *headers;

	off = 1;	/* ignore PDU type */
	if (unpack_uint8(&version, user_data, &off) == -1 ||
	    unpack_uintvar(&caps_len, user_data, &off) == -1 ||
	    unpack_uintvar(&headers_len, user_data, &off) == -1 ||
	    unpack_octstr(&caps, caps_len, user_data, &off) == -1 ||
	    unpack_octstr(&headers, headers_len, user_data, &off) == -1)
		return -1;

	debug(0, "Unpacked Connect PDU: version=%lu, caps_len=%lu, hdrs_len=%lu",
	      version, caps_len, headers_len);
	if (caps_len > 0) {
	    debug(0, "Unpacked caps:");
	    octstr_dump(caps);

	    unpack_caps(caps, m);
	}
	if (headers_len > 0) {
	    HTTPHeader *hdrs;
	    
	    octstr_dump(headers);
	    
	    hdrs = unpack_headers(headers);

	    /* pack them for more compact form */
	    header_pack(hdrs);
	    debug(0, "(packed) Unpacked headers:");
	    header_dump(hdrs);

	    /* we should put them into WSPMachine and use them in future */
	    header_destroy(hdrs);
	}
	return 0;
}


static int unpack_get_pdu(Octstr **url, Octstr **headers, Octstr *pdu) {
	unsigned long url_len;
	int off;

	off = 1; /* Offset 0 has type octet. */
	if (unpack_uintvar(&url_len, pdu, &off) == -1 ||
	    unpack_octstr(url, url_len, pdu, &off) == -1)
		return -1;
	if (off < octstr_len(pdu))
		error(0, "unpack_get_pdu: Get PDU has headers, ignored them");
	*headers = NULL;
	debug(0, "WSP: Get PDU had URL <%s>", octstr_get_cstr(*url));
	return 0;
}

static int unpack_post_pdu(Octstr **url, Octstr **headers, Octstr *pdu) {
	unsigned long url_len;
	unsigned long param_len;
	Octstr 		*param;
	Octstr 		*head;
	int off;

	off = 1; /* Offset 0 has type octet. */
	/* 
		0x60 : Post
		u8	: URL len
		u8 	: Header Len
		URL	:
		Vars	:

	*/
	if (unpack_uintvar(&url_len, pdu, &off) == -1)
	{
		return -1;
	}
	 if( unpack_uintvar(&param_len,pdu,&off) == -1)
	{
		return -1;
	}
/*
	debug(0, "WSP: Post PDU had  URL len <%d>", url_len);
	debug(0, "WSP: Post PDU had  param len <%d>", param_len);
*/
	if(  unpack_octstr(url, url_len, pdu, &off) == -1)
	{
		return -1;
	}
	debug(0, "WSP: Post PDU had URL <%s>", octstr_get_cstr(*url));

	if(unpack_octstr(&head,param_len,pdu,&off)==-1)
	{
		return -1;
	}
	debug(0, "WSP: Got headers. <%d> Total len <%d> offset",octstr_len(pdu),off);

	if(unpack_octstr(&param,octstr_len(pdu)-off,pdu,&off)==-1)
	{
		return -1;
	}

/*
	if (off < octstr_len(pdu))
		error(0, "unpack_post_pdu: Post PDU has headers, ignored them");
*/
		
	*headers = NULL;

	debug(0, "WSP: Post PDU had dats <%s>", octstr_get_cstr(param));

	octstr_destroy(head);
	/* Now we concatenante the two thingies */
	head=octstr_create("?");
	octstr_insert(*url,head,url_len);	
	octstr_insert(*url,param,octstr_len(*url));	
	octstr_destroy(param);
	octstr_destroy(head);
/*
	octstr_set_char	(*url,url_len,'?');
*/	
	debug(0, "WSP: Final URL is  <%s>", octstr_get_cstr(*url));
	debug(0, "WSP: URL unpacked");
	return 0;
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
	} while ((o & 0x80) == 1);

	return 0;
}


static int unpack_octstr(Octstr **ret, int len, Octstr *os, int *off) {
	if (*off + len > octstr_len(os)) {
		error(0, "WSP: Trying to unpack string past PDU");
		return -1;
	}
	*ret = octstr_copy(os, *off, len);
	*off += len;
	return 0;
}


static char *wsp_state_to_string(WSPState state) {
	switch (state) {
	#define STATE_NAME(name) case name: return #name;
	#define ROW(state, event, cond, stmt, next_state)
	#include "wsp_state-decl.h"
	}
	return "unknown wsp state";
}


static long wsp_next_session_id(void) {
	static long next_id = 1;
	return next_id++;
}


static Octstr *make_connectionmode_pdu(long type) {
	Octstr *pdu;
	
	pdu = octstr_create_empty();
	append_uint8(pdu, type);
	return pdu;
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
	Octstr *pdu, *caps = NULL, *hdrs = NULL, *tmp;
	
	pdu = make_connectionmode_pdu(ConnectReply_PDU);
	append_uintvar(pdu, session_id);
	/* set CapabilitiesLen */
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
	    
	    append_uintvar(pdu, octstr_len(caps));
	    octstr_destroy(tmp);
	} else
	    append_uintvar(pdu, 0);

	/* set HeadersLen */
	append_uintvar(pdu, 0);

	if (caps != NULL) {
	    append_octstr(pdu, caps);
	    octstr_destroy(caps);
	}
	if (hdrs != NULL) {
	    append_octstr(pdu, hdrs);
	    octstr_destroy(hdrs);
	}
	return pdu;
}


static Octstr *make_reply_pdu(long status, long type, Octstr *body) {
	Octstr *pdu;
	
	/* XXX this is a hardcoded kludge */
	pdu = make_connectionmode_pdu(Reply_PDU);
	append_uint8(pdu, convert_http_status_to_wsp_status(status));
	append_uintvar(pdu, 1);
	assert(type >= 0x00);
	assert(type < 0x80);
	append_uint8(pdu, type | 0x80);
	if (body != NULL)
		append_octstr(pdu, body);
	return pdu;
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
	int i;
	
	for (i = 0; i < sizeof(tab) / sizeof(tab[0]); ++i)
		if (tab[i].http_status == http_status)
			return tab[i].wsp_status;
	error(0, "WSP: Unknown status code used internally. Oops.");
	return 0x60; /* Status 500, or "Internal Server Error" */
}


static long new_server_transaction_id(void) {
	static long next_id = 1;
	
	return next_id++;
}


static int transaction_belongs_to_session(WTPMachine *wtp, WSPMachine *session)
{
	return
	  octstr_compare(wtp->source_address, session->client_address) == 0 &&
	  wtp->source_port == session->client_port &&
	  octstr_compare(wtp->destination_address, session->server_address) == 0 && 
	  wtp->destination_port == session->server_port;
}


/* Shut up WMLScript compiler status/trace messages. */
static void  dev_null(const char *data, size_t len, void *context) {
  /* nothing */
}


static int encode_content_type(const char *type) {
	static struct {
		char *type;
		int shortint;
	} tab[] = {
		{ "text/plain", 0x03 },
		{ "text/vnd.wap.wml", 0x08 },
		{ "text/vnd.wap.wmlscript", 0x09 },
		{ "application/vnd.wap.wmlc", 0x14 },
		{ "application/vnd.wap.wmlscriptc", 0x15 },
		{ "image/vnd.wap.wbmp", 0x21 },
	};
	int i;
	
	for (i = 0; i < sizeof(tab) / sizeof(tab[0]); ++i)
		if (strcmp(type, tab[i].type) == 0)
			return tab[i].shortint;
	error(0, "WSP: Unknown content type <%s>, assuming text/plain.", type);
	return 0x03;
}


static void *wsp_http_thread(void *arg) {
	char *type, *data;
	size_t size;
	Octstr *body;
	WSPEvent *e;
	int status;
#if 0
	struct wmlc *wmlc_data;
#endif
	char *url;
	WSPEvent *event;
	unsigned long body_size, client_SDU_size;

	debug(0, "WSP: wsp_http_thread starts");

	event = arg;
	debug(0, "WSP: Sending S-MethodInvoke.Res to WSP");
	wsp_dispatch_event(event->SMethodInvokeResult.machine, event);

	url = octstr_get_cstr(event->SMethodInvokeResult.url);
	debug(0, "WSP: url is <%s>", url);

	body = NULL;

	if (http_get(url, &type, &data, &size) == -1) {
		error(0, "WSP: http_get failed, oops.");
		status = 500; /* Internal server error; XXX should be 503 */
		type = "text/plain";
	} else {
		info(0, "WSP: Fetched <%s>", url);
		debug(0, "WSP: Type is <%s> (0x%02x)", type,
			encode_content_type(type));
		status = 200; /* OK */
		
		if (strcmp(type, "text/vnd.wap.wml") == 0) {
			debug(0, "WSP: Compiling WML");
			type = "application/vnd.wap.wmlc";
			data = gw_realloc(data, size + 1);
			data[size] = '\0';

#if 0
/* It seems wmo2wmlc can't handle multiple calls per process lifetime. Ooops. */
			debug(0, "WSP: calling wml2wmlc");
			wmlc_data = wml2wmlc(data);
			debug(0, "WSP: wml2wmlc done, %p", (void *) wmlc_data);
			if (wmlc_data == NULL) {
				error(0, "Out of memory or WML syntax error");
				status = 415; /* XXX wrong code! */
			} else {
				body = octstr_create_from_data(wmlc_data->wbxml,
							wmlc_data->wml_length);
				debug(0, "WSP: Compile OK.");
			}
#else
{
	char name[10*1024];
	char cmd[20*1024];
	char *test_wml;
	FILE *f;
	char wmlc[100*1024];
	size_t n;
	
	tmpnam(name);
	f = fopen(name, "w");
	if (f == NULL)
		goto error;
	if (fwrite(data, 1, size, f) == -1)
		goto error;
	fclose(f);
	
	test_wml = getenv("TEST_WML");
	if (test_wml == NULL)
		test_wml = "./test_wml";
	sprintf(cmd, "%s %s", test_wml, name);
	debug(0, "WSP: WML cmd: <%s>", cmd);
	f = popen(cmd, "r");
	if (f == NULL)
		goto error;
	n = fread(wmlc, 1, sizeof(wmlc), f);
	debug(0, "WSP: Read %lu bytes of compiled WMLC", (unsigned long) n);
	fclose(f);
	body = octstr_create_from_data(wmlc, n);
#if 0
	debug(0, "WML: Compiled WML:");
	octstr_dump(body);
#endif

	if (0) {
error:
	panic(errno, "Couldn't write temp file for WML compilation.");
	}
}
#endif
			debug(0, "WSP: WML done.");
		} else if (strcmp(type, "application/vnd.wap.wmlc") == 0) {
			body = octstr_create_from_data(data, size);
		} else if (strcmp(type, "image/vnd.wap.wbmp") == 0) {
			body = octstr_create_from_data(data, size);
		} else if (strcmp(type, "text/vnd.wap.wmlscript") == 0) {
			debug(0, "WSP: Compiling WMLScript");
			type = "application/vnd.wap.wmlscriptc";
#if 1
{
			WsCompilerParams params;
			WsCompilerPtr compiler;
			WsResult result;
			unsigned char *result_data;
			size_t result_size;

			memset(&params, 0, sizeof(params));
			params.use_latin1_strings = 0;
			params.print_symbolic_assembler = 0;
			params.print_assembler = 0;
			params.meta_name_cb = NULL;
			params.meta_name_cb_context = NULL;
			params.meta_http_equiv_cb = NULL;
			params.meta_http_equiv_cb_context = NULL;
			params.stdout_cb = dev_null;
			params.stderr_cb = dev_null;

			compiler = ws_create(&params);
			if (compiler == NULL) {
				panic(0, "WSP: could not create WMLScript compiler");
				exit(1);
			}

			result = ws_compile_data(compiler, url, 
				   data, size, &result_data, &result_size);
			if (result != WS_OK) {
				warning(0, "WSP: WMLScript compilation failed: %s",
					ws_result_to_string(result));
				status = 415; /* XXX wrong code */
			} else {
				body = octstr_create_from_data(result_data,
								result_size);
			}
}
#else
{
	FILE *f;
	char *wmlsc;
	char cmd[100*1024];
	char buf[100*1024];
	size_t n;
	char name[10*1024];
	
	tmpnam(name);
	f = fopen(name, "w");
	if (f == NULL)
		panic(0, "WSP: Couldn't open temp file.");
	fwrite(data, size, 1, f);
	fclose(f);

	wmlsc = getenv("WMLSC");
	if (wmlsc == NULL)
		wmlsc = "./wmlsc";
	sprintf(cmd, "%s %s", wmlsc, name);
	debug(0, "WSP: WMLSC cmd: <%s>", cmd);
	if (system(cmd) != 0)
		panic(0, "WSP: Couldn't run WMLSC compiler.");

	strcat(name, "c");
	f = fopen(name, "r");
	if (f == NULL)
		panic(0, "WSP: Couldn't open compiled temp file.");
	n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	debug(0, "WSP: Read %lu bytes of compiled WMLSC", (unsigned long) n);
	body = octstr_create_from_data(buf, n);
}
#endif
			debug(0, "WSP: WMLScript compilation done.");
		} else {
			status = 415; /* Unsupported media type */
			warning(0, "Unsupported content type `%s'", type);
		}
	}
		
	if (body == NULL)
		body_size = 0;
	else
		body_size = octstr_len(body);
	client_SDU_size = event->SMethodInvokeResult.session->client_SDU_size;

	if (body != NULL && body_size > client_SDU_size) {
		status = 413; /* XXX requested entity too large */
		warning(0, "WSP: Entity at %s too large (size %lu B, limit %lu B)",
			url, body_size, client_SDU_size);
		body = NULL;
		type = "text/plain";
	}

	e = wsp_event_create(SMethodResultRequest);
	e->SMethodResultRequest.server_transaction_id = 
		event->SMethodInvokeResult.server_transaction_id;
	e->SMethodResultRequest.status = status;
	e->SMethodResultRequest.response_type = encode_content_type(type);
	e->SMethodResultRequest.response_body = body;
	e->SMethodResultRequest.machine = event->SMethodInvokeResult.machine;

	debug(0, "WSP: sending S-MethodResult.req to WSP");
	wsp_dispatch_event(event->SMethodInvokeResult.machine, e);

	debug(0, "WSP: wsp_http_thread ends");
	return NULL;
}
