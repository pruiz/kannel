/*
 * WTP responder header
 *
 * Aarno Syvänen for Wapit Ltd
 */

#ifndef WTP_RESPONDER_H
#define WTP_RESPONDER_H

typedef struct WTPRespMachine WTPRespMachine;

#include "msg.h"
#include "wapbox.h"
#include "wap-events.h"
#include "wtp.h"
#include "wtp_tid.h"
#include "wtp_send.h"

/*
 * Responder machine states and responder WTP machine.
 * See file wtp_resp_state-decl.h for comments. Note that we must define macro
 * ROW to produce an empty string.
 */
enum resp_states {
    #define STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wtp_resp_state-decl.h"
    resp_states_count
};

typedef enum resp_states resp_states;

/*
 * See files wtp_resp_machine-decl.h and for comments. We define one macro for 
 * every separate type.
 */ 
struct WTPRespMachine {
       long mid; 
       #define INTEGER(name) int name; 
       #define MSG(name) Msg *name; 
       #define TIMER(name) Timer *name; 
       #define ADDRTUPLE(name) WAPAddrTuple *name;
       #define ENUM(name) resp_states name;
       #define WSP_EVENT(name) WAPEvent *name;
       #define MACHINE(field) field
       #include "wtp_resp_machine-decl.h"
};


/*
 * Initialize the WTP responder. MUST be called before any other calls
 * to this module.
 */
void wtp_resp_init(void);

/*
 * Shut down the WTP responder. MUST be called after the subsystem isn't
 * used anymore.
 */
void wtp_resp_shutdown(void);

/*
 * Transfers an event to the WTP responder
 */ 
void wtp_resp_dispatch_event(WAPEvent *event);

int wtp_resp_get_address_tuple(long mid, WAPAddrTuple **tuple);

#endif
