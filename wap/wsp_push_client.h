/*
 * wsp_push_client.h: push client (for testing) interface
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#ifndef WSP_PUSH_CLIENT_H
#define WSP_PUSH_CLIENT_H

typedef struct WSPPushClientMachine WSPPushClientMachine;

#include "gwlib/gwlib.h"
#include "wap_addr.h"
#include "wap_events.h"
#include "wap.h"

/*
 * Push client states
 */
enum push_client_states {
    #define PUSH_CLIENT_STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wsp_push_client_states.def"
    push_client_states_count
};

typedef enum push_client_states push_client_states;

/*
 * Declaration of push client state machine. We define one macro for every 
 * separate type.
 */
struct WSPPushClientMachine {
    long cpid;
    #define INTEGER(name) int name;
    #define HTTPHEADERS(name) List *name;
    #define MACHINE(fields) fields
    #include "wsp_push_client_machine.def"
};

#endif
