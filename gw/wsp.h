/*
 * wsp.h - WSP implementation header
 */

#ifndef WSP_H
#define WSP_H

/*
 * these capability maximum values are real maximum
 * values to our server. It cannot negotiate larger values
 * than these. No way.
 */

#define WSP_MAX_CLIENT_SDU 0
#define WSP_MAX_SERVER_SDU 0
#define WSP_MAX_PROTOCOL_OPTIONS 0x00
#define WSP_MAX_METHOD_MOR 1
#define WSP_MAX_PUSH_MOR 1

/*
 * int WSP_accepted_extended_methods[] = { -1 };
 * int WSP_accepted_header_code_pages[] = { -1 };
 */

/* well, aliases are not negotiated... */

/* use bitflags for set values */
   
#define WSP_CSDU_SET 	1
#define WSP_SSDU_SET 	2
#define WSP_PO_SET 	4
#define WSP_MMOR_SET	8
#define WSP_PMOR_SET	16
#define WSP_EM_SET	32
#define WSP_HCP_SET	64
#define WSP_A_SET	128


typedef struct WSPMachine WSPMachine;
typedef struct WSPMethodMachine WSPMethodMachine;
typedef struct WSPEvent WSPEvent;

#include "gwlib/gwlib.h"
#include "wtp.h"

typedef enum {
	#define WSP_EVENT(name, fields) name,
	#define INTEGER
	#define OCTSTR
	#define WTP_MACHINE
	#define SESSION_MACHINE
	#define HTTPHEADER
	#include "wsp_events-decl.h"
	WSPEventType_count
} WSPEventType;

struct WSPEvent {
	WSPEventType type;
	WSPEvent *next;

	#define INTEGER(name) int name
	#define OCTSTR(name) Octstr *name
	#define WTP_MACHINE(name) WTPMachine *name
	#define SESSION_MACHINE(name) WSPMachine *name
	#define WSP_EVENT(name, fields) struct name fields name;
	#define HTTPHEADER(name) HTTPHeader *name
	#include "wsp_events-decl.h"
};

struct WSPMachine {
	#define MUTEX(name) Mutex *name;
	#define INTEGER(name) long name;
	#define OCTSTR(name) Octstr *name;
	#define EVENT_POINTER(name) WSPEvent *name;
	#define METHOD_POINTER(name) WSPMethodMachine *name;
	#define SESSION_POINTER(name) WSPMachine *name;
	#define HTTPHEADER(name) HTTPHeader *name;
	#define LIST(name) List *name;
	#define SESSION_MACHINE(fields) fields
	#define METHOD_MACHINE(fields)
	#include "wsp_machine-decl.h"
};


struct WSPMethodMachine {
	#define MUTEX(name) Mutex *name;
	#define INTEGER(name) long name;
	#define OCTSTR(name) Octstr *name;
	#define EVENT_POINTER(name) WSPEvent *name;
	#define METHOD_POINTER(name) WSPMethodMachine *name;
	#define SESSION_POINTER(name) WSPMethodMachine *name;
	#define HTTPHEADER(name) HTTPHeader *name;
	#define LIST(name) List *name;
	#define SESSION_MACHINE(fields)
	#define METHOD_MACHINE(fields) fields
	#include "wsp_machine-decl.h"
};


/*
 * Initialize the WSP subsystem. This MUST be called before any other
 * functions in this header.
 */
void wsp_init(void);


/*
 * Create a WSPEvent structure and initialize it to be empty. Return a
 * pointer to the structure or NULL if there was a failure.
 */
WSPEvent *wsp_event_create(WSPEventType type);


/*
 * Destroy a WSPEvent structure, including all its members.
 */
void wsp_event_destroy(WSPEvent *event);


/*
 * Return string giving name of event type.
 */
char *wsp_event_name(WSPEventType type);


/*
 * Output (with `debug' in wapitlib.h) the type of an event and all
 * the fields of that type.
 */
void wsp_event_dump(WSPEvent *event);



/*
 * Find the correct WSPMachine to send the event to, or create a new
 * one, and then make that machine handle the event.
 */
void wsp_dispatch_event(WTPMachine *wtp_sm, WSPEvent *event);


/*
 * Create a WSPMachine structure and initialize it to be empty. Return a
 * pointer to the structure or NULL if there was a failure.
 */
WSPMachine *wsp_machine_create(void);


/*
 * Destroy a WSPMachine structure, including all its members.
 */
void wsp_machine_destroy(WSPMachine *machine);


/*
 * Output (with `debug' in wapitlib.h) a WSPMachine and its fields.
 */
void wsp_machine_dump(WSPMachine *machine);




/*
 * Feed a WSPEvent to a WSPMachine. Handle errors, do not report them to
 * the caller.
 */
void wsp_handle_event(WSPMachine *machine, WSPEvent *event);


int wsp_deduce_pdu_type(Octstr *pdu, int connectionless);


void *wsp_http_thread(void *arg);

/* configure an URL mapping; parses string on whitespace, uses left
 * part for the source URL, and right part for the destination URL
 */
void wsp_http_map_url_config(char *);

/* configure an URL mapping from source DEVICE:home to given string */
void wsp_http_map_url_config_device_home(char *);

/* show all configured URL mappings */
void wsp_http_map_url_config_info(void);

#endif
