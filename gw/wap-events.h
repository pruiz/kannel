/*
 * wap-events.h - definitions for wapbox events
 *
 * Aarno Syvänen
 * Lars Wirzenius
 */


#ifndef WAP_EVENTS_H
#define WAP_EVENTS_H

typedef struct WAPEvent WAPEvent;

#include "gwlib/gwlib.h"
#include "wtp.h"
#include "wsp.h"
#include "wapbox.h"


/*
 * Names of WAPEvents.
 */
typedef enum {
	#define WAPEVENT(name, fields) name,
	#include "wap-events-def.h"
	WAPEventNameCount
} WAPEventName;


/*
 * The actual WAPEvent.
 */
struct WAPEvent {
	WAPEventName type;

	union {
	#define WAPEVENT(name, fields) struct name { fields } name;
	#define OCTSTR(name) Octstr *name;
	#define INTEGER(name) long name;
	#define SESSION_MACHINE(name) WSPMachine *name;
	#define HTTPHEADER(name) List *name;
	#define ADDRTUPLE(name) WAPAddrTuple *name;
	#include "wap-events-def.h"
	} u;
};



WAPEvent *wap_event_create(WAPEventName type);
void wap_event_destroy(WAPEvent *event);
WAPEvent *wap_event_duplicate(WAPEvent *event);

const char *wap_event_name(WAPEventName type);
void wap_event_dump(WAPEvent *event);
void wap_event_assert(WAPEvent *event);


#endif
