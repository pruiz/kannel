#ifndef BB_SMSCCONN_CB
#define BB_SMSCCONN_CB

#include "msg.h"
#include "smscconn.h"

/* Callback functions for SMSC Connection implementations.
 * All functions return immediately.
 *
 * NOTE: These callback functions MUST be called by SMSCConn
 *   implementations in given times! See smscconn_p.h for details
 */


/* called immediately after startup is done. This is called
 * AUTOMATICALLY by smscconn_create, no need to call it from
 * various implementations */
void bb_smscconn_ready(SMSCConn *conn);


/* called after SMSCConn is shutdown or it kills itself
 * because of non-recoverable problems. SMSC Connection has already
 * destroyed all its private data areas and set status as KILLED.
 * Note that after this has been called the bearerbox can call final
 * destroy for the connection
 */
void bb_smscconn_killed(void);


/* called after successful sending of Msg 'sms'. This callbacks takes
 * care of 'sms' and MAY NOT be used by caller again. */
void bb_smscconn_sent(SMSCConn *conn, Msg *sms);


/* called after failed sending of 'sms'. Reason is set accordingly.
 * callback handles 'sms' and MAY NOT be used by caller again */
void bb_smscconn_send_failed(SMSCConn *conn, Msg *sms, int reason);

enum {
    SMSCCONN_FAILED_SHUTDOWN,
    SMSCCONN_FAILED_REJECTED,
    SMSCCONN_FAILED_MALFORMED
};


/* called when a new message 'sms' received. Callback handles
 * 'sms' and MAY NOT be used by caller again. Return 0 if all went
 * fine, and -1 if bearerbox does NOT accept the 'sms', in which case the
 * caller MUST take care of it */
int bb_smscconn_receive(SMSCConn *conn, Msg *sms);


#endif
