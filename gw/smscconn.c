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
#include "bb_smscconn_cb.h"


SMSCConn *smscconn_create(ConfigGroup *grp, int start_as_stopped)
{
    SMSCConn *conn;
    char *smsc_type, *p;
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

    conn->shutdown = NULL;
    conn->queued = NULL;
    conn->send_msg = NULL;

#define GET_OPTIONAL_VAL(x, n) x = (p = config_get(grp,n)) ? octstr_create(p) : NULL
    
    GET_OPTIONAL_VAL(conn->id, "smsc-id");
    GET_OPTIONAL_VAL(conn->denied_smsc_id, "denied-smsc-id");
    GET_OPTIONAL_VAL(conn->preferred_smsc_id, "preferred-smsc-id");
    GET_OPTIONAL_VAL(conn->denied_prefix, "denied-prefix");
    GET_OPTIONAL_VAL(conn->preferred_prefix, "preferred-prefix");
    
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

    bb_smscconn_ready(conn);
    
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
    octstr_destroy(conn->denied_smsc_id);
    octstr_destroy(conn->preferred_smsc_id);
    octstr_destroy(conn->denied_prefix);
    octstr_destroy(conn->preferred_prefix);
    
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
    return conn->name;
}


Octstr *smscconn_id(SMSCConn *conn)
{
    return conn->id;
}


static int does_prefix_match(Octstr *prefix, Octstr *number)
{
    /* XXX modify to use just octstr operations
     */
    char *b, *p, *n;

    gw_assert(prefix != NULL);
    gw_assert(number != NULL);

    p = octstr_get_cstr(prefix);
    n = octstr_get_cstr(number);
    

    while (*p != '\0') {
        b = n;
        for (b = n; *b != '\0'; b++, p++) {
            if (*p == ';' || *p == '\0') {
                return 1;
            }
            if (*p != *b) break;
        }
        while (*p != '\0' && *p != ';')
            p++;
        while (*p == ';') p++;
    }
    return 0;
}


int smscconn_usable(SMSCConn *conn, Msg *msg)
{
    List *list;

    gw_assert(conn != NULL);
    if (conn->status == SMSCCONN_KILLED || conn->is_killed)
	return -1;

    /* first check if this SMSC is denied altogether */
    
    if (conn->denied_smsc_id && msg->sms.smsc_id != NULL) {
        list = octstr_split(conn->denied_smsc_id, octstr_create_immutable(";"));
        if (list_search(list, msg->sms.smsc_id, octstr_item_match) != NULL) {
	    list_destroy(list, octstr_destroy_item);
	    return -1;
	}
	list_destroy(list, octstr_destroy_item);
    }
    if (conn->denied_prefix)
	if (does_prefix_match(conn->denied_prefix, msg->sms.receiver) == 1)
	    return -1;

    /* then see if it is preferred one */


    if (conn->preferred_smsc_id && msg->sms.smsc_id != NULL) {
        list = octstr_split(conn->preferred_smsc_id, octstr_create_immutable(";"));
        if (list_search(list, msg->sms.smsc_id, octstr_item_match) != NULL) {
	    list_destroy(list, octstr_destroy_item);
	    return 1;
	}
	list_destroy(list, octstr_destroy_item);
    }
    if (conn->preferred_prefix)
	if (does_prefix_match(conn->preferred_prefix, msg->sms.receiver) == 1)
	    return 1;

    return 0;
}


int smscconn_send(SMSCConn *conn, Msg *msg)
{
    gw_assert(conn != NULL);
    if (conn->status == SMSCCONN_KILLED || conn->is_killed)
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

    if (conn->queued)
	infotable->queued = conn->queued(conn);
    else
	infotable->queued = -1;

    infotable->load = conn->load;
    
    return 0;
}


