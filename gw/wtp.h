/*
 * wtp.h - WTP implementation header
 */

#ifndef WTP_H
#define WTP_H

typedef struct WTPMachine WTPMachine;
typedef struct Address Address;
typedef struct WTPSegment WTPSegment;
typedef struct Tid_cache Tid_cache;

#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "wsp.h"
#include "wap-events.h"
#include "wtp_timer.h"
#include "wtp_send.h"
#include "wtp_tid.h"

/*
 * For removing the magic
 */
#define NUMBER_OF_ABORT_TYPES 2
#define NUMBER_OF_ABORT_REASONS 9
#define NUMBER_OF_TRANSACTION_CLASSES 3
/*
 * For now, timers are defined. They will depend on bearer information fetched
 * from address (or from a header field of the protocol speaking with the
 * bearerbox).
 */

#define L_A_WITH_USER_ACK 4
#define L_R_WITH_USER_ACK 7

/*
 * Maximum values for counters (for retransmissions and acknowledgement waiting 
 * periods)
 */
#define AEC_MAX 6
#define MAX_RCR  8

/*
 * Types of WTP PDUs and numbers assigned for them
 */

enum {
     ERRONEOUS = -0x01,
     NOT_ALLOWED = 0x00,
     INVOKE = 0x01,
     RESULT = 0x02,
     ACK = 0x03,
     ABORT = 0x04,
     SEGMENTED_INVOKE = 0x05,
     SEGMENTED_RESULT = 0x06,
     NEGATIVE_ACK = 0x07
};

/*
 * Types of acknowledgement PDU (normal acknowledgement or tid verification)
 */

enum {
   ACKNOWLEDGEMENT = 0,
   TID_VERIFICATION = 1
};

/*
 * Who is aborting (WTP or WTP user)
 */
enum {
     PROVIDER = 0x00,
     USER = 0x01
};

/*
 * See file wtp_state-decl.h for comments. Note that in this case macro ROW is 
 * defined to produce an empty string.
 */
enum states {
    #define STATE_NAME(state) state,
    #define ROW(state, event, condition, action, next_state)
    #include "wtp_state-decl.h"
    states_count
};

typedef enum states states;

/*
 * See file wtp_machine-decl.h for comments. We define one macro for every type.
 */
struct WTPMachine {
        #define INTEGER(name) long name
        #define ENUM(name) states name
        #define MSG(name) Msg *name
        #define OCTSTR(name) Octstr *name
        #define QUEUE(name) WTPEvent *name
        #define WSP_EVENT(name) WAPEvent *name
	#define TIMER(name) WTPTimer *name
        #define MUTEX(name) Mutex *name
        #define NEXT(name) struct WTPMachine *name
        #define MACHINE(field) field
	#define LIST(name) List *name
        #include "wtp_machine-decl.h"
};

/*
 * A separate data structure for storing an address four-tuple of a message.
 */
struct Address {
   Octstr *source_address;
   long source_port;
   Octstr *destination_address;
   long destination_port;
};

/*
 * An ordered linked list for storing received segments.
 */
struct WTPSegment {
   long tid;
   char packet_sequence_number;
   Octstr *data;
   struct WTPSegment *next;
};

/*
 * Initialize the WTP subsystem. MUST be called before any other calls
 * to this module.
 */
void wtp_init(void);

/*
 * Shut down the WTP subsystem. MUST be called after the subsystem isn't
 * used anymore.
 */
void wtp_shutdown(void);

/*
 * Parse a `wdp_datagram' message object (of type Msg, see msg.h) and
 * create a corresponding WTPEvent object. Also check that the datagram
 * is syntactically valid. If there is a problem (memory allocation or
 * invalid packet), then return NULL, and send an appropriate error
 * packet to the phone. Otherwise return a pointer to the event structure
 * that has been created.
 */
WAPEvent *wtp_unpack_wdp_datagram(Msg *msg);


/*
 * Creates wtp machine having addsress quintuple and transaction class 
 * iniatilised. If machines list is busy, just waits.
 */ 
WTPMachine *wtp_machine_create(Octstr *srcaddr, long srcport,
				Octstr *destaddr, long destport, long tid,
				long tcl);

/*
 * Checks whether wtp machines data structure includes a spesific machine.
 * The machine in question is identified with with source and destination
 * address and port and tid. Address information is fetched from message
 * fields, tid from an field of the event. If the machine does not exist and
 * the event is RcvInvoke, a new machine is created and added in the machines
 * data structure. If the event was RcvAck or RcvAbort, the event is ignored.
 * If the event is RcvErrorPDU, new machine is created.
 */
WTPMachine *wtp_machine_find_or_create(Msg *msg, WAPEvent *event);


/*
 * Mark a WTP state machine unused. Normally, removing a state machine from the state 
 * machines list means marking turning off a flag.  If machines list is busy, just wait.
 */
void wtp_machine_mark_unused(WTPMachine *machine);


/* 
 * Removes from the machines list all machines having in_use-flag cleared. Panics 
 * if machines list is empty. If machines list is busy, does nothing (garbage 
 * collection will eventually start again).
 */
void wtp_machines_list_clear(void);


/*
 * Output the state of the machine and all its fields.
 */
void wtp_machine_dump(WTPMachine  *machine);


/*
 * Feed an event to a WTP state machine. Handle all errors by itself, do not 
 * report them to the caller. Generate a pointer to WSP event, if an indication 
 * or a confirmation is required.
 */
void wtp_handle_event(WTPMachine *machine, WAPEvent *event);

/*
 * Generates a new transaction handle by incrementing the previous one by one.
 */
unsigned long wtp_tid_next(void);

#endif
