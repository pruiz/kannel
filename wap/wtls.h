/*
 * WTLS Server Header
 *
 * Nick Clarey <nclarey@3glab.com>
 */

#ifndef WTLS_H
#define WTLS_H

typedef struct WTLSMachine WTLSMachine;

#include "gw/msg.h"
//#include "gw/wapbox.h"
#include "wap/wap_events.h"
#include "wap/wtls_pdu.h"

/*
 * WTLS Server machine states and WTLS machine.
 * See file wtls_state-decl.h for comments. Note that we must define macro
 * ROW to produce an empty string.
 */
enum serv_states {
    #define STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wtls_state-decl.h"
    serv_states_count
};

typedef enum serv_states serv_states;

/*
 * See files wtls_machine-decl.h for comments. We define one macro for 
 * every separate type.
 */ 
struct WTLSMachine {
       long mid;
       #define ENUM(name) serv_states name;
       #define ADDRTUPLE(name) WAPAddrTuple *name;
       #define INTEGER(name) int name;
       #define OCTSTR(name) Octstr *name;
       #define MACHINE(field) field
       #define PDULIST(name) List *name;
       #include "wtls_machine-decl.h"
};

/*
 * Initialize the WTLS server.
 */
void wtls_init(void);

/*
 * Shut down the WTLS server machines. MUST be called after the subsystem isn't
 * used anymore.
 */
void wtls_shutdown(void);

/*
 * Transfers control of an event to the WTLS server machine subsystem.
 */ 
void wtls_dispatch_event(WAPEvent *event);

/*
 * Handles possible concatenated messages. Returns a list of wap events. 
 * Real unpacking is done by an internal function.
 */
WAPEvent *wtls_unpack_wdp_datagram(Msg *msg);

int wtls_get_address_tuple(long mid, WAPAddrTuple **tuple);

#endif
