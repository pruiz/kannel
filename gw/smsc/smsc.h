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

#include "gwlib/gwlib.h"
#include "msg.h"

/*
 * A data structure representing an SMS center. This data structure
 * is opaque: users MUST NOT use the fields directly, only the
 * smsc_* functions may do so.
 */
typedef struct SMSCenter SMSCenter;


/* Open the connection to an SMS center. 'grp' is a configgroup which
   determines the sms center. See details from sample configuration file
   'kannel.conf'

   The operation returns NULL for error and the pointer
   to the new SMSCenter structure for OK.
   */
SMSCenter *smsc_open(CfgGroup *grp);

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
   a human understands that uniquely identifies the SMSC. This operation
   cannot fail. */
char *smsc_name(SMSCenter *smsc);


/* Close the connection to an SMS center. Return -1 for error
   (the connection will be closed anyway, but there was some error
   while doing so, so it wasn't closed cleanly), or 0 for OK.
   Return 0 if the smsc is NULL or smsc is already closed.
 */
int smsc_close(SMSCenter *smsc);



#endif
