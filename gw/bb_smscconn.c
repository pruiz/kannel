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
#include "bearerbox.h"
#include "numhash.h"
#include "smscconn.h"

#include "bb_smscconn_cb.h"    /* callback functions for connections */


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
static char *unified_prefix;

static Numhash *black_list;
static Numhash *white_list;




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


void bb_smscconn_killed(int reason)
{
    /* NOTE: after status has been set to KILLED, bearerbox
     *   is free to release/delete 'conn'
     */
    list_remove_producer(incoming_sms);
    list_remove_producer(flow_threads);
}


void bb_smscconn_sent(SMSCConn *conn, Msg *sms)
{
    counter_increase(outgoing_sms_counter);
    alog("Sent a message - SMSC:%s receiver:%s msg: '%s'",
	 smscconn_id(conn),
	 octstr_get_cstr(sms->sms.receiver),
	 octstr_get_cstr(sms->sms.msgdata));

    msg_destroy(sms);
}


void bb_smscconn_send_failed(SMSCConn *conn, Msg *sms, int reason)
{
    switch (reason) {

    case SMSCCONN_FAILED_SHUTDOWN:
	list_produce(outgoing_sms, sms);
	break;
    default:
	/* XXX yell etc. here */
	alog("Send FAILED - SMSC:%s receiver:%s msg: '%s'",
             smscconn_id(conn),
             octstr_get_cstr(sms->sms.receiver),
             octstr_get_cstr(sms->sms.msgdata));
	msg_destroy(sms);
    }
}    


int bb_smscconn_receive(SMSCConn *conn, Msg *sms)
{

    normalize_number(unified_prefix, &(sms->sms.sender));

    if (white_list &&
	numhash_find_number(white_list, sms->sms.sender) < 1) {
	info(0, "Number <%s> is not in white-list, message discarded",
	     octstr_get_cstr(sms->sms.sender));
	msg_destroy(sms);
        return -1;
    }
    if (black_list &&
	numhash_find_number(black_list, sms->sms.sender) == 1) {
	info(0, "Number <%s> is in black-list, message discarded",
	     octstr_get_cstr(sms->sms.sender));
	msg_destroy(sms);
	return -1;
    }

    /*
     * XXX  WAP on SMS - assembling WDP packets should be somehow here
     */
    
    alog("Received a message - SMSC:%s sender:%s msg: '%s'",
	 smscconn_name(conn),
	 octstr_get_cstr(sms->sms.sender),
	 octstr_get_cstr(sms->sms.msgdata));

    list_produce(incoming_sms, sms);
    counter_increase(incoming_sms_counter);

    return 0;
}


/*------------------------------------------------------------------------------
 * Other functions
 */


    
/* function to route outgoing SMS'es,
 * use some nice magics to route them to proper SMSC
 */
static void sms_router(void *arg)
{
    Msg *msg;
    SMSCConn *conn, *best_preferred, *best_ok, *best_bad;
    long bp_load, bo_load, bb_load;
    int i, s, ret;
    StatusInfo info;

    list_add_producer(flow_threads);
    gwthread_wakeup(MAIN_THREAD_ID);

    while(bb_status != BB_DEAD) {

        list_consume(suspended);        /* block here if suspended */

        if ((msg = list_consume(outgoing_sms)) == NULL)
            break;

        gw_assert(msg_type(msg) == sms);

        if (list_len(smsc_list) == 0) {
            warning(0, "No SMSCes to receive message, discarding it!");
            alog("SMS DISCARDED - SMSC:%s receiver:%s msg: '%s'",
                 (msg->sms.smsc_id) == NULL ?
                 octstr_get_cstr(msg->sms.smsc_id) : "unknown",
                 octstr_get_cstr(msg->sms.receiver),
                 octstr_get_cstr(msg->sms.msgdata));
            msg_destroy(msg);
            continue;
        }
        /* XXX we normalize the receiver - if set, but do we want
         *     to normalize the sender, too?
         */
        normalize_number(unified_prefix, &(msg->sms.receiver));
            
        /* select in which list to add this
         * start - from random SMSCConn, as they are all 'equal'
         */

        list_lock(smsc_list);

        s = gw_rand() % list_len(smsc_list);
	best_preferred = best_ok = best_bad = NULL;

        for (i=0; i < list_len(smsc_list); i++) {
            conn = list_get(smsc_list,  (i+s) % list_len(smsc_list));

	    ret = smscconn_usable(conn,msg);
	    if (ret == -1)
		continue;

	    /* if we already have a preferred one, skip non-preferred */
	    if (ret != 1 && best_preferred)	
		continue;

	    smscconn_info(conn, &info);
	    if (info.status != SMSCCONN_ACTIVE) {
		if (best_bad == NULL || info.load < bb_load) {
		    best_bad = conn;
		    bb_load = info.load;
		}
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
	else if (best_bad)
	    ret = smscconn_send(best_bad, msg);
	else {
            warning(0, "Cannot find SMSCConn for message to <%s>, discarded.",
		    octstr_get_cstr(msg->sms.receiver));
            alog("SMS DISCARDED - SMSCID:%s receiver:%s msg: '%s'",
                 (msg->sms.smsc_id) == NULL ?
                 octstr_get_cstr(msg->sms.smsc_id) : "unknown",
                 octstr_get_cstr(msg->sms.receiver),
                 octstr_get_cstr(msg->sms.msgdata));
	    ret = 0;
	}

	if (ret == -1)
	    list_produce(outgoing_sms, msg);
	else
            msg_destroy(msg);
    }
    /* router has died, make sure that rest die, too */
    
    smsc_running = 0;

    list_remove_producer(flow_threads);
}




/*-------------------------------------------------------------
 * public functions
 *
 */

int smsc2_start(Config *config)
{
    ConfigGroup *grp;
    SMSCConn *conn;
    char *ls;
    
    if (smsc_running) return -1;

    smsc_list = list_create();

    grp = config_find_first_group(config, "group", "core");
    unified_prefix = config_get(grp, "unified-prefix");

    white_list = black_list = NULL;
    if ((ls = config_get(grp, "white-list")) != NULL)
        white_list = numhash_create(ls);
    if ((ls = config_get(grp, "black-list")) != NULL)
        black_list = numhash_create(ls);

    grp = config_find_first_group(config, "group", "smsc");
    while(grp != NULL) {
        conn = smscconn_create(grp, 0); /* should we start as stopped? */
        if (conn == NULL)
            panic(0, "Cannot start with SMSC connection failing");
        
        list_append(smsc_list, conn);
        grp = config_find_next_group(grp, "group", "smsc");
    }
    if (gwthread_create(sms_router, NULL) == -1)
	panic(0, "Failed to start a new thread for SMS routing");
    
    list_add_producer(incoming_sms);
    list_add_producer(incoming_wdp);
    smsc_running = 1;
    return 0;
}


/*
 * this function receives an WDP message, and puts into
 * WDP disassembly unit list... in the future!
 */
int smsc2_addwdp(Msg *msg)
{
    if (!smsc_running) return -1;
    
    return -1;
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
    
    for(i=0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
	smscconn_destroy(conn);
    }
    list_destroy(smsc_list, NULL);
    
    numhash_destroy(white_list);
    numhash_destroy(black_list);
}


Octstr *smsc2_status(int status_type)
{
    char tmp[512], tmp2[256], tmp3[64];
    char *lb;
    int i, para = 0;
    SMSCConn *conn;
    StatusInfo info;

    if ((lb = bb_status_linebreak(status_type))==NULL)
	return octstr_create("Un-supported format");

    if (status_type == BBSTATUS_HTML || status_type == BBSTATUS_WML)
	para = 1;

    if (!smsc_running) {
	sprintf(tmp, "%sNo SMSC connections%s\n\n", para ? "<p>" : "",
		para ? "</p>" : "");
	return octstr_create(tmp);
    }
    sprintf(tmp, "%sSMSC connections:%s", para ? "<p>" : "", lb);
    
    for(i=0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
	smscconn_info(conn, &info);

	if (info.status == SMSCCONN_KILLED)
	    continue;

	if (status_type == BBSTATUS_HTML)
	    strcat(tmp, "&nbsp;&nbsp;&nbsp;&nbsp;");
	else if (status_type == BBSTATUS_TEXT)
	    strcat(tmp, "    ");
        strcat(tmp, octstr_get_cstr(smscconn_name(conn)));
	switch(info.status) {
	case SMSCCONN_ACTIVE:
	    sprintf(tmp3, "online %lds", info.online);
	    break;
	case SMSCCONN_DISCONNECTED:
	    sprintf(tmp3, "disconnected");
	    break;
	default:
	    sprintf(tmp3, "connecting");
	}
	
	sprintf(tmp2, " (%s, rcvd %ld, sent %ld, failed %ld, queued %ld msgs)%s", tmp3,
		info.received, info.sent, info.failed, info.queued, lb);
	strcat(tmp, tmp2);
    }
    if (para)
	strcat(tmp, "</p>");
    strcat(tmp, "\n\n");
    return octstr_create(tmp);
}



