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

#include "gwlib.h"
#include "msg.h" 
#include "wsp.h" 
#include "wtp_timer.h" 

#define NUMBER_OF_ABORT_REASONS 8
/*
 * For now, timers are defined. They will depend on bearer information fetched
 * from address (or from a header field of the protocol speaking with the 
 * bearerbox).
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


struct WTPMachine {
        #define INTEGER(name) long name
        #define ENUM(name) states name
        #define OCTSTR(name) Octstr *name
        #define QUEUE(name) WTPEvent *name
	#define TIMER(name) WTPTimer *name
        #define MUTEX(name) pthread_mutex_t name
        #define NEXT(name) struct WTPMachine *name
        #define MACHINE(field) field
        #include "wtp_machine-decl.h"

};


struct WTPEvent {
    enum event_name type;
    WTPEvent *next;

    #define INTEGER(name) long name
    #define OCTSTR(name) Octstr *name
    #define EVENT(name, field) struct name field name;
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
 * Output (with `debug' in log.h) the type of an event and all
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


WTPMachine *wtp_machine_create(Octstr *srcaddr, long srcport, 
				Octstr *destaddr, long destport);
WTPMachine *wtp_machine_find_or_create(Msg *msg, WTPEvent *event);


/*
 * Mark a WTP state machine unused. Normally, removing a state machine from the
 * state machines list means marking turning off a flag. Panics when there is
 * no machines to mark unused.
 */
void wtp_machine_mark_unused(WTPMachine *machine);


/*
 * Destroy a WTPMachine structure, including all its members. Remove the
 * structure from the global list of WTPMachine structures. This function is
 * used only by the garbage collection.
 */
void wtp_machine_destroy(WTPMachine *machine);


/*
 * Output (with `debug' in gwlib/log.h) the state of the machine  and all
 * its fields.
 */
void wtp_machine_dump(WTPMachine  *machine);


/*
 * Feed an event to a WTP state machine. Handle all errors yourself,
 * and report them to the caller. Generate a pointer to WSP event, if an 
 * indication or a confirmation is required.
 */
void wtp_handle_event(WTPMachine *machine, WTPEvent *event);

/*
 * Generates a new transaction handle by incrementing the previous one by one.
 */
long wtp_tid_next(void);

#endif

