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

/*
 * Some defaults
 */
#define SMSCCONN_RECONNECT_DELAY     10.0 


/*
 * Add reroute information to the connection data. Where the priority
 * is in the order: reroute, reroute-smsc-id, reroute-receiver.
 */
static void init_reroute(SMSCConn **conn, CfgGroup *grp)
{
    Octstr *rule = NULL;
    unsigned int i, j;

    if (cfg_get_bool(&(*conn)->reroute, grp, octstr_imm("reroute")) != -1) {
        debug("smscconn",0,"Adding general internal routing for smsc id <%s>",
              octstr_get_cstr((*conn)->id));
        return;
    }

    if (((*conn)->reroute_to_smsc 
         = cfg_get(grp, octstr_imm("reroute-smsc-id"))) != NULL) {

         /* reroute all messages to a specific smsc-id */
         debug("smscconn",0,"Adding internal routing: smsc id <%s> to smsc id <%s>",
               octstr_get_cstr((*conn)->id), octstr_get_cstr((*conn)->reroute_to_smsc));
        return;
    }

    if ((rule = cfg_get(grp, octstr_imm("reroute-receiver"))) != NULL) {
        List *routes = NULL;

        /* create hash disctionary for this smsc-id */
        (*conn)->reroute_by_receiver = dict_create(10, (void(*)(void *)) octstr_destroy);
        
        routes = octstr_split(rule, octstr_imm(";"));
        for (i = 0; i < list_len(routes); i++) {
            Octstr *item = list_get(routes, i);
            Octstr *smsc = NULL;
            List *receivers = NULL;
    
            /* first word is the smsc-id, all other are the receivers */
            receivers = octstr_split(item, octstr_imm(","));
            smsc = list_len(receivers) > 0 ? 
                octstr_duplicate(list_get(receivers, 0)) : NULL;
            if (smsc)
                octstr_strip_blanks(smsc);

            for (j = 0; j < list_len(receivers); j++) {
                Octstr *n = list_get(receivers, j);

                if (j != 0) {
                    Octstr *r = octstr_duplicate(n);
                    octstr_strip_blanks(r);
                    debug("smscconn",0,"Adding internal routing for smsc id <%s>: "
                          "receiver <%s> to smsc id <%s>",
                          octstr_get_cstr((*conn)->id), octstr_get_cstr(r), 
                          octstr_get_cstr(smsc));

                    dict_put((*conn)->reroute_by_receiver, r, octstr_duplicate(smsc));
                    octstr_destroy(r);
                }
            }
            octstr_destroy(smsc);
            list_destroy(receivers, octstr_destroy_item);
        }
        octstr_destroy(rule);
        list_destroy(routes, octstr_destroy_item);
    }
}


SMSCConn *smscconn_create(CfgGroup *grp, int start_as_stopped)
{
    SMSCConn *conn;
    Octstr *smsc_type;
    int ret;
    long throughput;

    if (grp == NULL)
	return NULL;

    conn = gw_malloc(sizeof(SMSCConn));

    conn->why_killed = SMSCCONN_ALIVE;
    conn->status = SMSCCONN_CONNECTING;
    conn->connect_time = -1;
    conn->load = 0;
    conn->is_stopped = start_as_stopped;
    conn->received = counter_create();
    conn->sent = counter_create();
    conn->failed = counter_create();
    conn->flow_mutex = mutex_create();
    conn->name = NULL;
    conn->shutdown = NULL;
    conn->queued = NULL;
    conn->send_msg = NULL;
    conn->stop_conn = NULL;
    conn->start_conn = NULL;
    conn->log_idx = 0;
    conn->reroute = 0;
    conn->reroute_to_smsc = NULL;
    conn->reroute_by_receiver = NULL;

#define GET_OPTIONAL_VAL(x, n) x = cfg_get(grp, octstr_imm(n))
    
    GET_OPTIONAL_VAL(conn->id, "smsc-id");
    GET_OPTIONAL_VAL(conn->allowed_smsc_id, "allowed-smsc-id");
    GET_OPTIONAL_VAL(conn->denied_smsc_id, "denied-smsc-id");
    GET_OPTIONAL_VAL(conn->preferred_smsc_id, "preferred-smsc-id");
    GET_OPTIONAL_VAL(conn->allowed_prefix, "allowed-prefix");
    GET_OPTIONAL_VAL(conn->denied_prefix, "denied-prefix");
    GET_OPTIONAL_VAL(conn->preferred_prefix, "preferred-prefix");
    GET_OPTIONAL_VAL(conn->unified_prefix, "unified-prefix");
    GET_OPTIONAL_VAL(conn->our_host, "our-host");
    GET_OPTIONAL_VAL(conn->log_file, "log-file");
    cfg_get_bool(&conn->alt_dcs, grp, octstr_imm("alt-dcs"));
             
    if (cfg_get_integer(&throughput, grp, octstr_imm("throughput")) == -1)
        conn->throughput = 0;   /* defaults to no throughtput limitation */
    else 
        conn->throughput = (int) throughput;

    /* configure the internal rerouting rules for this smsc id */
    init_reroute(&conn, grp);

    if (cfg_get_integer(&conn->log_level, grp, octstr_imm("log-level")) == -1)
        conn->log_level = 0;
   
    /* open a smsc-id specific log-file in exlusive mode */
    if (conn->log_file)
        conn->log_idx = log_open(octstr_get_cstr(conn->log_file), 
                                 conn->log_level, GW_EXCL); 

    if (conn->allowed_smsc_id && conn->denied_smsc_id)
	warning(0, "Both 'allowed-smsc-id' and 'denied-smsc-id' set, deny-list "
		"automatically ignored");

    if (cfg_get_integer(&conn->reconnect_delay, grp, 
                        octstr_imm("reconnect-delay")) == -1)
        conn->reconnect_delay = SMSCCONN_RECONNECT_DELAY;
    
    smsc_type = cfg_get(grp, octstr_imm("smsc"));
    if (smsc_type == NULL) {
        error(0, "Required field 'smsc' missing for smsc group.");
        smscconn_destroy(conn);
        octstr_destroy(smsc_type);
        return NULL;
    }

    if (octstr_compare(smsc_type, octstr_imm("fake")) == 0)
        ret = smsc_fake_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("cimd2")) == 0)
	ret = smsc_cimd2_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("emi")) == 0)
	ret = smsc_emi2_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("http")) == 0)
        ret = smsc_http_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("smpp")) == 0)
	ret = smsc_smpp_create(conn, grp);
    else if (octstr_compare(smsc_type, octstr_imm("at")) == 0)
	ret = smsc_at2_create(conn,grp);
    else if (octstr_compare(smsc_type, octstr_imm("cgw")) == 0)
        ret = smsc_cgw_create(conn,grp);
    else if (octstr_compare(smsc_type, octstr_imm("smasi")) == 0)
        ret = smsc_smasi_create(conn, grp);
    else
        ret = smsc_wrapper_create(conn, grp);

    octstr_destroy(smsc_type);
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
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD) {
	mutex_unlock(conn->flow_mutex);
	return;
    }

    /* Call SMSC specific destroyer */
    if (conn->shutdown)
	conn->shutdown(conn, finish_sending);
    else
	conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    mutex_unlock(conn->flow_mutex);
    return;
}


int smscconn_destroy(SMSCConn *conn)
{
    if (conn == NULL)
	return 0;
    if (conn->status != SMSCCONN_DEAD)
	return -1;
    mutex_lock(conn->flow_mutex);

    counter_destroy(conn->received);
    counter_destroy(conn->sent);
    counter_destroy(conn->failed);

    octstr_destroy(conn->name);
    octstr_destroy(conn->id);
    octstr_destroy(conn->allowed_smsc_id);
    octstr_destroy(conn->denied_smsc_id);
    octstr_destroy(conn->preferred_smsc_id);
    octstr_destroy(conn->denied_prefix);
    octstr_destroy(conn->allowed_prefix);
    octstr_destroy(conn->preferred_prefix);
    octstr_destroy(conn->unified_prefix);
    octstr_destroy(conn->our_host);
    octstr_destroy(conn->log_file);

    octstr_destroy(conn->reroute_to_smsc);
    dict_destroy(conn->reroute_by_receiver);
    
    mutex_unlock(conn->flow_mutex);
    mutex_destroy(conn->flow_mutex);
    
    gw_free(conn);
    return 0;
}


int smscconn_stop(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD || conn->is_stopped != 0
	|| conn->why_killed != SMSCCONN_ALIVE)
    {
	mutex_unlock(conn->flow_mutex);
	return -1;
    }
    conn->is_stopped = 1;

    if (conn->stop_conn)
	conn->stop_conn(conn);
    
    mutex_unlock(conn->flow_mutex);
    return 0;
}


void smscconn_start(SMSCConn *conn)
{
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD || conn->is_stopped == 0) {
	mutex_unlock(conn->flow_mutex);
	return;
    }
    conn->is_stopped = 0;

    if (conn->start_conn)
	conn->start_conn(conn);
    mutex_unlock(conn->flow_mutex);
}


Octstr *smscconn_name(SMSCConn *conn)
{
    return conn->name;
}


Octstr *smscconn_id(SMSCConn *conn)
{
    return conn->id;
}


int smscconn_usable(SMSCConn *conn, Msg *msg)
{
    List *list;

    gw_assert(conn != NULL);
    if (conn->status == SMSCCONN_DEAD || conn->why_killed != SMSCCONN_ALIVE)
	return -1;

    /* if allowed-smsc-id set, then only allow this SMSC if message
     * smsc-id matches any of its allowed SMSCes
     */
    if (conn->allowed_smsc_id) {
	if (msg->sms.smsc_id == NULL)
	    return -1;

        list = octstr_split(conn->allowed_smsc_id, octstr_imm(";"));
        if (list_search(list, msg->sms.smsc_id, octstr_item_match) == NULL) {
	    list_destroy(list, octstr_destroy_item);
	    return -1;
	}
	list_destroy(list, octstr_destroy_item);
    }
    /* ..if no allowed-smsc-id set but denied-smsc-id and message smsc-id
     * is set, deny message if smsc-ids match */
    else if (conn->denied_smsc_id && msg->sms.smsc_id != NULL) {
        list = octstr_split(conn->denied_smsc_id, octstr_imm(";"));
        if (list_search(list, msg->sms.smsc_id, octstr_item_match) != NULL) {
	    list_destroy(list, octstr_destroy_item);
	    return -1;
	}
	list_destroy(list, octstr_destroy_item);
    }

    /* Have allowed */
    if (conn->allowed_prefix && ! conn->denied_prefix && 
       (does_prefix_match(conn->allowed_prefix, msg->sms.receiver) != 1))
	return -1;

    /* Have denied */
    if (conn->denied_prefix && ! conn->allowed_prefix &&
       (does_prefix_match(conn->denied_prefix, msg->sms.receiver) == 1))
	return -1;

    /* Have allowed and denied */
    if (conn->denied_prefix && conn->allowed_prefix &&
       (does_prefix_match(conn->allowed_prefix, msg->sms.receiver) != 1) &&
       (does_prefix_match(conn->denied_prefix, msg->sms.receiver) == 1) )
	return -1;

    /* then see if it is preferred one */

    if (conn->preferred_smsc_id && msg->sms.smsc_id != NULL) {
        list = octstr_split(conn->preferred_smsc_id, octstr_imm(";"));
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
    int ret;
    char *uf;
    
    gw_assert(conn != NULL);
    mutex_lock(conn->flow_mutex);
    if (conn->status == SMSCCONN_DEAD || conn->why_killed != SMSCCONN_ALIVE) {
        mutex_unlock(conn->flow_mutex);
        return -1;
    }

    /* normalize the destination number for this smsc */
    uf = conn->unified_prefix ? octstr_get_cstr(conn->unified_prefix) : NULL;
    normalize_number(uf, &(msg->sms.receiver));

    ret = conn->send_msg(conn, msg);
	mutex_unlock(conn->flow_mutex);
    return ret;
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

    mutex_lock(conn->flow_mutex);
    
    infotable->status = conn->status;
    infotable->killed = conn->why_killed;
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
    
    mutex_unlock(conn->flow_mutex);

    return 0;
}


