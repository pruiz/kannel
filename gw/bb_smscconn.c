/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2004 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

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
#include "dlr.h"

#include "bb_smscconn_cb.h"    /* callback functions for connections */
#include "smscconn_p.h"        /* to access counters */

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
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
static RWLock smsc_list_lock;
static List *smsc_groups;
static Octstr *unified_prefix;

static Numhash *black_list;
static Numhash *white_list;

static regex_t *white_list_regex;
static regex_t *black_list_regex;

static long router_thread = -1;

/*
 * Counter for catenated SMS messages. The counter that can be put into
 * the catenated SMS message's UDH headers is actually the lowest 8 bits.
 */
Counter *split_msg_counter;

/*
 * forward declaration
 */
static long route_incoming_to_smsc(SMSCConn *conn, Msg *msg);


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


static void handle_split(SMSCConn *conn, Msg *msg, long reason)
{
    struct split_parts *split = msg->sms.split_parts;
    
    /* if temporarely failed, try again immediately */
    if (reason == SMSCCONN_FAILED_TEMPORARILY && smscconn_send(conn, msg) == 0)
        return;
    
    /*
     * if the reason is not a success and status is still success
     * then set status of a split to the reason.
     * Note: reason 'malformed','discarded' or 'rejected' has higher priority!
     */
    switch(reason) {
    case SMSCCONN_FAILED_DISCARDED:
    case SMSCCONN_FAILED_REJECTED:
    case SMSCCONN_FAILED_MALFORMED:
        debug("bb.sms.splits", 0, "Set split msg status to %ld", reason);
        split->status = reason;
        break;
    case SMSCCONN_SUCCESS:
        break; /* nothing todo */
    default:
        if (split->status == SMSCCONN_SUCCESS) {
            debug("bb.sms.splits", 0, "Set split msg status to %ld", reason);
            split->status = reason;
        }
        break;
    }

    /*
     * now destroy this message, because we don't need it anymore.
     * we will split it again in smscconn_send(...).
     */
    msg_destroy(msg);
        
    if (counter_decrease(split->parts_left) <= 1) {
        /* all splited parts were processed */
        counter_destroy(split->parts_left);
        msg = split->orig;
        msg->sms.split_parts = NULL;
        if (split->status == SMSCCONN_SUCCESS)
            bb_smscconn_sent(conn, msg, NULL);
        else {
            debug("bb.sms.splits", 0, "Parts of concatenated message failed.");
            bb_smscconn_send_failed(conn, msg, split->status, NULL);
        }
        gw_free(split);
    }
}


void bb_smscconn_sent(SMSCConn *conn, Msg *sms, Octstr *reply)
{
    if (sms->sms.split_parts != NULL) {
        handle_split(conn, sms, SMSCCONN_SUCCESS);
        return;
    }
    
    counter_increase(outgoing_sms_counter);
    if (conn) counter_increase(conn->sent);

    /* write ACK to store file */
    store_save_ack(sms, ack_success);

    bb_alog_sms(conn, sms, "Sent SMS");

    /* generate relay confirmancy message */
    if (DLR_IS_SMSC_SUCCESS(sms->sms.dlr_mask)) {
        Msg *dlrmsg;

	if (reply == NULL)
	    reply = octstr_create("");

	octstr_insert_data(reply, 0, "ACK/", 4);
        dlrmsg = create_dlr_from_msg((conn->id?conn->id:conn->name), sms,
	                reply, DLR_SMSC_SUCCESS);
        if (dlrmsg != NULL) {
            bb_smscconn_receive(conn, dlrmsg);
        }
    }

    msg_destroy(sms);
    octstr_destroy(reply);
}


void bb_smscconn_send_failed(SMSCConn *conn, Msg *sms, int reason, Octstr *reply)
{
    if (sms->sms.split_parts != NULL) {
        handle_split(conn, sms, reason);
        return;
    }
    
    switch (reason) {

    case SMSCCONN_FAILED_SHUTDOWN:
    case SMSCCONN_FAILED_TEMPORARILY:
	list_produce(outgoing_sms, sms);
	break;
    default:
	/* write NACK to store file */
        store_save_ack(sms, ack_failed);

	if (conn) counter_increase(conn->failed);
	if (reason == SMSCCONN_FAILED_DISCARDED)
	    bb_alog_sms(conn, sms, "DISCARDED SMS");
	else
	    bb_alog_sms(conn, sms, "FAILED Send SMS");

        /* generate relay confirmancy message */
        if (DLR_IS_SMSC_FAIL(sms->sms.dlr_mask) ||
	    DLR_IS_FAIL(sms->sms.dlr_mask)) {
            Msg *dlrmsg;

	    if (reply == NULL)
	        reply = octstr_create("");

	    octstr_insert_data(reply, 0, "NACK/", 5);
            dlrmsg = create_dlr_from_msg((conn ? (conn->id?conn->id:conn->name) : NULL), sms,
	                                 reply, DLR_SMSC_FAIL);
            if (dlrmsg != NULL) {
                bb_smscconn_receive(conn, dlrmsg);
            }
        }
	msg_destroy(sms);
    }

    octstr_destroy(reply);
}

long bb_smscconn_receive(SMSCConn *conn, Msg *sms)
{
    char *uf;
    int rc;
    Msg *copy;

   /*
    * First normalize in smsc level and then on global level.
    * In outbound direction it's vise versa, hence first global then smsc.
    */
    uf = (conn && conn->unified_prefix) ? octstr_get_cstr(conn->unified_prefix) : NULL;
    normalize_number(uf, &(sms->sms.sender));

    uf = unified_prefix ? octstr_get_cstr(unified_prefix) : NULL;
    normalize_number(uf, &(sms->sms.sender));

    if (white_list &&
	numhash_find_number(white_list, sms->sms.sender) < 1) {
	info(0, "Number <%s> is not in white-list, message discarded",
	     octstr_get_cstr(sms->sms.sender));
	bb_alog_sms(conn, sms, "REJECTED - not white-listed SMS");
	msg_destroy(sms);
        return SMSCCONN_FAILED_REJECTED;
    }

    if (white_list_regex && (gw_regex_matches(white_list_regex, sms->sms.sender) == NO_MATCH)) {
        info(0, "Number <%s> is not in white-list, message discarded",
             octstr_get_cstr(sms->sms.sender));
        bb_alog_sms(conn, sms, "REJECTED - not white-regex-listed SMS");
        msg_destroy(sms);
        return SMSCCONN_FAILED_REJECTED;
    }
    
    if (black_list &&
	numhash_find_number(black_list, sms->sms.sender) == 1) {
	info(0, "Number <%s> is in black-list, message discarded",
	     octstr_get_cstr(sms->sms.sender));
	bb_alog_sms(conn, sms, "REJECTED - black-listed SMS");
	msg_destroy(sms);
	return SMSCCONN_FAILED_REJECTED;
    }

    if (black_list_regex && (gw_regex_matches(black_list_regex, sms->sms.sender) == NO_MATCH)) {
        info(0, "Number <%s> is not in black-list, message discarded",
             octstr_get_cstr(sms->sms.sender));
        bb_alog_sms(conn, sms, "REJECTED - black-regex-listed SMS");
        msg_destroy(sms);
        return SMSCCONN_FAILED_REJECTED;
    }

    if (sms->sms.sms_type != report_mo)
	sms->sms.sms_type = mo;

    /* write to store (if enabled) */
    if (store_save(sms) == -1)
	return SMSCCONN_FAILED_TEMPORARILY;

    copy = msg_duplicate(sms);

    /*
     * Try to reroute internally to an smsc-id without leaving
     * actually bearerbox scope.
     * Scope: internal routing (to smsc-ids)
     */
    if ((rc = route_incoming_to_smsc(conn, copy)) == -1) {
        /*
         * Now try to route the message to a specific smsbox
         * connection based on the existing msg->sms.boxc_id or
         * the registered receiver numbers for specific smsbox'es.
         * Scope: external routing (to smsbox connections)
         */
        if (route_incoming_to_boxc(copy) == -1) {
            warning(0, "incoming messages queue too long, dropping a message.");
            if (sms->sms.sms_type == report_mo)
                bb_alog_sms(conn, sms, "DROPPED Received DLR");
            else
                bb_alog_sms(conn, sms, "DROPPED Received SMS");

            /* put nack into store-file */
            store_save_ack(sms, ack_failed);

            msg_destroy(copy);

            msg_destroy(sms);
            gwthread_sleep(0.1); /* letting the queue go down */
            return SMSCCONN_FAILED_QFULL;
        }
    }

    if (sms->sms.sms_type != report_mo)
	bb_alog_sms(conn, sms, "Receive SMS");
    else
	bb_alog_sms(conn, sms, "DLR SMS");

    counter_increase(incoming_sms_counter);
    if (conn != NULL) counter_increase(conn->received);

    msg_destroy(sms);

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
	    bb_smscconn_send_failed(NULL, msg, SMSCCONN_FAILED_DISCARDED,
	                            octstr_create("DISCARDED"));
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

    /* create split sms counter */
    split_msg_counter = counter_create();
    
    smsc_list = list_create();
    gw_rwlock_init_static(&smsc_list_lock);

    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    unified_prefix = cfg_get(grp, octstr_imm("unified-prefix"));

    white_list = black_list = NULL;
    os = cfg_get(grp, octstr_imm("white-list"));
    if (os != NULL) {
        white_list = numhash_create(octstr_get_cstr(os));
	octstr_destroy(os);
    }
    if ((os = cfg_get(grp, octstr_imm("white-list-regex"))) != NULL) {
        if ((white_list_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
        octstr_destroy(os);
    }
    
    os = cfg_get(grp, octstr_imm("black-list"));
    if (os != NULL) {
        black_list = numhash_create(octstr_get_cstr(os));
	octstr_destroy(os);
    }
    if ((os = cfg_get(grp, octstr_imm("black-list-regex"))) != NULL) {
        if ((black_list_regex = gw_regex_comp(os, REG_EXTENDED)) == NULL)
            panic(0, "Could not compile pattern '%s'", octstr_get_cstr(os));
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
    smsc_running = 1;
    return 0;
}

/*
 * Find a matching smsc-id in the smsc list starting at position start.
 * NOTE: Caller must ensure that smsc_list is properly locked!
 */
static long smsc2_find(Octstr *id, long start)
{
    SMSCConn *conn = NULL;
    long i;

    if (start > list_len(smsc_list) || start < 0)
        return -1;

    for (i = start; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
        if (conn != NULL && octstr_compare(conn->id, id) == 0) {
            break;
        }
    }
    if (i >= list_len(smsc_list))
        i = -1;
    return i;
}

int smsc2_stop_smsc(Octstr *id)
{
    SMSCConn *conn;
    long i = -1;

    if (!smsc_running)
        return -1;

    gw_rwlock_rdlock(&smsc_list_lock);
    /* find the specific smsc via id */
    while((i = smsc2_find(id, ++i)) != -1) {
        conn = list_get(smsc_list, i);
        if (conn != NULL && smscconn_status(conn) == SMSCCONN_DEAD) {
            info(0, "HTTP: Could not shutdown already dead smsc-id `%s'",
                octstr_get_cstr(id));
        } else {
            info(0,"HTTP: Shutting down smsc-id `%s'", octstr_get_cstr(id));
            smscconn_shutdown(conn, 1);   /* shutdown the smsc */
        }
    }
    gw_rwlock_unlock(&smsc_list_lock);
    return 0;
}

int smsc2_restart_smsc(Octstr *id)
{
    CfgGroup *grp;
    SMSCConn *conn, *new_conn;
    Octstr *smscid = NULL;
    long i = -1;
    int num = 0;

    if (!smsc_running)
        return -1;

    gw_rwlock_wrlock(&smsc_list_lock);
    /* find the specific smsc via id */
    while((i = smsc2_find(id, ++i)) != -1) {
        int hit;
        long group_index;
        /* check if smsc has online status already */
        conn = list_get(smsc_list, i);
        if (conn != NULL && smscconn_status(conn) != SMSCCONN_DEAD) {
            warning(0, "HTTP: Could not re-start already running smsc-id `%s'",
                octstr_get_cstr(id));
            continue;
        }
        /* find the group with equal smsc id */
        hit = 0;
        grp = NULL;
        for (group_index = 0; group_index < list_len(smsc_groups) && 
             (grp = list_get(smsc_groups, group_index)) != NULL; group_index++) {
            smscid = cfg_get(grp, octstr_imm("smsc-id"));
            if (smscid != NULL && octstr_compare(smscid, id) == 0) {
                if (hit == num)
                    break;
                else
                    hit++;
            }
            octstr_destroy(smscid);
            smscid = NULL;
        }
        octstr_destroy(smscid);
        if (hit != num) {
            /* config group not found */
            error(0, "HTTP: Could not find config for smsc-id `%s'", octstr_get_cstr(id));
            break;
        }
        
        info(0,"HTTP: Re-starting smsc-id `%s'", octstr_get_cstr(id));

        new_conn = smscconn_create(grp, 1);
        if (new_conn == NULL) {
            error(0, "Start of SMSC connection failed, smsc-id `%s'", octstr_get_cstr(id));
            continue; /* keep old connection on the list */
        }
        
        /* drop old connection from the active smsc list */
        list_delete(smsc_list, i, 1);
        /* destroy the connection */
        smscconn_destroy(conn);
        list_insert(smsc_list, i, new_conn);
        smscconn_start(new_conn);
        num++;
    }
    gw_rwlock_unlock(&smsc_list_lock);
    
    /* wake-up the router */
    if (router_thread >= 0)
        gwthread_wakeup(router_thread);
    
    return 0;
}

void smsc2_resume(void)
{
    SMSCConn *conn;
    long i;

    if (!smsc_running)
        return;

    gw_rwlock_rdlock(&smsc_list_lock);
    for (i = 0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
        smscconn_start(conn);
    }
    gw_rwlock_unlock(&smsc_list_lock);
    
    if (router_thread >= 0)
        gwthread_wakeup(router_thread);
}


void smsc2_suspend(void)
{
    SMSCConn *conn;
    long i;

    if (!smsc_running)
        return;

    gw_rwlock_rdlock(&smsc_list_lock);
    for (i = 0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
        smscconn_stop(conn);
    }
    gw_rwlock_unlock(&smsc_list_lock);
}


int smsc2_shutdown(void)
{
    SMSCConn *conn;
    long i;

    if (!smsc_running)
        return -1;

    /* Call shutdown for all SMSC Connections; they should
     * handle that they quit, by emptying queues and then dying off
     */
    gw_rwlock_rdlock(&smsc_list_lock);
    for(i=0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
	smscconn_shutdown(conn, 1);
    }
    gw_rwlock_unlock(&smsc_list_lock);
    if (router_thread >= 0)
	gwthread_wakeup(router_thread);

    /* start avalanche by calling shutdown */

    /* XXX shouldn'w we be sure that all smsces have closed their
     * receive thingies? Is this guaranteed by setting bb_status
     * to shutdown before calling these?
     */
    list_remove_producer(incoming_sms);
    return 0;
}


void smsc2_cleanup(void)
{
    SMSCConn *conn;
    long i;

    if (!smsc_running)
        return;

    debug("smscconn", 0, "final clean-up for SMSCConn");
    
    gw_rwlock_wrlock(&smsc_list_lock);
    for (i = 0; i < list_len(smsc_list); i++) {
        conn = list_get(smsc_list, i);
        smscconn_destroy(conn);
    }
    list_destroy(smsc_list, NULL);
    smsc_list = NULL;
    gw_rwlock_unlock(&smsc_list_lock);
    list_destroy(smsc_groups, NULL);
    octstr_destroy(unified_prefix);    
    numhash_destroy(white_list);
    numhash_destroy(black_list);
    if (white_list_regex != NULL)
        gw_regex_destroy(white_list_regex);
    if (black_list_regex != NULL)
        gw_regex_destroy(black_list_regex);
    /* destroy msg split counter */
    counter_destroy(split_msg_counter);
    gw_rwlock_destroy(&smsc_list_lock);
}


Octstr *smsc2_status(int status_type)
{
    Octstr *tmp;
    char tmp3[64];
    char *lb;
    long i;
    int para = 0;
    SMSCConn *conn;
    StatusInfo info;
    const Octstr *conn_id = NULL;
    const Octstr *conn_name = NULL;

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

    gw_rwlock_rdlock(&smsc_list_lock);
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
        conn_id = conn_id ? conn_id : octstr_imm("unknown");
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
    gw_rwlock_unlock(&smsc_list_lock);

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
    
    /* unify prefix of receiver, in case of it has not been
     * already done */

    uf = unified_prefix ? octstr_get_cstr(unified_prefix) : NULL;
    normalize_number(uf, &(msg->sms.receiver));
            
    /* select in which list to add this
     * start - from random SMSCConn, as they are all 'equal'
     */
    gw_rwlock_rdlock(&smsc_list_lock);
    if (list_len(smsc_list) == 0) {
	warning(0, "No SMSCes to receive message");
        gw_rwlock_unlock(&smsc_list_lock);
	return SMSCCONN_FAILED_DISCARDED;
    }

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

    if (best_preferred)
	ret = smscconn_send(best_preferred, msg);
    else if (best_ok)
	ret = smscconn_send(best_ok, msg);
    else if (bad_found) {
	if (bb_status != BB_SHUTDOWN)
	    list_produce(outgoing_sms, msg);
        gw_rwlock_unlock(&smsc_list_lock);
	return 0;
    }
    else {
        gw_rwlock_unlock(&smsc_list_lock);
	if (bb_status == BB_SHUTDOWN)
	    return 0;
	warning(0, "Cannot find SMSCConn for message to <%s>, rejected.",
		octstr_get_cstr(msg->sms.receiver));
	return -1;
    }
    gw_rwlock_unlock(&smsc_list_lock);
    /* check the status of sending operation */
    if (ret == -1)
	return (smsc2_rout(msg));	/* re-try */

    msg_destroy(msg);
    return 1;
}


/*
 * Try to reroute to another smsc.
 * @return -1 if no rerouting info available; otherwise return code from smsc2_route.
 */
static long route_incoming_to_smsc(SMSCConn *conn, Msg *msg)
{
    Octstr *smsc;
    
    /* sanity check */
    if (!conn || !msg)
        return -1;
        
    /* check for dlr rerouting */
    if (!conn->reroute_dlr && (msg->sms.sms_type == report_mo || msg->sms.sms_type == report_mt))
        return -1;

    /*
     * Check if we have any "reroute" rules to obey. Which means msg gets
     * transported internally from MO to MT msg.
     */
    if (conn->reroute) {
        /* change message direction */
        store_save_ack(msg, ack_success);
        msg->sms.sms_type = mt_push;
        store_save(msg);
        /* drop into outbound queue again for routing */
        return smsc2_rout(msg);
    }
    
    if (conn->reroute_to_smsc) {
        /* change message direction */
        store_save_ack(msg, ack_success);
        msg->sms.sms_type = mt_push;
        store_save(msg);
        /* apply directly to the given smsc-id for MT traffic */
        octstr_destroy(msg->sms.smsc_id);
        msg->sms.smsc_id = octstr_duplicate(conn->reroute_to_smsc);
        return smsc2_rout(msg);
    }
    
    if (conn->reroute_by_receiver && msg->sms.receiver &&
                 (smsc = dict_get(conn->reroute_by_receiver, msg->sms.receiver))) {
        /* change message direction */
        store_save_ack(msg, ack_success);
        msg->sms.sms_type = mt_push;
        store_save(msg);
        /* route by receiver number */
        /* XXX implement wildcard matching too! */
        octstr_destroy(msg->sms.smsc_id);
        msg->sms.smsc_id = octstr_duplicate(smsc);
        return smsc2_rout(msg);
    }

    return -1; 
}

