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

typedef struct sar_info_t {
    int sar_psn;
    Octstr *sar_data;
} sar_info_t;


/*
 * Structure to keep SAR data during transmission
 */
typedef struct WTPSARData {
    int nsegm;  /* number of the last segment, i.e. total number - 1 */
    int csegm;  /* last segment confirmed by recipient */
    int lsegm;  /* last sent segment */
    int tr;		/* if current psn is gtr or ttr */
    Octstr *data;
} WTPSARData;


/* 
 * Nokia wap gw uses the size of 576, but mobiles use 1,5K size, 
 * I will think later what is better to use
 */
#define	SAR_SEGM_SIZE 576
#define	SAR_GROUP_LEN 3


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
       #define LIST(name) List *name;
       #define SARDATA(name) WTPSARData *name;
       #define MACHINE(field) field
       #include "wtp_resp_machine.def"
};

#endif
