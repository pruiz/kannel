#ifndef SMSCCONN_P_H
#define SMSCCONN_P_H

/* SMSC Connection private header
 *
 * Defines internal private structure
 *
 * Kalle Marjola 2000 for project Kannel
 *

 ADDING AND WORKING OF NEW SMS CENTER CONNECTIONS:

 These are guidelines and rules for adding new SMSC Connections to
 Kannel. See file bb_smscconn.h for callback function prototypes.

 New SMSC Connection handler is free-formed module which only have following
 rules:
 
 1) Each new SMSC Connection MUST implement function
    smsc_xxx_create(SMSCConn *conn, ConfigGrp *cfg), which:

    a) SHOULD NOT block   (XXX)
    b) MUST warn about any configuration group variables it does
       not support    (XXX)
    c) MUST set up send_msg dynamic function to handle messages
       to-be-sent. This function MAY NOT block.
    d) CAN set up private shutdown function, which MAY NOT block
       
 2) Each SMSC Connection MUST call certain BB callback functions when
    certain things occur:

    a) Each SMSC Connection MUST call callback function
       bb_smscconn_killed when it dies because it was put down earlier
       with bb_smscconn_shutdown or it simply cannot keep the connection
       up (wrong password etc.) Reason must be supplied. When killed,
       SMSC Connection MUST release all memory it has taken EXCEPT for
       the basic SMSCConn struct, which is laterwards released by the
       bearerbox. 

    b) When SMSC Connection receives a message from SMSC, it must
       create a new Msg from it and call bb_smscconn_received

    c) When SMSC Connection has sent a message to SMSC, it MUST call
       callback function bb_smscconn_sent

    d) When SMSC Connection has failed to send a message to SMSC, it
       MUST call callback function bb_smscconn_send_failed with appropriate
       reason

 3) SMSC Connection MUST fill up SMSCConn structure as needed to, and is
    responsible for any concurrency timings. SMSCConn->status MAY NOT be
    set to KILLED until the connection is really that. Use is_killed to
    make internally dead.

 4) When SMSC Connection shuts down, it MUST call bb_smscconn_send_failed
    for each message not yet sent, and must destroy all memory it has used
    (SMSCConn struct MUST be left, but data is destroyed). Before data is
    being destroyed, the SMSC Connection must set SMSCConn->is_killed to
    non-zero. After everything is destroyed, it MUST set status to
    SMSCCONN_KILLED and call callback bb_smscconn_killed with correct reason.

 5) Callback bb_smscconn_ready is automatically called by main
    smscconn_create. New implementation MAY NOT call it directly
*/
 
#include <signal.h>
#include "gwlib/gwlib.h"

struct smscconn {
    int		 status;	/* see smscconn.h */
    int 	load;	       	/* load factor, 0 = no load */
    int		is_killed;	/* time to die */
    time_t 	connect_time;	/* When connection to SMSC was established */

    Mutex 	*flow_mutex;	/* used for thread synchronization */
    List	*stopped;	/* list-trick for suspend/isolate */ 
    int 	is_stopped;

    /* connection specific counters */
    Counter *received;
    Counter *sent;
    Counter *failed;

    Octstr *name;		/* Descriptive name filled from connection info */
    Octstr *id;			/* Abstract name spesified in configuration and
				   used for logging and routing */
    
    /* XXX: move global data from Smsc here, unless it is
     *      moved straight to bearerbox (routing information)
     */

    /* pointer to function called when smscconn_shutdown called.
       Note that this function is not needed always. */
    int (*shutdown) (SMSCConn *conn);

    /* pointer to function called when a new message is needed to be sent.
       MAY NOT block */
    int (*send_msg) (SMSCConn *conn, Msg *msg);
    
    void *data;			/* SMSC specific stuff */

};

/*
 * Initializers for various SMSC connection implementations,
 * each should take same arguments and return an int,
 * which is 0 for okay and -1 for error.
 *
 * Each function is responsible for setting up all dynamic
 * function pointers at SMSCConn structure and starting up any
 * threads it might need.
 */

/* generic wrapper for old SMSC implementations (uses old smsc.h).
 * Responsible file: smsc_wrapper.c */
int smsc_wrapper_create(SMSCConn *conn, ConfigGroup *cfg);



#endif
