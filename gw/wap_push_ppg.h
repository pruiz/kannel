/*
 * Push PPG main module header
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#ifndef WAP_PUSH_PPG_H
#define WAP_PUSH_PPG_H

#include "wap/wap_events.h"
#include "wap/wap.h"
#include "wap/wap_addr.h"

typedef struct PPGSessionMachine PPGSessionMachine;
typedef struct PPGPushMachine PPGPushMachine;

/*
 * Enumerations used by PPG main module for PAP attribute, see PPG Services, 
 * Chapter 6.
 *
 * Message state
 */
enum {
    PAP_UNDELIVERABLE,         /* general message status */
    PAP_UNDELIVERABLE1,        /* transformation failure */
    PAP_UNDELIVERABLE2,        /* no bearer support */
    PAP_PENDING,
    PAP_EXPIRED,
    PAP_DELIVERED,             /* general message status */
    PAP_DELIVERED1,            /* for unconfirmed push, PPG internal */
    PAP_DELIVERED2,            /* for confirmed push, PPG internal  */
    PAP_ABORTED,
    PAP_TIMEOUT,
    PAP_CANCELLED
};

/*
 * PAP protocol status codes used by PPG main module. See Push Access Protocol,
 * 9.13 and 9.14. 
 */
enum {
    PAP_OK = 1000,
    PAP_ACCEPTED_FOR_PROCESSING = 1001,
    PAP_BAD_REQUEST = 2000, 
    PAP_FORBIDDEN = 2001,
    PAP_ADDRESS_ERROR = 2002,
    PAP_CAPABILITIES_MISMATCH = 2005,
    PAP_DUPLICATE_PUSH_ID = 2007,
    PAP_INTERNAL_SERVER_ERROR = 3000,
    PAP_TRANSFORMATION_FAILURE = 3006,
    PAP_REQUIRED_BEARER_NOT_AVAILABLE = 3010,
    PAP_SERVICE_FAILURE = 4000,
    PAP_CLIENT_ABORTED = 5000,
    PAP_ABORT_USERPND = 5028
};

/*
 * Values for last attribute (it is, is this message last using this bearer).
 */
enum {
    NOT_LAST,
    LAST
};

/*
 * Enumerations used by PAP message fields, see Push Access Protocol, Chapter
 * 9. Default values are the first ones (ones having value 0)
 *
 * Simple answer to question is something required or not
 */
enum {
    PAP_FALSE,
    PAP_TRUE
};

/*
 * Priority
 */
enum {
    PAP_MEDIUM,
    PAP_HIGH,
    PAP_LOW
};

/*
 * Delivery method
 */
enum {
    PAP_NOT_SPECIFIED = 0,
    PAP_PREFERCONFIRMED = 1,
    PAP_UNCONFIRMED = 2,
    PAP_CONFIRMED = 3
};

/*
 * Port number definitions
 */
enum {
    CONNECTIONLESS_PUSH_CLIPORT = 2948,
    CONNECTIONLESS_SERVPORT = 9200,
    CONNECTED_CLIPORT = 9209,
    CONNECTED_SERVPORT = 9201
};

struct PPGSessionMachine {
    #define OCTSTR(name) Octstr *name;
    #define ADDRTUPLE(name) WAPAddrTuple *name;
    #define INTEGER(name) long name;
    #define PUSHMACHINES(name) List *name;
    #define CAPABILITIES(name) List *name;
    #define MACHINE(fields) fields
    #include "wap_ppg_session_machine.def"
};

struct PPGPushMachine {
    #define OCTSTR(name) Octstr *name;
    #define INTEGER(name) long name;
    #define ADDRTUPLE(name) WAPAddrTuple *name;
    #define HTTPHEADER(name) List *name;
    #define CAPABILITIES(name) List *name;
    #define MACHINE(fields) fields
    #include "wap_ppg_push_machine.def"
};

void wap_push_ppg_init(wap_dispatch_func_t *ota_dispatch,
                       wap_dispatch_func_t *pap_dispatch,
                       wap_dispatch_func_t *appl_dispatch);
void wap_push_ppg_shutdown(void);
void wap_push_ppg_dispatch_event(WAPEvent *e);

/*
 * Check do we have established a session with an initiator for this push.
 * Initiators are identified by their address tuple (ppg main module does not
 * know wsp sessions until told. 
 */
PPGSessionMachine *wap_push_ppg_have_push_session_for(WAPAddrTuple *tuple);

/*
 * Now iniator are identified by their session id. This function is used after
 * the session is established.
 */
PPGSessionMachine *wap_push_ppg_have_push_session_for_sid(long sid);

#endif
