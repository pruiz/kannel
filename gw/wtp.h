/*
 * wtp.h - WTP implementation header
 */

#ifndef WTP_H
#define WTP_H

typedef struct WTPMachine WTPMachine;
typedef struct WTPEvent WTPEvent;

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "wapitlib.h"
#include "msg.h" 
#include "wtp_timer.h" 

#define NUMBER_OF_ABORT_REASONS 8

enum event_name {
     #define EVENT(name, field) name,
     #include "wtp_events-decl.h"
};


/*
 * Create a WTPEvent structure and initialize it to be empty. Return a
 * pointer to the structure or NULL if there was a failure.
 */
WTPEvent *wtp_event_create(enum event_name type);


/*
 * Destroy a WTPEvent structure, including all its members.
 */
void wtp_event_destroy(WTPEvent *event);


/*
 * Output (with `debug' in wapitlib.h) the type of an event and all
 * the fields of that type.
 */
void wtp_event_dump(WTPEvent *event);


/*
 * Parse a `wdp_datagram' message object (of type Msg, see msg.h) and
 * create a corresponding WTPEvent object. Also check that the datagram
 * is syntactically valid. If there is a problem (memory allocation or
 * invalid packet), then return NULL, and send an appropriate error
 * packet to the phone. Otherwise return a pointer to the event structure
 * that has been created.
 */
WTPEvent *wtp_unpack_wdp_datagram(Msg *msg);


/*
 * Create and initialize a WTPMachine structure. Return a pointer to it,
 * or NULL if there was a problem. Add the structure to a global list of
 * all WTPMachine structures (see wtp_machine_find).
 */
WTPMachine *wtp_machine_create(void);


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
 * Find the WTPMachine from the global list of WTPMachine structures that
 * corresponds to the five-tuple of source and destination addresses and
 * ports and the transaction identifier. Return a pointer to the machine,
 * or NULL if not found.
 */
WTPMachine *wtp_machine_find(Octstr *source_address, long source_port,
	Octstr *destination_address, long destination_port, long tid);


/*
 * Feed an event to a WTP state machine. Handle all errors yourself,
 * don't report them to the caller.
 */
void wtp_handle_event(WTPMachine *machine, WTPEvent *event);

#endif
