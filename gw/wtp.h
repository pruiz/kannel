/*
 * wtp.h - WTP implementation header
 */

#ifndef WTP_H
#define WTP_H

typedef struct WTPMachine WTPMachine;
typedef struct WTPEvent WTPEvent;
typedef struct WSPEvent WSPEvent;

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "gwlib.h"
#include "msg.h" 
#include "wtp_timer.h" 
#include "wtp_send.h"

#define NUMBER_OF_ABORT_REASONS 8
/*
 *For now, timers are defined. They will depend on bearer information fetched
 *from address.
 */

#define L_A_WITH_USER_ACK 4
#define L_R_WITH_USER_ACK 7

enum event_name {
     #define EVENT(name, field) name,
     #include "wtp_events-decl.h"
};

enum states {
    #define STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wtp_state-decl.h"
};

typedef enum states states;

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



