/*
 * SMSC Connection interface for Bearerbox.
 *
 * Includes callback functions called by SMSCConn implementations
 *
 * Handles all startup/shutdown adminstrative work in bearerbox, plus
 * routing, writing actual access logs, handling failed messages etc.
 *
 * Kalle Marjola 2000 for project Kannel
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>


#include "gwlib/gwlib.h"
#include "msg.h"
#include "sms.h"
#include "bearerbox.h"
#include "numhash.h"
#include "smscconn.h"

#include "bb_smscconn_cb.h"    /* callback functions for connections */
#include "smscconn_p.h"        /* to access counters */

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern List *incoming_wdp;
extern List *incoming_sms;
extern List *outgoing_sms;

extern Counter *incoming_sms_counter;
extern Counter *outgoing_sms_counter;

extern List *flow_threads;
extern List *suspended;
extern List *isolated;

/* our own thingies */

static volatile sig_atomic_t smsc_running;
static List *smsc_list;
static List *smsc_groups;
static Octstr *unified_prefix;

static Numhash *black_list;
static Numhash *white_list;

static long maximum_queue_length;

static long router_thread = -1;

static void log_sms(SMSCConn *conn, Msg *sms, char *message)
{
    Octstr *text, *udh;
    
    text = sms->sms.msgdata ? octstr_duplicate(sms->sms.msgdata) : octstr_create("");
    udh = sms->sms.udhdata ? octstr_duplicate(sms->sms.udhdata) : octstr_create("");
    if ((sms->sms.coding == DC_8BIT || sms->sms.coding == DC_UCS2))
	octstr_binary_to_hex(text, 1);
    octstr_binary_to_hex(udh, 1);

    alog("%s [SMSC:%s] [SVC:%s] [ACT:%s] [from:%s] [to:%s] [flags:%d:%d:%d:%d:%d] [msg:%d:%s]"
	" [udh:%d:%s]",
	 message,
	 conn ? (smscconn_id(conn) ? octstr_get_cstr(smscconn_id(conn)) : "")
	 : "",
	 sms->sms.service ? octstr_get_cstr(sms->sms.service) : "",
	 sms->sms.account ? octstr_get_cstr(sms->sms.account) : "",
	 sms->sms.sender ? octstr_get_cstr(sms->sms.sender) : "",
	 sms->sms.receiver ? octstr_get_cstr(sms->sms.receiver) : "",
	 sms->sms.mclass, sms->sms.coding, sms->sms.mwi, sms->sms.compress,
	 sms->sms.dlr_mask,
	 octstr_len(sms->sms.msgdata), octstr_get_cstr(text),
	 octstr_len(sms->sms.udhdata), octstr_get_cstr(udh)
    );
    octstr_destroy(udh);
    octstr_destroy(text);
}

/*---------------------------------------------------------------------------
 * CALLBACK FUNCTIONS
 *
 * called by SMSCConn implementations when appropriate
 */

void bb_smscconn_ready(SMSCConn *conn)
{
    list_add_producer(flow_threads);
    list_add_producer(incoming_sms);
}


void bb_smscconn_connected(SMSCConn *conn)
{
    if (router_thread >= 0)
	gwthread_wakeup(router_thread);
}


void bb_smscconn_killed(void)
{
    /* NOTE: after status has been set to SMSCCONN_DEAD, bearerbox
     *   is free to release/delete 'conn'
     */
    list_remove_producer(incoming_sms);
    list_remove_producer(flow_threads);
}


void bb_smscconn_sent(SMSCConn *conn, Msg *sms)
{
    Msg *mack;
    
    counter_increase(outgoing_sms_counter);
    if (conn) counter_increase(conn->sent);
    
    /* write ACK to store file */

    mack = msg_create(ack);
    mack->ack.nack = 0;
    mack->ack.time = sms->sms.time;
    mack->ack.id = sms->sms.id;
    
    (void) store_save(mack);
    msg_destroy(mack);
    
    /* XXX relay confirmancy message should be generated here */
    
    log_sms(conn, sms, "Sent SMS");
    msg_destroy(sms);
}


void bb_smscconn_send_failed(SMSCConn *conn, Msg *sms, int reason)
{
    Msg *mnack;
    
    switch (reason) {

    case SMSCCONN_FAILED_SHUTDOWN:
    case SMSCCONN_FAILED_TEMPORARILY:
	list_produce(outgoing_sms, sms);
	break;
    default:

	/* write NACK to store file */

	mnack = msg_create(ack);
	mnack->ack.nack = 1;
	mnack->ack.time = sms->sms.time;
	mnack->ack.id = sms->sms.id;

	(void) store_save(mnack);
	msg_destroy(mnack);
    
	if (conn) counter_increase(conn->failed);
	if (reason == SMSCCONN_FAILED_DISCARDED)
	    log_sms(conn, sms, "DISCARDED SMS");
	else
	    log_sms(conn, sms, "FAILED Send SMS");
	msg_destroy(sms);
    } 
}    

int bb_smscconn_receive(SMSCConn *conn, Msg *sms)
{
    char *uf;

    /* do some queue control */
    if (maximum_queue_length != -1 && bb_status == BB_FULL && 
            list_len(incoming_sms) <= maximum_queue_length) {
        bb_status = BB_RUNNING;
        warning(0, "started to accept messages again");
    }
    
    if (maximum_queue_length != -1 && 
            list_len(incoming_sms) > maximum_queue_length) {
        if (bb_status != BB_FULL)
            bb_status = BB_FULL;
        warning(0, "incoming messages queue too long, dropping a message");
        log_sms(conn, sms, "DROPPED Received SMS");
        gwthread_sleep(0.1); /* letting the queue go down */
     	return -1;
    }

    /* else if (list_len(incoming_sms) > 100)
     *	gwthread_sleep(0.5);
     */
    
    if (unified_prefix == NULL)
    	uf = NULL;
    else
    	uf = octstr_get_cstr(unified_prefix);

    normalize_number(uf, &(sms->sms.sender));

    if (white_list &&
	numhash_find_number(white_list, sms->sms.sender) < 1) {
	info(0, "Number <%s> is not in white-list, message discarded",
	     octstr_get_cstr(sms->sms.sender));
	log_sms(conn, sms, "REJECTED - not white-listed SMS");
	msg_destroy(sms);
        return -1;
    }
    if (black_list &&
	numhash_find_number(black_list, sms->sms.sender) == 1) {
	info(0, "Number <%s> is in black-list, message discarded",
	     octstr_get_cstr(sms->sms.sender));
	log_sms(conn, sms, "REJECTED - black-listed SMS");
	msg_destroy(sms);
	return -1;
    }

    if (sms->sms.sms_type != report)
	sms->sms.sms_type = mo;
    
    /* write to store (if enabled) */
    if (store_save(sms) == -1)
	return -1;
    
    if (sms->sms.sms_type != report)
	log_sms(conn, sms, "Receive SMS");
    else
	log_sms(conn, sms, "DLR SMS");

    list_produce(incoming_sms, sms);
    counter_increase(incoming_sms_counter);
    counter_increase(conn->received);

    return 0;
}


/*---------------------------------------------------------------------
 * Other functions
 */


    
/* function to route outgoing SMS'es from delay-list
 * use some nice magics to route them to proper SMSC
 */
static void sms_router(void *arg)
{
    Msg *msg, *newmsg, *startmsg;
    int ret;
    
    list_add_producer(flow_threads);
    gwthread_wakeup(MAIN_THREAD_ID);

    newmsg = startmsg = NULL;
    ret = 0;
    
    while(bb_status != BB_DEAD) {

	if (newmsg == startmsg) {
	    if (ret != 1) {
		debug("bb.sms", 0, "sms_router: time to sleep"); 
		gwthread_sleep(600.0);	/* hopefully someone wakes us up */
		debug("bb.sms", 0, "sms_router: list_len = %ld",
		      list_len(outgoing_sms));
	    }
	    startmsg = list_consume(outgoing_sms);
	    newmsg = NULL;
	    msg = startmsg;
	} else {
	    newmsg = list_consume(outgoing_sms);
	    msg = newmsg;
	}
	/* debug("bb.sms", 0, "sms_router: handling message (%p vs %p)",
	 *         newmsg, startmsg); */
	
	if (msg == NULL)
            break;

	ret = smsc2_rout(msg);
	if (ret == -1) {
            warning(0, "No SMSCes to receive message, discarding it!");
	    bb_smscconn_send_failed(NULL, msg, SMSCCONN_FAILED_DISCARDED);
        } else if (ret == 1) {
	    newmsg = startmsg = NULL;
	}
	    
	    
    }
    /* router has died, make sure that rest die, too */
    
    smsc_running = 0;

    list_remove_producer(flow_threads);
}




/*-------------------------------------------------------------
 * public functions
 *
 */

int smsc2_start(Cfg *cfg)
{
    CfgGroup *grp;
    SMSCConn *conn;
    Octstr *os;
    int i;

    if (smsc_running) return -1;

    smsc_list = list_create();
    smsc_groups = list_create();

    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    unified_prefix = cfg_get(grp, octstr_imm("unified-prefix"));
    if (cfg_get_integer(&maximum_queue_length, grp, 
                           octstr_imm("maximum-queue-length")) == -1)
        maximum_queue_length = -1;

    white_list = black_list = NULL;
    os = cfg_get(grp, octstr_imm("white-list"));
    if (os != NULL) {
        white_list = numhash_create(octstr_get_cstr(os));
	octstr_destroy(os);
    }
    os = cfg_get(grp, octstr_imm("black-list"));
    if (os != NULL) {
        black_list = numhash_create(octstr_get_cstr(os));
	octstr_destroy(os);
    }

    smsc_groups = cfg_get_multi_group(cfg, octstr_imm("smsc"));
    /*
    while(groups && (grp = list_extract_first(groups)) != NULL) {
        conn = smscconn_create(grp, 1); 
        if (conn == NULL)
            panic(0, "Cannot start with SMSC connection failing");
        
        list_append(smsc_list, conn);
    }
    */
    list_add_producer(smsc_list);
    for (i = 0; i < list_len(smsc_groups) && 
        (grp = list_get(smsc_groups, i)) != NULL; i++) {
        conn = smscconn_create(grp, 1); 
        if (conn == NULL)
            panic(0, "Cannot start with SMSC connection failing");
        list_append(smsc_list, conn);
    }
    list_remove_producer(smsc_list);
    
    if ((router_thread = gwthread_create(sms_router, NULL)) == -1)
	panic(0, "Failed to start a new thread for SMS routing");
    
    list_add_producer(incoming_sms);
    list_add_producer(incoming_wdp);
    smsc_running = 1;
    return 0;
}

static int smsc2_find(Octstr *id)
{
    SMSCConn *conn = NULL;
    int i;

    list_lock(smsc_list);
    for (i = 0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
        if (conn != NULL && octstr_compare(conn->id, id) == 0) {
            break;
        }
    }
    list_unlock(smsc_list);
    if (i >= list_len(smsc_list))
        i = -1;
    return i;
}

int smsc2_stop_smsc(Octstr *id)
{
    SMSCConn *conn;
    int i;

    /* find the specific smsc via id */
    if ((i = smsc2_find(id)) == -1) {
        error(0, "HTTP: Could not shutdown smsc-id `%s'", octstr_get_cstr(id));
        return -1;
    }
    conn = list_get(smsc_list, i);
    if (conn != NULL && conn->status == SMSCCONN_DEAD) {
        error(0, "HTTP: Could not shutdown already deaed smsc-id `%s'", 
              octstr_get_cstr(id));
        return -1;
    }
    info(0,"HTTP: Shutting down smsc-id `%s'", octstr_get_cstr(id));
    smscconn_shutdown(conn, 1);   /* shutdown the smsc */
    return 0;
}

int smsc2_restart_smsc(Octstr *id)
{
    CfgGroup *grp;
    SMSCConn *conn;
    Octstr *smscid;
    int i;

    /* find the specific smsc via id */
    if ((i = smsc2_find(id)) == -1) {
        error(0, "HTTP: Could not re-start non defined smsc-id `%s'", 
              octstr_get_cstr(id));
        return -1;
    }

    /* check if smsc is online status already */
    conn = list_get(smsc_list, i);
    if (conn != NULL && conn->status != SMSCCONN_DEAD) {
        error(0, "HTTP: Could not re-start already running smsc-id `%s'", 
              octstr_get_cstr(id));
        return -1;
    }
  
    list_delete(smsc_list, i, 1); /* drop it from the active smsc list */
	smscconn_destroy(conn);       /* destroy the connection */

    /* find the group with smsc id */
    grp = NULL;
    list_lock(smsc_groups);
    for (i = 0; i < list_len(smsc_groups) && 
        (grp = list_get(smsc_groups, i)) != NULL; i++) {
        smscid = cfg_get(grp, octstr_imm("smsc-id"));
        if (smscid != NULL && octstr_compare(smscid, id) == 0) {
            break;
        }
    }
    list_unlock(smsc_groups);
    if (i > list_len(smsc_groups))
        return -1;
    
    info(0,"HTTP: Re-starting smsc-id `%s'", octstr_get_cstr(id));

    conn = smscconn_create(grp, 1); 
    if (conn == NULL)
        error(0, "Cannot start with SMSC connection failing");
    list_append(smsc_list, conn);

    smscconn_start(conn);
    if (router_thread >= 0)
        gwthread_wakeup(router_thread);

    return 0;
}

void smsc2_resume(void)
{
    SMSCConn *conn;
    int i;
    
    for (i = 0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
        smscconn_start(conn);
    }
    if (router_thread >= 0)
	gwthread_wakeup(router_thread);
}


void smsc2_suspend(void)
{
    SMSCConn *conn;
    int i;
    
    list_lock(smsc_list);
    for (i = 0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
        smscconn_stop(conn);
    }
    list_unlock(smsc_list);
}


int smsc2_shutdown(void)
{
    SMSCConn *conn;
    int i;

    if (!smsc_running) return -1;

    /* Call shutdown for all SMSC Connections; they should
     * handle that they quit, by emptying queues and then dying off
     */
    list_lock(smsc_list);
    for(i=0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
	smscconn_shutdown(conn, 1);
    }
    list_unlock(smsc_list);
    if (router_thread >= 0)
	gwthread_wakeup(router_thread);

    /* start avalanche by calling shutdown */

    /* XXX shouldn'w we be sure that all smsces have closed their
     * receive thingies? Is this guaranteed by setting bb_status
     * to shutdown before calling these?
     */
    list_remove_producer(incoming_sms);
    list_remove_producer(incoming_wdp);
    return 0;
}


void smsc2_cleanup(void)
{
    SMSCConn *conn;
    int i;

    debug("smscconn", 0, "final clean-up for SMSCConn");
    
    list_lock(smsc_list);
    for (i = 0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
        smscconn_destroy(conn);
    }
    list_unlock(smsc_list);
    list_destroy(smsc_list, NULL);
    list_destroy(smsc_groups, NULL);
    octstr_destroy(unified_prefix);    
    numhash_destroy(white_list);
    numhash_destroy(black_list);
}


Octstr *smsc2_status(int status_type)
{
    Octstr *tmp;
    char tmp3[64];
    char *lb;
    int i, para = 0;
    SMSCConn *conn;
    StatusInfo info;
    Octstr *conn_id = NULL;
    Octstr *conn_name = NULL;

    if ((lb = bb_status_linebreak(status_type)) == NULL)
        return octstr_create("Un-supported format");

    if (status_type == BBSTATUS_HTML || status_type == BBSTATUS_WML)
        para = 1;

    if (!smsc_running) {
        if (status_type == BBSTATUS_XML)
            return octstr_create ("<smscs>\n\t<count>0</count>\n</smscs>");
        else
            return octstr_format("%sNo SMSC connections%s\n\n", para ? "<p>" : "",
                                 para ? "</p>" : "");
    }

    if (status_type != BBSTATUS_XML)
        tmp = octstr_format("%sSMSC connections:%s", para ? "<p>" : "", lb);
    else
        tmp = octstr_format("<smscs><count>%d</count>\n\t", list_len(smsc_list));
    
    list_lock(smsc_list);
    for (i = 0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);

        if ((smscconn_info(conn, &info) == -1)) {
            /* 
             * we do not delete SMSCs from the list 
             * this way we can show in the status which links are dead
             */
            continue;
        }

        conn_id = conn ? smscconn_id(conn) : octstr_imm("unknown");
        conn_name = conn ? smscconn_name(conn) : octstr_imm("unknown");

        if (status_type == BBSTATUS_HTML) {
            octstr_append_cstr(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;<b>");
            octstr_append(tmp, conn_id);
            octstr_append_cstr(tmp, "</b>&nbsp;&nbsp;&nbsp;&nbsp;");
        } else if (status_type == BBSTATUS_TEXT) {
            octstr_append_cstr(tmp, "    ");
            octstr_append(tmp, conn_id);
            octstr_append_cstr(tmp, "    ");
        } 
        if (status_type == BBSTATUS_XML) {
            octstr_append_cstr(tmp, "<smsc>\n\t\t<name>");
            octstr_append(tmp, conn_name);
            octstr_append_cstr(tmp, "</name>\n\t\t");
            octstr_append_cstr(tmp, "<id>");
            octstr_append(tmp, conn_id);
            octstr_append_cstr(tmp, "</id>\n\t\t");
        } else
            octstr_append(tmp, conn_name);

        switch (info.status) {
            case SMSCCONN_ACTIVE:
            case SMSCCONN_ACTIVE_RECV:
                sprintf(tmp3, "online %lds", info.online);
                break;
            case SMSCCONN_DISCONNECTED:
                sprintf(tmp3, "disconnected");
                break;
            case SMSCCONN_CONNECTING:
                sprintf(tmp3, "connecting");
                break;
            case SMSCCONN_RECONNECTING:
                sprintf(tmp3, "re-connecting");
                break;
            case SMSCCONN_DEAD:
                sprintf(tmp3, "dead");
                break;
            default:
                sprintf(tmp3, "unknown");
        }
	
        if (status_type == BBSTATUS_XML)
            octstr_format_append(tmp, "<status>%s</status>\n\t\t<received>%ld</received>"
                "\n\t\t<sent>%ld</sent>\n\t\t<failed>%ld</failed>\n\t\t"
                "<queued>%ld</queued>\n\t</smsc>\n", tmp3,
                info.received, info.sent, info.failed,
                info.queued);
        else
            octstr_format_append(tmp, " (%s, rcvd %ld, sent %ld, failed %ld, "
                "queued %ld msgs)%s", tmp3,
            info.received, info.sent, info.failed,
            info.queued, lb);
    }
    list_unlock(smsc_list);

    if (para)
        octstr_append_cstr(tmp, "</p>");
    if (status_type == BBSTATUS_XML)
        octstr_append_cstr(tmp, "</smscs>\n");
    else
        octstr_append_cstr(tmp, "\n\n");
    return tmp;
}


/* function to route outgoing SMS'es
 *
 * If finds a good one, puts into it and returns 1
 * If finds only bad ones, but acceptable, queues and
 *  returns 0  (like all acceptable currently disconnected)
 * If cannot find nothing at all, returns -1 and
 * message is NOT destroyed (otherwise it is)
 */
int smsc2_rout(Msg *msg)
{
    StatusInfo info;
    SMSCConn *conn, *best_preferred, *best_ok;
    long bp_load, bo_load;
    int i, s, ret, bad_found;
    char *uf;

    bp_load = bo_load = 0;

    /* XXX handle ack here? */
    if (msg_type(msg) != sms)
	return -1;
    
    if (list_len(smsc_list) == 0) {
	warning(0, "No SMSCes to receive message");
	return -1;
    }

    /* unify prefix of receiver, in case of it has not been
     * already done */

    uf = unified_prefix ? octstr_get_cstr(unified_prefix) : NULL;
    normalize_number(uf, &(msg->sms.receiver));
            
    /* select in which list to add this
     * start - from random SMSCConn, as they are all 'equal'
     */

    list_lock(smsc_list);

    s = gw_rand() % list_len(smsc_list);
    best_preferred = best_ok = NULL;
    bad_found = 0;
    
    conn = NULL;
    for (i=0; i < list_len(smsc_list); i++) {
	conn = list_get(smsc_list,  (i+s) % list_len(smsc_list));

	ret = smscconn_usable(conn,msg);
	if (ret == -1)
	    continue;

	/* if we already have a preferred one, skip non-preferred */
	if (ret != 1 && best_preferred)	
	    continue;

	smscconn_info(conn, &info);
	/* If connection is not currently answering... */
	if (info.status != SMSCCONN_ACTIVE) {
	    bad_found = 1;
	    continue;
	}
	if (ret == 1) {          /* preferred */
	    if (best_preferred == NULL || info.load < bp_load) {
		best_preferred = conn;
		bp_load = info.load;
		continue;
	    }
	}
	if (best_ok == NULL || info.load < bo_load) {
	    best_ok = conn;
	    bo_load = info.load;
	}
    }
    list_unlock(smsc_list);

    if (best_preferred)
	ret = smscconn_send(best_preferred, msg);
    else if (best_ok)
	ret = smscconn_send(best_ok, msg);
    else if (bad_found) {
	if (bb_status != BB_SHUTDOWN)
	    list_produce(outgoing_sms, msg);
	return 0;
    }
    else {
	if (bb_status == BB_SHUTDOWN)
	    return 0;
	warning(0, "Cannot find SMSCConn for message to <%s>, rejected.",
		octstr_get_cstr(msg->sms.receiver));
	return -1;
    }
    /* check the status of sending operation */
    if (ret == -1)
	return (smsc2_rout(msg));	/* re-try */

    msg_destroy(msg);
    return 1;
}














