/*
 * wap-events.c - functions for manipulating wapbox events
 *
 * Aarno Syvänen
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"
#include "wap-events.h"


WAPEvent *wap_event_create(WAPEventName type) {
	WAPEvent *event;
	
	gw_assert(type >= 0);
	gw_assert(type < WAPEventNameCount);

	event = gw_malloc(sizeof(WAPEvent));
	event->type = type;

	switch (event->type) {
	#define WAPEVENT(name, fields) \
		case name: \
			{ struct name *p = &event->u.name; fields } \
			break;
	#define OCTSTR(name) p->name = NULL;
	#define INTEGER(name) p->name = 0;
	#define SESSION_MACHINE(name) p->name = NULL;
	#define HTTPHEADER(name) p->name = NULL;
	#define ADDRTUPLE(name) p->name = NULL;
	#include "wap-events-def.h"
	default:
		panic(0, "Unknown WAP event type %d", event->type);
	}
	
	return event;
}


void wap_event_destroy(WAPEvent *event) {
	if (event == NULL)
		return;

	wap_event_assert(event);

	switch (event->type) {
	#define WAPEVENT(name, fields) \
		case name: \
			{ struct name *p = &event->u.name; fields; } \
			break;
	#define OCTSTR(name) octstr_destroy(p->name);
	#define INTEGER(name) p->name = 0;
	#define SESSION_MACHINE(name) p->name = NULL;
	#define HTTPHEADER(name) http_destroy_headers(p->name);
	#define ADDRTUPLE(name) wap_addr_tuple_destroy(p->name);
	#include "wap-events-def.h"
	default:
		panic(0, "Unknown WAPEvent type %d", (int) event->type);
	}
	gw_free(event);
}


WAPEvent *wap_event_duplicate(WAPEvent *event) {
	WAPEvent *new;
	
	if (event == NULL)
		return NULL;

	wap_event_assert(event);

	new = gw_malloc(sizeof(WAPEvent));
	new->type = event->type;

	switch (event->type) {
	#define WAPEVENT(name, fields) \
		case name: \
			{ struct name *p = &new->u.name; \
			  struct name *q = &event->u.name; \
			  fields } \
			break;
	#define OCTSTR(name) p->name = octstr_duplicate(q->name);
	#define INTEGER(name) p->name = q->name;
	#define SESSION_MACHINE(name) p->name = q->name;
	#define HTTPHEADER(name) p->name = http_header_duplicate(q->name);
	#define ADDRTUPLE(name) p->name = wap_addr_tuple_duplicate(q->name);
	#include "wap-events-def.h"
	default:
		panic(0, "Unknown WAP event type %d", event->type);
	}
	
	return new;
}


const char *wap_event_name(WAPEventName type) {
	switch (type) {
	#define WAPEVENT(name, fields) \
		case name: return #name;
	#include "wap-events-def.h"
	default:
		panic(0, "Unknown WAPEvent type %d", (int) type);
		return "unknown WAPEventName";
	}
}


void wap_event_dump(WAPEvent *event) {
	debug("wap.event", 0, "Dumping WAPEvent %p", (void *) event);
	if (event != NULL) {
		debug("wap.event", 0, "  type = %s", 
			wap_event_name(event->type));
		switch (event->type) {
		#define WAPEVENT(name, fields) \
			case name: \
			{ struct name *p = &event->u.name; fields; break; }
		#define OCTSTR(name) \
			debug("wap.event", 0, "%s =", #name); \
			octstr_dump(p->name, 1);
		#define INTEGER(name) \
			debug("wap.event", 0, "  %s = %ld", #name, p->name);
		#define SESSION_MACHINE(name) \
			debug("wap.event", 0, "  %s = %p", \
				#name, (void *) p->name);
		#define HTTPHEADER(name) \
			http_header_dump(p->name);
		#define ADDRTUPLE(name) \
			wap_addr_tuple_dump(p->name);
		#include "wap-events-def.h"
		default:
			debug("wap.event", 0, "Unknown type");
		}
	}
	debug("wap.event", 0, "WAPEvent dump ends.");
}



void wap_event_assert(WAPEvent *event) {
	gw_assert(event != NULL),
	gw_assert(event->type >= 0);
	gw_assert(event->type < WAPEventNameCount);
}
