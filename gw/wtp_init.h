/*
 * WTP initiator header
 *
 * Aarno Syvänen for Wapit Ltd
 */

#ifndef WTP_INIT_H
#define WTP_INIT_H

typedef struct WTPInitMachine WTPInitMachine;

#include "msg.h"
#include "wapbox.h"
#include "wap-events.h"
#include "timers.h"
#include "wtp.h"
#include "wtp_send.h"

/*
 * Initiator machine states and initiator WTP machine.
 * See file wtp_init_state-decl.h for comments. Note that we must define macro
 * ROW to produce an empty string.
 */
enum init_states {
    #define INIT_STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wtp_init_state-decl.h"
    init_states_count
};

typedef enum init_states init_states;

/*
 * See file wtp_init_machine-decl.h for comments. We define one macro for 
 * every separate type.
 */
struct WTPInitMachine {
    long mid; 
    #define INTEGER(name) int name; 
    #define MSG(name) Msg *name; 
    #define TIMER(name) Timer *name; 
    #define ADDRTUPLE(name) WAPAddrTuple *name;
    #define ENUM(name) init_states name;
    #define MACHINE(field) field
    #include "wtp_init_machine-decl.h"
};


/*
 * Initialize the WTP initiator. MUST be called before any other calls
 * to this module.
 */
void wtp_initiator_init(void);

/*
 * Shut down the WTP initiator. MUST be called after the subsystem isn't
 * used anymore.
 */
void wtp_initiator_shutdown(void);

/*
 * Transfers an event to the WTP initiator
 */ 
void wtp_initiator_dispatch_event(WAPEvent *event);

int wtp_initiator_get_address_tuple(long mid, WAPAddrTuple **tuple);

#endif
