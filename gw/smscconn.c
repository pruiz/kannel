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


SMSCConn *smscconn_create(ConfigGroup *grp, int start_as_stopped)
{
    SMSCConn *conn;
    char *smsc_type;
    int ret;
    
    if (grp == NULL)
	return NULL;
    
    conn = gw_malloc(sizeof(SMSCConn));

    conn->is_killed = 0;
    conn->status = SMSCCONN_STARTING;
    conn->connect_time = -1;
    conn->load = 0;

    conn->stopped = list_create();
    conn->is_stopped = start_as_stopped;
    if (start_as_stopped)
	list_add_producer(conn->stopped);
    
    conn->received = counter_create();
    conn->sent = counter_create();
    conn->failed = counter_create();
    conn->flow_mutex = mutex_create();

    conn->name = NULL;
    conn->id = NULL;
    conn->shutdown = NULL;
    conn->send_msg = NULL;

    smsc_type = config_get(grp, "smsc");
    
    /*
     * if (strcmp(smsc_type, "xxx")==0)
     *	ret = smsc_xxx_create(conn, grp);
     * else
     */
    ret = smsc_wrapper_create(conn, grp);
    if (ret == -1) {
	smscconn_destroy(conn);
	return NULL;
    }
    gw_assert(conn->send_msg != NULL);
#if 0
    bb_smscconn_ready();
#endif
    
    return conn;
}


void smscconn_shutdown(SMSCConn *conn, int finish_sending)
{
    gw_assert(conn != NULL);
    if (conn->status == SMSCCONN_KILLED)
	return;

    /* Call SMSC specific destroyer */
    if (conn->shutdown)
	conn->shutdown(conn, finish_sending);
    
    conn->is_killed = 1;
    return;
}


int smscconn_destroy(SMSCConn *conn)
{
    if (conn == NULL)
	return 0;
    if (conn->status != SMSCCONN_KILLED)
	return -1;
    counter_destroy(conn->received);
    counter_destroy(conn->sent);
    counter_destroy(conn->failed);

    octstr_destroy(conn->name);
    octstr_destroy(conn->id);
    mutex_destroy(conn->flow_mutex);
    list_destroy(conn->stopped, NULL);
    
    gw_free(conn);
    return 0;
}


int smscconn_stop(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    if (conn->status == SMSCCONN_KILLED || conn->is_stopped || conn->is_killed)
	return -1;
    conn->is_stopped = 1;
    list_add_producer(conn->stopped);
    return 0;
}


void smscconn_start(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    if (conn->status == SMSCCONN_KILLED || conn->is_stopped == 0)
	return;
    conn->is_stopped = 0;
    list_remove_producer(conn->stopped);
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
    gw_assert(conn != NULL);
    if (conn->status == SMSCCONN_KILLED)
	return -1;
    return conn->send_msg(conn, msg);
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
/*    infotable->queued = list_len(conn->outgoing_queue); */

    infotable->load = conn->load;
    
    return 0;
}


