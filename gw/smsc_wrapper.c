/*
 * SMSC Connection wrapper
 *
 * Interface to old SMS center implementations
 *
 * Kalle Marjola 2000
 */

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"

#include "smsc.h"
#include "smsc_p.h"


int smsc_wrapper_destroy(SMSCConn *conn)
{
    /* 1. kill threads (well, sort-of)
     * 2. destroy smsc
     */

    return -1;
}


int smsc_wrapper_create(SMSCConn *conn, ConfigGroup *cfg)
{
    /* 1. Call smsc_open()
     * 2. create sender/receiver threads
     * 3. fill up the conn
     */

    conn->destroyer = smsc_wrapper_destroy;
    
    return -1;
}



