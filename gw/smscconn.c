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
#include "smscconn.h"

#include "smsc.h"
#include "smsc_p.h"  	/* do we need this? */


struct smscconn {
    List *incoming_queue;
    int status;
    int is_stopped;
    time_t connect_time;	/* When connection to SMSC was established */

    Counter *received;
    Counter *sent;
    Counter *failed;
    List *outgoing_queue;

    /* XXX: move global data from Smsc here, unless it is
     *      moved straight to bearerbox (routing information)
     */
    SMSCenter *smsc_data;	/* until they all converted */
};


SMSCConn *smscconn_create(ConfigGroup *cfg, List *incoming_list, 
			  List *failed_send, int start_as_stopped)
{
    return NULL;
}


void smscconn_destroy(SMSCConn *smscconn)
{
    if (smscconn == NULL)
	return;

    return;
}


int smscconn_stop(SMSCConn *smscconn)
{
    gw_assert(smscconn != NULL);
    smscconn->is_stopped = 1;
    return 0;
}


void smscconn_start(SMSCConn *smscconn)
{
    gw_assert(smscconn != NULL);
    smscconn->is_stopped = 0;
}


Octstr *smscconn_name(SMSCConn *smscconn)
{
    return octstr_create_immutable("N/A");
}


int smscconn_send(SMSCConn *smscconn, Msg *msg)
{
    return -1;
}


int smscconn_status(SMSCConn *smscconn)
{
    gw_assert(smscconn != NULL);

    return smscconn->status;
}


int smscconn_info(SMSCConn *smscconn, StatusInfo *infotable)
{
    if (smscconn == NULL || infotable == NULL)
	return -1;

    infotable->status = smscconn->status;
    infotable->is_stopped = smscconn->is_stopped;
    infotable->online = time(NULL) - smscconn->connect_time;
    
    infotable->sent = counter_value(smscconn->sent);
    infotable->received = counter_value(smscconn->received);
    infotable->failed = counter_value(smscconn->failed);
    infotable->queued = list_len(smscconn->outgoing_queue);
    
    return 0;
}


