#ifndef SMSCCONN_P_H
#define SMSCCONN_P_H


#include <signal.h>
#include "gwlib/gwlib.h"

struct smscconn {
    List *incoming_queue;	/* reference to list in which to put new msgs */
    List *failed_queue;		/* reference to list in which to put failed msgs */
    sig_atomic_t status;
    int is_stopped;
    time_t connect_time;	/* When connection to SMSC was established */

    Counter *received;
    Counter *sent;
    Counter *failed;
    List *outgoing_queue;	/* our own private list */

    Octstr *name;		/* Descriptive name filled from connection info */
    Octstr *id;			/* Abstract name spesified in configuration and
				   used for logging and routing */
    
    /* XXX: move global data from Smsc here, unless it is
     *      moved straight to bearerbox (routing information)
     */

    int (*destroyer) (SMSCConn *conn);  /* pointer to function which destroys
					   SMSC specific data */
    
    void *data;			/* SMSC specific stuff */

    /* the big question: should we use function pointers to
     * functions like 'destroy', 'receive_message', 'send_message'?
     */
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
