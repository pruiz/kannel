/*
 * wsp.h - WSP implementation header
 */

#ifndef WSP_H
#define WSP_H

typedef struct WSPMachine WSPMachine;
typedef struct WSPEvent WSPEvent;

#include "octstr.h"
#include "wtp.h"

typedef enum {
	#define WSP_EVENT(name, fields) name,
	#include "wsp_events-decl.h"
} WSPEventType;

struct WSPEvent {
	WSPEventType type;

	#define INTEGER(name) int name
	#define OCTSTR(name) Octstr *name
	#define MACHINE(name) WTPMachine *name
	#define WSP_EVENT(name, fields) struct name fields name;
	#include "wsp_events-decl.h"
};


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


int wsp_deduce_pdu_type(Octstr *pdu, int connectionless);
int wsp_unpack_connect_pdu(Octstr *pdu);


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
