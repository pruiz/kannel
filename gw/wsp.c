/*
 * wsp.c - Implement WSP
 *
 * Lars Wirzenius <liw@wapit.com>
 */


#include "gwlib.h"
#include "wsp.h"

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

static int unpack_uint8(unsigned long *u, Octstr *os, int *off);
static int unpack_uintvar(unsigned long *u, Octstr *os, int *off);
static int unpack_octstr(Octstr **ret, int len, Octstr *os, int *off);

static char *wsp_state_to_string(WSPState state);
static long wsp_next_session_id(void);

static Octstr *make_connectionmode_pdu(long type);
static void append_uint8(Octstr *pdu, long n);
static void append_uintvar(Octstr *pdu, long n);
static Octstr *make_connectreply_pdu(long session_id);



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
	#define WSP_EVENT(type, fields) \
		{ char *t = #type; struct type *p = &event->type; fields }
	#include "wsp_events-decl.h"
	debug(0, "Dump of WSPEvent %p ends.", (void *) event);
}


void wsp_dispatch_event(WTPMachine *wtp_sm, WSPEvent *event) {
	/* XXX this now always creates a new machine for each event, boo */
	
	WSPMachine *sm;
	
	debug(0, "wsp_dispatch_event called");
	sm = wsp_machine_create();
	debug(0, "wsp_dispatch_event: machine created");
	sm->client_address = octstr_duplicate(wtp_sm->source_address);
	sm->client_port = wtp_sm->source_port;
	sm->server_address = octstr_duplicate(wtp_sm->destination_address);
	sm->server_port = wtp_sm->destination_port;
	debug(0, "wsp_dispatch_event: machine initialized");
	wsp_handle_event(sm, event);
	debug(0, "wsp_dispatch_event: done");
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
	int done;
	
	debug(0, "wsp_handle_event called");

	/* 
	 * If we're already handling events for this machine, add the
	 * event to the queue.
	 */
	if (mutex_try_lock(sm->mutex) == -1) {
		debug(0, "wsp_handle_event: machine already locked, queing event");
		append_to_event_queue(sm, current_event);
		return;
	}

	debug(0, "wsp_handle_event: got mutex");
	
	do {
		debug(0, "wsp_handle_event: current state is %s, event is %s",
			wsp_state_to_string(sm->state), 
			wsp_event_name(current_event->type));
#if 0
		debug(0, "wsp_handle_event: event is:");
		wsp_event_dump(current_event);
#endif

		done = 0;
		#define STATE_NAME(name)
		#define ROW(state_name, event, condition, action, next_state) \
			{ \
				struct event *e; \
				e = &current_event->event; \
				if (!done && sm->state == state_name && \
				    current_event->type == event && \
				    (condition)) { \
				        debug(0, "WSP: entering %s handler", \
						#state_name); \
					action \
					debug(0, "WSP: setting state to %s", \
						#next_state); \
					sm->state = next_state; \
					done = 1; \
					goto end; \
				} \
			}
		#include "wsp_state-decl.h"
		if (!done) {
			error(0, "wsp_handle_event: Can't handle event.");
		}

	end:
		current_event = remove_from_event_queue(sm);
	} while (current_event != NULL);
	debug(0, "wsp_handle_event: done handling events");
	
	mutex_unlock(sm->mutex);
	debug(0, "wsp_handle_event: done");
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
	debug(0, "wsp_deduce_pdu_type: 0x%02lx (connectionless: %d)", o,
		connectionless);
	return o;
}


int wsp_unpack_connect_pdu(Octstr *user_data) {
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


static int unpack_uint8(unsigned long *u, Octstr *os, int *off) {
	if (*off >= octstr_len(os))
		return -1;
	*u = octstr_get_char(os, *off);
	++(*off);
	return 0;
}


static int unpack_uintvar(unsigned long *u, Octstr *os, int *off) {
	unsigned long o;
	
	*u = 0;
	do {
		if (unpack_uint8(&o, os, off) == -1)
			return -1;
		*u = ((*u) << 7) | (o & 0x7F);
	} while ((o & 0x80) == 1);

	return 0;
}


static int unpack_octstr(Octstr **ret, int len, Octstr *os, int *off) {
	if (*off + len > octstr_len(os))
		return -1;
	*ret = octstr_copy(os, 0, len);
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
	if (n == 0)
		append_uint8(pdu, 0);
	else {
		while ((n & 0xFE000000) == 0)
			n <<= 7;
		while ((n & 0x01FFFFFF) != 0) {
			append_uint8(pdu, 0x80 | ((n & 0xFE000000) >> 25));
			n <<= 7;
		}
		if (n != 0)
			append_uint8(pdu, (n & 0xFE000000) >> 25);
	}
}


static Octstr *make_connectreply_pdu(long session_id) {
	Octstr *pdu;
	
	pdu = make_connectionmode_pdu(ConnectReply_PDU);
	append_uintvar(pdu, session_id);
	append_uintvar(pdu, 0);
	append_uintvar(pdu, 0);
	return pdu;
}
