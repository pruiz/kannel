/*
 * WTP initiator header
 *
 * Aarno Syvänen for Wapit Ltd
 */

#ifndef WTP_INIT_H
#define WTP_INIT_H

#include "gwlib/gwlib.h"
#include "wap_addr.h"
#include "wap_events.h"
#include "timers.h"

/*
 * Initiator machine states and initiator WTP machine.
 * See included file for comments. Note that we must define macro
 * ROW to produce an empty string.
 */
enum init_states {
    #define INIT_STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wtp_init_states.def"
    init_states_count
};

typedef enum init_states init_states;

/*
 * See included file for comments. We define one macro for 
 * every separate type.
 */
typedef struct WTPInitMachine {
    unsigned long mid; 
    #define INTEGER(name) int name; 
    #define EVENT(name) WAPEvent *name;
    #define TIMER(name) Timer *name; 
    #define ADDRTUPLE(name) WAPAddrTuple *name;
    #define ENUM(name) init_states name;
    #define MACHINE(field) field
    #include "wtp_init_machine.def"
} WTPInitMachine;

#endif
