/*
 * smsc.h - interface to SMS center subsystem
 *
 * Lars Wirzenius for WapIT Ltd.
 *
 * New API by Kalle Marjola 1999
 */

#ifndef SMSC_H
#define SMSC_H


#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "wapitlib.h"
#include "config.h"
#include "bb_msg.h"

/*
 * A data structure representing an SMS center. This data structure
 * is opaque: users MUST NOT use the fields directly, only the
 * smsc_* functions may do so.
 */
typedef struct SMSCenter SMSCenter;


/* Open the connection to an SMS center. 'grp' is a configgroup which determines
   the sms center. See details from sample configuration file 'bearerbox.conf'

   The operation returns NULL for error and the pointer
   to the new SMSCenter structure for OK.
   */
SMSCenter *smsc_open(ConfigGroup *grp);

/*
 * reopen once opened SMS Center connection. Close old connection if
 * exists
 *
 * return 0 on success
 * return -1 if failed
 * return -2 if failed and no use to repeat the progress (i.e. currently
 *   reopen not implemented)
 */
int smsc_reopen(SMSCenter *smsc);


/* Return the `name' of an SMC center. Name is defined here as a string that
   a humand understands that uniquely identifies the SMSC. This operation
   cannot fail. */
char *smsc_name(SMSCenter *smsc);

/* Return SMSC personal dial prefix, if any
 */
char *smsc_dial_prefix(SMSCenter *smsc);

/* Return 1 if the SMSC is router for 'number', 2 if the SMSC is
 * default router and 0 if not suitable.
 * (function checks if the prefix of the number is in route_prefix)
 */
int smsc_receiver(SMSCenter *smsc, char *number);


/* Close the connection to an SMS center. Return -1 for error
   (the connection will be closed anyway, but there was some error
   while doing so, so it wasn't closed cleanly), or 0 for OK.
   Return 0 if the smsc is NULL or smsc is already closed.
 */
int smsc_close(SMSCenter *smsc);


/* Send an SMS message via an SMS center. If the message is not an ACK/NACK,
 *  add ACK or NACK according to status intp request queue
 *
 *  Return -1 for FATAL error, 0 for OK/message ignored
 */
int smsc_send_message(SMSCenter *smsc, RQueueItem *msg, RQueue *request_queue);


/* receive a message from SMS center.
 * Return -1 if smsc fails and cannot re-connected
 * 0 if nothing new (or message creation fails) and 1 if new message,
 * which is then set to 'new', which is otherwise set as NULL
 */
int smsc_get_message(SMSCenter *smsc, RQueueItem **new);



#endif
