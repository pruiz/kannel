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

#include "octstr.h"
#include "wtp.h"

typedef enum {
	#define WSP_EVENT(name, fields) name,
	#include "wsp_events-decl.h"
} WSPEventType;

struct WSPEvent {
	WSPEventType type;
	WSPEvent *next;

	#define INTEGER(name) int name
	#define OCTSTR(name) Octstr *name
	#define WTP_MACHINE(name) WTPMachine *name
	#define SESSION_MACHINE(name) WSPMachine *name
	#define WSP_EVENT(name, fields) struct name fields name;
	#include "wsp_events-decl.h"
};

struct WSPMachine
	#define MUTEX(name) Mutex *name
	#define INTEGER(name) long name
	#define OCTSTR(name) Octstr *name
	#define EVENT_POINTER(name) WSPEvent *name
	#define METHOD_POINTER(name) WSPMethodMachine *name
	#define SESSION_POINTER(name) WSPMachine *name
	#define SESSION_MACHINE(fields) fields
	#define METHOD_MACHINE(fields)
	#include "wsp_machine-decl.h"
;


struct WSPMethodMachine
	#define MUTEX(name) Mutex *name
	#define INTEGER(name) long name
	#define METHOD_POINTER(name) WSPMethodMachine *name
	#define SESSION_POINTER(name) WSPMethodMachine *name
	#define SESSION_MACHINE(fields)
	#define METHOD_MACHINE(fields) fields
	#include "wsp_machine-decl.h"
;


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


#if 0

/*
 * Parse the user_data portion of a TR-Invoke.ind event into a WSPEvent
 * object. If there is a problem (memory allocation or invalid packet), 
 * then return NULL, and send an appropriate error packet to the phone. 
 * Otherwise return a pointer to the event structure that has been created.
 */
WSPEvent *wsp_unpack_tr_invoke_ind(...);


WTPMachine *create_or_find_wtp_machine(Msg *msg, WTPEvent *event);


/*
 *Mark a WTP state machine unused. Normally, removing a stete machine from the
 *state machines list means marking it unused.
 */
void wtp_machine_mark_unused(WTPMachine *machine);


/*
 * Destroy a WTPMachine structure, including all its members. Remove the
 * structure from the global list of WTPMachine structures. This function is
 * used only by the garbage collection.
 */
void wtp_machine_destroy(WTPMachine *machine);


/*
 * Output (with `debug' in wapitlib.h) the state of the machine  and all
 * its fields.
 */
void wtp_machine_dump(WTPMachine  *machine);


/*
 * Feed an event to a WTP state machine. Handle all errors yourself,
 * and report them to the caller. Generate a pointer to WSP event, if an 
 * indication or a confirmation is required.
 *
 *Returns: WSPEvent, if succeeded and an indication or a confirmation is 
 *          generated
 *          NULL, if succeeded and no indication or confirmation is generated
 *          NULL, if failed (this information is superflous, but required by
 *          the function call syntax.)
 */
WSPEvent *wtp_handle_event(WTPMachine *machine, WTPEvent *event);


#endif
#endif
