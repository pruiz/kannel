#ifndef SMSCCONN_H
#define SMSCCONN_H

/*
 * SMSC Connection
 *
 * Interface for main bearerbox to SMS center connection modules
 *
 * At first stage a simple wrapper for old ugly smsc module
 *
 * Kalle Marjola 2000
 */

#include "gwlib/gwlib.h"
#include "gw/msg.h"

/*
 * Structure hierarchy:
 *
 * bearerbox has its own smsc struct, in which it helds information
 * about routing information, and then pointer to smscc
 *
 * smscc is internal structure for smscc module. It has a list
 * of common variables like number of sent/received messages and
 * and pointers to appropriate lists, and then it has a void pointer
 * to appropriate smsc structure defined and used by corresponding smsc
 * connection type module (like CIMD2, SMPP etc al)
 */

typedef struct smscconn SMSCConn;

/* create new SMS center connection from given configuration group,
 * or return NULL if failed 
 *
 * 'incoming_list' is a list in which the SMSC adds any received
 * SMS messages
 *
 * 'failed_send' is a list in bearerbox, in which the SMSC adds
 *  messages it completely fails to send
 *
 * NOTE: this function starts one or more threads to
 *   handle traffic with SMSC, and caller does not need to
 *   care about it afterwards.
 */
SMSCConn *smscconn_create(ConfigGroup *cfg, List *incoming_list, 
			  List *failed_send, int start_as_stopped);

/* destroy smscc. Put all messages in internal outgoing queue
 * into failed_send list, close connections etc.
 */
void smscconn_destroy(SMSCConn *smscconn);

/* stop smscc. A stopped smscc does not receive any messages, but can
 * still send messages, so that internal queue can be emptied. The caller
 * is responsible for not to add new messages into queue if the caller wants
 * the list to empty at some point
 */
int smscconn_stop(SMSCConn *smscconn);

/* start stopped smscc. Return -1 if failed, 0 otherwise */
void smscconn_start(SMSCConn *smscconn);

/* Return name of the SMSC. The caller must destroy the octstr */
Octstr *smscconn_name(SMSCConn *smscconn);

/* Append a copy of message 'msg' to smscc internal queue
 * Return number of messages in outgoing queue, or -1 if operation
 * failed. In any case, caller must destroy msg.
 * XXX possible future: when the message is successfully sent, add
 *     acknowledgement to incoming_list
 */
int smscconn_send(SMSCConn *smsccconn, Msg *msg);


/* Return just status as defined below */
int smscconn_status(SMSCConn *smscconn);

typedef struct smsc_state {
    int	status;		/* see enumeration, below */
    int is_stopped;	/* is connection currently in stopped state? */
    long received;	/* total number */
    long sent;		/* total number */
    long failed;	/* total number */
    long queued;	/* set our internal outgoing queue length */
    long online;	/* in seconds */
} StatusInfo;


enum {
    SMSCCONN_UNKNOWN_STATUS = -1,
    SMSCCONN_ACTIVE = 0,
    SMSCCONN_CONNECTING = 1,
    SMSCCONN_RECONNECTING = 2,
    SMSCCONN_DISCONNECTED = 3,
    SMSCCONN_KILLED = 4
};

/* return current status of the SMSC connection, filled to infotable.
 * For unknown numbers, put -1. Return -1 if either argument was NULL.
 */
int smscconn_info(SMSCConn *smscconn, StatusInfo *infotable);



#endif
