/*
 * wsp.c - Implement WSP
 *
 * Lars Wirzenius <liw@wapit.com>
 */


#include "gwlib.h"
#include "wsp.h"
#include "wml.h"

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

#if 0
static int unpack_connect_pdu(Octstr *pdu);
#endif
static int unpack_get_pdu(Octstr **url, Octstr **headers, Octstr *pdu);

static int unpack_uint8(unsigned long *u, Octstr *os, int *off);
static int unpack_uintvar(unsigned long *u, Octstr *os, int *off);
static int unpack_octstr(Octstr **ret, int len, Octstr *os, int *off);

static char *wsp_state_to_string(WSPState state);
static long wsp_next_session_id(void);

static void append_uint8(Octstr *pdu, long n);
static void append_uintvar(Octstr *pdu, long n);
static void append_octstr(Octstr *pdu, Octstr *os);

static Octstr *make_connectionmode_pdu(long type);
static Octstr *make_connectreply_pdu(long session_id);
static Octstr *make_reply_pdu(long status, Octstr *body);

static long convert_http_status_to_wsp_status(long http_status);

static int transaction_belongs_to_session(WTPMachine *wtp, WSPMachine *session);

static void *wsp_http_thread(void *arg);



WSPEvent *wsp_event_create(WSPEventType type) {
	WSPEvent *event;
	
	event = gw_malloc(sizeof(WSPEvent));
	event->type = type;
	event->next = NULL;

	#define INTEGER(name) p->name = 0
	#define OCTSTR(name) p->name = NULL
	#define MACHINE(name) p->name = NULL
	#define WSP_EVENT(name, fields) \
		{ struct name *p = &event->name; fields }
	#include "wsp_events-decl.h"

	return event;
}


void wsp_event_destroy(WSPEvent *event) {
	if (event != NULL) {
		#define INTEGER(name) p->name = 0
		#define OCTSTR(name) octstr_destroy(p->name)
		#define MACHINE(name) p->name = NULL
		#define WSP_EVENT(name, fields) \
			{ struct name *p = &event->name; fields }
		#include "wsp_events-decl.h"

		free(event);
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
	#define MACHINE(name) \
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
	
#if 0
	debug(0, "wsp_dispatch_event called");
#endif

	for (sm = session_machines; sm != NULL; sm = sm->next)
		if (transaction_belongs_to_session(wtp_sm, sm))
			break;

	if (sm == NULL) {
		sm = wsp_machine_create();
#if 0
		debug(0, "wsp_dispatch_event: machine %p created", 
			(void *) sm);
#endif

		sm->client_address = octstr_duplicate(wtp_sm->source_address);
		sm->client_port = wtp_sm->source_port;
		sm->server_address = 
			octstr_duplicate(wtp_sm->destination_address);
		sm->server_port = wtp_sm->destination_port;
#if 0
		debug(0, "wsp_dispatch_event: machine initialized");
#endif
	} else
#if 0
		debug(0, "wsp_dispatch_event: found machine %p", (void *) sm);
#else
		;
#endif

	wsp_handle_event(sm, event);
#if 0
	debug(0, "wsp_dispatch_event: done");
#endif
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
#if 0
	debug(0, "wsp_handle_event called");
#endif

	/* 
	 * If we're already handling events for this machine, add the
	 * event to the queue.
	 */
	if (mutex_try_lock(sm->mutex) == -1) {
#if 0
		debug(0, "wsp_handle_event: machine already locked, queing event");
#endif
		append_to_event_queue(sm, current_event);
		return;
	}

#if 0
	debug(0, "wsp_handle_event: got mutex");
#endif
	
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
		error(0, "WSP: Can't handle event.");
		debug(0, "WSP: The unhandled event:");
		wsp_event_dump(current_event);

	end:
		current_event = remove_from_event_queue(sm);
	} while (current_event != NULL);
#if 0
	debug(0, "wsp_handle_event: done handling events");
#endif
	
	mutex_unlock(sm->mutex);
#if 0
	debug(0, "wsp_handle_event: done");
#endif
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
#if 0
	debug(0, "wsp_deduce_pdu_type: 0x%02lx (connectionless: %d)", o,
		connectionless);
#endif
	return o;
}


/***********************************************************************
 * Local functions
 */


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


#if 0
static int unpack_connect_pdu(Octstr *user_data) {
	int off;
	unsigned long version, caps_len, headers_len;
	Octstr *caps, *headers;

	off = 0;
	if (unpack_uint8(&version, user_data, &off) == -1 ||
	    unpack_uintvar(&caps_len, user_data, &off) == -1 ||
	    unpack_uintvar(&headers_len, user_data, &off) == -1 ||
	    unpack_octstr(&caps, caps_len, user_data, &off) == -1 ||
	    unpack_octstr(&headers, headers_len, user_data, &off) == -1)
		return -1;
	debug(0, "Unpacked Connect PDU:");
	debug(0, "  version=%lu", version);
	debug(0, "  caps_len=%lu", caps_len);
	debug(0, "  headers_len=%lu", headers_len);
	debug(0, "  caps:");
	octstr_dump(caps);
	debug(0, "  headers:");
	octstr_dump(headers);
	debug(0, "Connect PDU dump done.");
	return 0;
}
#endif



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
	if (pdu == NULL)
		panic(0, "octstr_create failed, out of memory");
	append_uint8(pdu, type);
	return pdu;
}


static void append_uint8(Octstr *pdu, long n) {
	unsigned char c;
	
	c = (unsigned char) n;
	if (octstr_insert_data(pdu, octstr_len(pdu), &c, 1) == -1)
		panic(0, "octstr_insert_data failed, out of memory");
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
	if (octstr_insert(pdu, os, octstr_len(pdu)) == -1)
		panic(0, "octstr_insert failed, out of memory");
}


static Octstr *make_connectreply_pdu(long session_id) {
	Octstr *pdu;
	
	pdu = make_connectionmode_pdu(ConnectReply_PDU);
	append_uintvar(pdu, session_id);
	append_uintvar(pdu, 0);
	append_uintvar(pdu, 0);
	return pdu;
}


static Octstr *make_reply_pdu(long status, Octstr *body) {
	Octstr *pdu;
	
	/* XXX this is a hardcoded kludge */
	pdu = make_connectionmode_pdu(Reply_PDU);
	append_uint8(pdu, convert_http_status_to_wsp_status(status));
	append_uintvar(pdu, 1);
	append_uint8(pdu, 0x94); /* XXX */
	append_octstr(pdu, body);
	return pdu;
}


static long convert_http_status_to_wsp_status(long http_status) {
	static struct {
		long http_status;
		long wsp_status;
	} tab[] = {
		{ 200, 0x20 },
	};
	int i;
	
	for (i = 0; i < sizeof(tab) / sizeof(tab[0]); ++i)
		if (tab[i].http_status == http_status)
			return tab[i].wsp_status;
	return 0x60; /* Status 500, or "Internal Server Error" */
}


static int transaction_belongs_to_session(WTPMachine *wtp, WSPMachine *session)
{
	return 
	  octstr_compare(wtp->source_address, session->client_address) == 0 &&
	  wtp->source_port == session->client_port &&
	  octstr_compare(wtp->destination_address, session->server_address) == 0 && 
	  wtp->destination_port == session->server_port;
}


static void *wsp_http_thread(void *arg) {
#if 1
	char *type, *data;
	size_t size;
	Octstr *body;
	WSPEvent *e;
	int status;
	struct wmlc *wmlc_data;
#endif
	char *url;
	WSPEvent *event;

	debug(0, "WSP: wsp_http_thread starts");

	event = arg;
	debug(0, "WSP: Sending S-MethodInvoke.Res to WSP");
	wsp_dispatch_event(event->SMethodInvokeResult.machine, event);

	url = octstr_get_cstr(event->SMethodInvokeResult.url);
	debug(0, "WSP: url is <%s>", url);
#if 1
	if (http_get(url, &type, &data, &size) == -1) {
		error(0, "WSP: http_get failed, oops.");
		status = 500; /* Internal server error */
		body = NULL;
	} else {
		info(0, "WSP: Fetched <%s>", url);
		status = 200; /* OK */

		data = gw_realloc(data, size + 1);
		data[size] = '\0';

		wmlc_data = wml2wmlc(data);
		if (wmlc_data == NULL)
			panic(0, "Out of memory");
		
		body = octstr_create_from_data(wmlc_data->wbxml, 
						wmlc_data->wml_length);
		if (body == NULL)
			panic(0, "octstr_create_from_data failed, oops");
	}
		
	e = wsp_event_create(SMethodResultRequest);
	e->SMethodResultRequest.server_transaction_id = 
		event->SMethodInvokeIndication.server_transaction_id;
	e->SMethodResultRequest.status = 200;
	e->SMethodResultRequest.response_body = body;
	e->SMethodResultRequest.machine = 
		event->SMethodInvokeResult.machine;
	debug(0, "WSP: sending S-MethodResult.req to WSP");
	wsp_dispatch_event(event->SMethodInvokeResult.machine, e);
#else
	{
		Octstr *data;
		WSPEvent *e;
		
		data = octstr_read_file("../main.wmlc");
		if (data == NULL) {
			error(0, "octstr_read_file failed, oops");
			goto end;
		}
		
		e = wsp_event_create(SMethodResultRequest);
		e->SMethodResultRequest.server_transaction_id = 
			event->SMethodInvokeIndication.server_transaction_id;
		e->SMethodResultRequest.status = 200;
		e->SMethodResultRequest.response_body = data;
		e->SMethodResultRequest.machine = 
			event->SMethodInvokeResult.machine;
		debug(0, "WSP: sending S-MethodResult.req to WSP");
		wsp_dispatch_event(event->SMethodInvokeResult.machine, e);
	}
#endif

end:
	debug(0, "WSP: wsp_http_thread ends");
	return NULL;
}
