/*
 * SMSC Connection
 *
 * Interface for main bearerbox to SMS center connection modules
 *
 * Kalle Marjola 2000 for project Kannel
 */

#include <signal.h>
#include <time.h>


#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"


SMSCConn *smscconn_create(ConfigGroup *grp, List *incoming_list, 
			  List *failed_send, int start_as_stopped)
{
    SMSCConn *conn;
    char *smsc_type;

    if (incoming_list == NULL || failed_send == NULL || grp == NULL)
	return NULL;
    
    conn = gw_malloc(sizeof(SMSCConn));
    conn->incoming_queue = incoming_list;
    conn->failed_queue = failed_send;
    conn->status = SMSCCONN_UNKNOWN_STATUS;
    conn->is_stopped = start_as_stopped;
    conn->connect_time = -1;

    conn->received = counter_create();
    conn->sent = counter_create();
    conn->failed = counter_create();

    conn->outgoing_queue = list_create();
    list_add_producer(conn->outgoing_queue);

    smsc_type = config_get(grp, "smsc");

    conn->name = NULL;
    conn->id = NULL;
    /*
     * if (strcmp(smsc_type, "xxx")==0)
     *	smsc_xxx_create(conn, grp);
     * else
     */
	smsc_wrapper_create(conn, grp);
    
    return conn;
}


void smscconn_destroy(SMSCConn *conn)
{
    Msg *msg;
    
    if (conn == NULL)
	return;

    /* XXX this might need locking! */

    list_remove_producer(conn->outgoing_queue);

    /* How do we handle possible problems with dying threads? */
    
    while((msg = list_extract_first(conn->outgoing_queue)) != NULL)
	list_produce(conn->failed_queue, msg);

    list_destroy(conn->outgoing_queue, NULL);

    /* Call SMSC specific destroyer */
    conn->destroyer(conn);
    
    counter_destroy(conn->received);
    counter_destroy(conn->sent);
    counter_destroy(conn->failed);

    octstr_destroy(conn->name);
    octstr_destroy(conn->id);
    
    gw_free(conn);
    return;
}


int smscconn_stop(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    conn->is_stopped = 1;
    return 0;
}


void smscconn_start(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    conn->is_stopped = 0;
}


Octstr *smscconn_name(SMSCConn *conn)
{
    return octstr_create_immutable("N/A");
}


Octstr *smscconn_id(SMSCConn *conn)
{
    return octstr_create_immutable("N/A");
}


int smscconn_send(SMSCConn *conn, Msg *msg)
{
    Msg *own_msg;
    if (conn->status == SMSCCONN_KILLED)
	return -1;

    own_msg = msg_duplicate(msg);
    list_produce(conn->outgoing_queue, own_msg);

    return list_len(conn->outgoing_queue);
}


int smscconn_status(SMSCConn *conn)
{
    gw_assert(conn != NULL);

    return conn->status;
}


int smscconn_info(SMSCConn *conn, StatusInfo *infotable)
{
    if (conn == NULL || infotable == NULL)
	return -1;

    infotable->status = conn->status;
    infotable->is_stopped = conn->is_stopped;
    infotable->online = time(NULL) - conn->connect_time;
    
    infotable->sent = counter_value(conn->sent);
    infotable->received = counter_value(conn->received);
    infotable->failed = counter_value(conn->failed);
    infotable->queued = list_len(conn->outgoing_queue);
    
    return 0;
}


