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

	#define WAPEVENT(name, fields) \
		{ struct name *p = &event->name; fields }
	#define OCTSTR(name) p->name = NULL;
	#define INTEGER(name) p->name = 0;
	#define WTP_MACHINE(name) p->name = NULL;
	#define SESSION_MACHINE(name) p->name = NULL;
	#define HTTPHEADER(name) p->name = NULL;
	#include "wap-events-def.h"
	
	return event;
}


void wap_event_destroy(WAPEvent *event) {
	if (event == NULL)
		return;

	wap_event_assert(event);

	switch (event->type) {
	#define WAPEVENT(name, fields) \
		case name: \
			{ struct name *p = &event->name; fields; break; }
	#define OCTSTR(name) octstr_destroy(p->name);
	#define INTEGER(name) p->name = 0;
	#define WTP_MACHINE(name) p->name = NULL;
	#define SESSION_MACHINE(name) p->name = NULL;
	#define HTTPHEADER(name) http2_destroy_headers(p->name);
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

	#define WAPEVENT(name, fields) \
		{ struct name *p = &new->name, *q = &event->name; fields }
	#define OCTSTR(name) p->name = octstr_duplicate(q->name);
	#define INTEGER(name) p->name = q->name;
	#define WTP_MACHINE(name) p->name = q->name;
	#define SESSION_MACHINE(name) p->name = q->name;
	#define HTTPHEADER(name) p->name = http2_header_duplicate(q->name);
	#include "wap-events-def.h"
	
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
			{ struct name *p = &event->name; fields; break; }
		#define OCTSTR(name) \
			octstr_dump(p->name, 1);
		#define INTEGER(name) \
			debug("wap.event", 0, "  %s = %ld", #name, p->name);
		#define WTP_MACHINE(name) \
			debug("wap.event", 0, "  %s = %p", \
				#name, (void *) p->name);
		#define SESSION_MACHINE(name) \
			debug("wap.event", 0, "  %s = %p", \
				#name, (void *) p->name);
		#define HTTPHEADER(name) \
			http2_header_dump(p->name);
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

#if 0 /* XXX the following is bogus. --liw */
	switch (event->type) {
	#define WAPEVENT(name, fields) \
		case name: \
		{ struct name *p = &event->name; fields; break; }
	#define OCTSTR(name) octstr_len(p->name);
	#define INTEGER(name) gw_assert(p != NULL);
	#define HTTPHEADER(name) gw_assert(p->name != NULL);
	#define WTP_MACHINE(name) gw_assert(p->name != NULL);
	#define SESSION_MACHINE(name) gw_assert(p->name != NULL);
	#include "wap-events-def.h"
	default:
		gw_assert(0);
	}
#endif
}
