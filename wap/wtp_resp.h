/*
 * WTP responder header
 *
 * Aarno Syvänen for Wapit Ltd
 */

#ifndef WTP_RESPONDER_H
#define WTP_RESPONDER_H

typedef struct WTPRespMachine WTPRespMachine;

#include "gwlib/gwlib.h"
#include "wap_events.h"
#include "timers.h"

/*
 * Responder machine states and responder WTP machine.
 * See file wtp_resp_state-decl.h for comments. Note that we must define macro
 * ROW to produce an empty string.
 */
enum resp_states {
    #define STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wtp_resp_states.def"
    resp_states_count
};

typedef enum resp_states resp_states;

/*
 * See files wtp_resp_machine-decl.h and for comments. We define one macro for 
 * every separate type.
 */ 
struct WTPRespMachine {
       unsigned long mid; 
       #define INTEGER(name) int name; 
       #define TIMER(name) Timer *name; 
       #define ADDRTUPLE(name) WAPAddrTuple *name;
       #define ENUM(name) resp_states name;
       #define EVENT(name) WAPEvent *name;
       #define MACHINE(field) field
       #include "wtp_resp_machine.def"
};

#endif
