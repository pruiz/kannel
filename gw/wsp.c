/*
 * wsp.c - Implement WSP
 *
 * Lars Wirzenius <liw@wapit.com>
 */


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



static int unpack_uint8(unsigned long *u, Octstr *os, int *off);
static int unpack_uintvar(unsigned long *u, Octstr *os, int *off);
static int unpack_octstr(Octstr **ret, int len, Octstr *os, int *off);



WSPEvent *wsp_event_create(WSPEventType type) {
	WSPEvent *event;
	
	event = malloc(sizeof(WSPEvent));
	if (event == NULL)
		goto error;
	
	event->type = type;

	#define INTEGER(name) p->name = 0
	#define OCTSTR(name) p->name = NULL
	#define MACHINE(name) p->name = NULL
	#define WSP_EVENT(name, fields) \
		{ struct name *p = &event->name; fields }
	#include "wsp_events-decl.h"

	return event;

error:
	error(errno, "Out of memory.");
	return NULL;
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
	debug(0, "  Type: %s (%d)", wsp_event_name(event->type), event->type);
	#define INTEGER(name) debug(0, "  %s: %d", #name, p->name)
	#define OCTSTR(name) debug(0, "  %s:", #name); octstr_dump(p->name)
	#define MACHINE(name) \
		debug(0, "  %s:", #name); wtp_machine_dump(p->name)
	#define WSP_EVENT(name, fields) \
		{ struct name *p = &event->name; fields }
	#include "wsp_events-decl.h"
	debug(0, "Dump of WSPEvent %p ends.", (void *) event);
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
