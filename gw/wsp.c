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


static void append_to_event_queue(WSPMachine *machine, WSPEvent *event);
static WSPEvent *remove_from_event_queue(WSPMachine *machine);

static int unpack_uint8(unsigned long *u, Octstr *os, int *off);
static int unpack_uintvar(unsigned long *u, Octstr *os, int *off);
static int unpack_octstr(Octstr **ret, int len, Octstr *os, int *off);



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
	#define INTEGER(name) debug(0, " %s. %s: %d", t, #name, p->name)
	#define OCTSTR(name) debug(0, "%s.%s:", t, #name); octstr_dump(p->name)
	#define MACHINE(name) \
		debug(0, "%s.%s:", t, #name); wtp_machine_dump(p->name)
	#define WSP_EVENT(type, fields) \
		{ char *t=#type; struct type *p = &event->type; fields }
	#include "wsp_events-decl.h"
	debug(0, "Dump of WSPEvent %p ends.", (void *) event);
}


WSPMachine *wsp_machine_create(void) {
	WSPMachine *p;
	
	p = gw_malloc(sizeof(WSPMachine));
	
	#define MUTEX(name) \
		pthread_mutex_init(&p->name, 0)
	#define INTEGER(name) p->name = 0
	#define METHOD_POINTER(name) p->name = NULL
	#define EVENT_POINTER(name) p->name = NULL
	#define SESSION_POINTER(name) p->name = NULL
	#define SESSION_MACHINE(fields) fields
	#define METHOD_MACHINE(fields)
	#include "wsp_machine-decl.h"
	
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
	if (mutex_try_lock(&sm->mutex) == EBUSY) {
		append_to_event_queue(sm, current_event);
		return;
	}
	
	do {
		#define STATE_NAME(name)
		#define ROW(state_name, event, condition, action, next_state) \
			{ \
				struct event *e = &current_event->event; \
				if (sm->state == state_name && \
				    current_event->type == event && \
				    (condition)) { \
					action \
					sm->state = next_state; \
					goto end; \
				} \
			}
		#include "wsp_state-decl.h"

	end:
		current_event = remove_from_event_queue(sm);
	} while (current_event != NULL);
	
	mutex_unlock(&sm->mutex);
}




int wsp_deduce_pdu_type(Octstr *pdu, int connectionless) {
	int off;
	unsigned long o;

	if (connectionless)
		off = 0;
	else
		off = 1;
	if (unpack_uint8(&o, pdu, &off) == -1)
		return Bad_PDU;
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
	mutex_lock(&machine->queue_lock);
	if (machine->event_queue_head == NULL) {
		machine->event_queue_head = event;
		machine->event_queue_tail = event;
		event->next = NULL;
	} else {
		machine->event_queue_tail->next = event;
		machine->event_queue_tail = event;
		event->next = NULL;
	}
	mutex_unlock(&machine->queue_lock);
}

static WSPEvent *remove_from_event_queue(WSPMachine *machine) {
	WSPEvent *event;
	
	mutex_lock(&machine->queue_lock);
	if (machine->event_queue_head == NULL)
		event = NULL;
	else {
		event = machine->event_queue_head;
		machine->event_queue_head = event->next;
		event->next = NULL;
	}
	mutex_unlock(&machine->queue_lock);
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
