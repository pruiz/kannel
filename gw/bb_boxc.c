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
 * bb_boxc.c : bearerbox box connection module
 *
 * handles start/restart/stop/suspend/die operations of the sms and
 * wapbox connections
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
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "bearerbox.h"
#include "bb_smscconn_cb.h"

#define SMSBOX_MAX_PENDING 100

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern volatile sig_atomic_t restart;
extern List *incoming_sms;
extern List *outgoing_sms;
extern List *incoming_wdp;
extern List *outgoing_wdp;

extern List *flow_threads;
extern List *suspended;

/* incoming/outgoing sms queue control */
extern long max_incoming_sms_qlength;


/* our own thingies */

static volatile sig_atomic_t smsbox_running;
static volatile sig_atomic_t wapbox_running;
static List	*wapbox_list;
static List	*smsbox_list;
static RWLock   *smsbox_list_rwlock;

/* dictionaries for holding the smsbox routing information */
static Dict *smsbox_by_id;
static Dict *smsbox_by_smsc;
static Dict *smsbox_by_receiver;

static long	smsbox_port;
static int smsbox_port_ssl;
static long	wapbox_port;
static int wapbox_port_ssl;

/* max pending messages on the line to smsbox */
static long smsbox_max_pending;

static Octstr *box_allow_ip;
static Octstr *box_deny_ip;


static Counter *boxid;

/* sms_to_smsboxes thread-id */
static long sms_dequeue_thread;


typedef struct _boxc {
    Connection	*conn;
    int               is_wap;
    long            id;
    int               load;
    time_t        connect_time;
    Octstr        *client_ip;
    List            *incoming;
    List            *retry;   	/* If sending fails */
    List            *outgoing;
    Dict           *sent;
    Semaphore *pending;
    volatile sig_atomic_t alive;
    Octstr        *boxc_id; /* identifies the connected smsbox instance */
    /* used to mark connection usable or still waiting for ident. msg */
    volatile int routable;
} Boxc;


/* forward declaration */
static void sms_to_smsboxes(void *arg);
static int send_msg(Boxc *boxconn, Msg *pmsg);
static void boxc_sent_push(Boxc*, Msg*);
static void boxc_sent_pop(Boxc*, Msg*);


/*-------------------------------------------------
 *  receiver thingies
 */

static Msg *read_from_box(Boxc *boxconn)
{
    int ret;
    Octstr *pack;
    Msg *msg;

    pack = NULL;
    while (bb_status != BB_DEAD && boxconn->alive) {
            /* XXX: if box doesn't send (just keep conn open) we block here while shutdown */
	    pack = conn_read_withlen(boxconn->conn);
	    gw_claim_area(pack);
	    if (pack != NULL)
	        break;
	    if (conn_error(boxconn->conn)) {
	        info(0, "Read error when reading from box <%s>, disconnecting",
		         octstr_get_cstr(boxconn->client_ip));
	        return NULL;
	    }
	    if (conn_eof(boxconn->conn)) {
	        info(0, "Connection closed by the box <%s>",
		         octstr_get_cstr(boxconn->client_ip));
	        return NULL;
	    }

	    ret = conn_wait(boxconn->conn, -1.0);
	    if (ret < 0) {
	        error(0, "Connection to box <%s> broke.",
		          octstr_get_cstr(boxconn->client_ip));
	        return NULL;
	    }
    }

    if (pack == NULL)
    	return NULL;

    msg = msg_unpack(pack);
    octstr_destroy(pack);

    if (msg == NULL)
	    error(0, "Failed to unpack data!");
    return msg;
}


/*
 * Try to deliver message to internal or smscconn queue
 * and generate ack/nack for smsbox connections.
 */
static void deliver_sms_to_queue(Msg *msg, Boxc *conn)
{
    Msg *mack, *mack_store;
    int rc;

    /*
     * save modifies ID and time, so if the smsbox uses it, save
     * it FIRST for the reply message!!!
     */
    mack = msg_create(ack);
    gw_assert(mack != NULL);
    uuid_copy(mack->ack.id, msg->sms.id);
    mack->ack.time = msg->sms.time;

    store_save(msg);

    rc = smsc2_rout(msg);
    switch(rc) {
        case 1:
           mack->ack.nack = ack_success;
           break;
        case 0:
           mack->ack.nack = ack_buffered;
           break;
        case -1: /* no router at all */
           warning(0, "Message rejected by bearerbox, no router!");
           /*
            * first create nack for store-file, in order to delete
            * message from store-file.
            */
           mack_store = msg_create(ack);
           gw_assert(mack_store != NULL);
           uuid_copy(mack_store->ack.id, msg->sms.id);
           mack_store->ack.time = msg->sms.time;
           mack->ack.nack = mack_store->ack.nack = ack_failed;
           store_save(mack_store);
           msg_destroy(mack_store);

           /* destroy original message */
           msg_destroy(msg);
           break;
    }

    /* put ack into incoming queue of conn */
    send_msg(conn, mack);
    msg_destroy(mack);
}


static void boxc_receiver(void *arg)
{
    Boxc *conn = arg;
    Msg *msg, *mack;

    /* remove messages from socket until it is closed */
    while (bb_status != BB_DEAD && conn->alive) {

        list_consume(suspended);	/* block here if suspended */

        msg = read_from_box(conn);

        if (msg == NULL) {	/* garbage/connection lost */
            conn->alive = 0;
            break;
        }

        /* we don't accept new messages in shutdown phase */
        if ((bb_status == BB_SHUTDOWN || bb_status == BB_DEAD) && msg_type(msg) == sms) {
            mack = msg_create(ack);
            uuid_copy(mack->ack.id, msg->sms.id);
            mack->ack.time = msg->sms.time;
            mack->ack.nack = ack_failed_tmp;
            msg_destroy(msg);
            send_msg(conn, mack);
            msg_destroy(mack);
            continue;
        }

        if (msg_type(msg) == sms && conn->is_wap == 0) {
            debug("bb.boxc", 0, "boxc_receiver: sms received");

            /* deliver message to queue */
            deliver_sms_to_queue(msg, conn);

            if (conn->routable == 0) {
                conn->routable = 1;
                /* wakeup the dequeue thread */
                gwthread_wakeup(sms_dequeue_thread);
            }
        } else if (msg_type(msg) == wdp_datagram  && conn->is_wap) {
            debug("bb.boxc", 0, "boxc_receiver: got wdp from wapbox");

            /* XXX we should block these in SHUTDOWN phase too, but
               we need ack/nack msgs implemented first. */
            list_produce(conn->outgoing, msg);

        } else if (msg_type(msg) == sms  && conn->is_wap) {
            debug("bb.boxc", 0, "boxc_receiver: got sms from wapbox");

            /* should be a WAP push message, so tried it the same way */
            deliver_sms_to_queue(msg, conn);

            if (conn->routable == 0) {
                conn->routable = 1;
                /* wakeup the dequeue thread */
                gwthread_wakeup(sms_dequeue_thread);
            }
        } else {
            if (msg_type(msg) == heartbeat) {
                if (msg->heartbeat.load != conn->load)
		              debug("bb.boxc", 0, "boxc_receiver: heartbeat with "
			                "load value %ld received", msg->heartbeat.load);
                conn->load = msg->heartbeat.load;
            }
            else if (msg_type(msg) == ack) {
                boxc_sent_pop(conn, msg);
                store_save(msg);
                debug("bb.boxc", 0, "boxc_receiver: got ack");
            }
            /* if this is an identification message from an smsbox instance */
            else if (msg_type(msg) == admin && msg->admin.command == cmd_identify) {

                /*
                 * any smsbox sends this command even if boxc_id is NULL,
                 * but we will only consider real identified boxes
                 */
                if (msg->admin.boxc_id != NULL) {

                    /* and add the boxc_id into conn for boxc_status() output */
                    if (conn->boxc_id != NULL) {
                        dict_remove(smsbox_by_id, msg->admin.boxc_id);
                        octstr_destroy(conn->boxc_id);
                    }

                    conn->boxc_id = msg->admin.boxc_id;
                    msg->admin.boxc_id = NULL;

                    /* add this identified smsbox to the dictionary */
                    /* XXX check for equal boxc_id in Dict, otherwise we overwrite it */
                    dict_put(smsbox_by_id, conn->boxc_id, conn);
                    debug("bb.boxc", 0, "boxc_receiver: got boxc_id <%s> from <%s>",
                          octstr_get_cstr(conn->boxc_id),
                          octstr_get_cstr(conn->client_ip));
                }

                conn->routable = 1;
                /* wakeup the dequeue thread */
                gwthread_wakeup(sms_dequeue_thread);
            }
            else
                warning(0, "boxc_receiver: unknown msg received from <%s>, "
                           "ignored", octstr_get_cstr(conn->client_ip));
            msg_destroy(msg);
        }
    }
}


/*---------------------------------------------
 * sender thingies
 */

static int send_msg(Boxc *boxconn, Msg *pmsg)
{
    Octstr *pack;

    pack = msg_pack(pmsg);

    if (pack == NULL)
        return -1;

    if (boxconn->boxc_id != NULL)
        debug("bb.boxc", 0, "send_msg: sending msg to boxc: <%s>",
          octstr_get_cstr(boxconn->boxc_id));
    else
        debug("bb.boxc", 0, "send_msg: sending msg to box: <%s>",
          octstr_get_cstr(boxconn->client_ip));

    if (conn_write_withlen(boxconn->conn, pack) == -1) {
    	error(0, "Couldn't write Msg to box <%s>, disconnecting",
	      octstr_get_cstr(boxconn->client_ip));
        octstr_destroy(pack);
        return -1;
    }

    octstr_destroy(pack);
    return 0;
}


static void boxc_sent_push(Boxc *conn, Msg *m)
{
    Octstr *os;
    char id[UUID_STR_LEN + 1];
    
    if (conn->is_wap || !conn->sent || !m || msg_type(m) != sms)
        return;
    
    uuid_unparse(m->sms.id, id);
    os = octstr_create(id);
    dict_put(conn->sent, os, msg_duplicate(m));
    semaphore_down(conn->pending);
    octstr_destroy(os);
}


static void boxc_sent_pop(Boxc *conn, Msg *m)
{
    Octstr *os;
    char id[UUID_STR_LEN + 1];
    Msg *msg;

    if (conn->is_wap || !conn->sent || !m || (msg_type(m) != ack && msg_type(m) != sms))
        return;

    uuid_unparse((msg_type(m) == sms ? m->sms.id : m->ack.id), id);
    os = octstr_create(id);
    msg = dict_remove(conn->sent, os);
    octstr_destroy(os);
    if (!msg) {
        error(0, "BOXC: Got ack for nonexistend message!");
        msg_dump(m, 0);
        return;
    }
    semaphore_up(conn->pending);
    msg_destroy(msg);
}


static void boxc_sender(void *arg)
{
    Msg *msg;
    Boxc *conn = arg;

    list_add_producer(flow_threads);

    while (bb_status != BB_DEAD && conn->alive) {

        /*
         * Make sure there's no data left in the outgoing connection before
         * doing the potentially blocking list_consume()s
         */
        conn_flush(conn->conn);

        list_consume(suspended);	/* block here if suspended */

        if ((msg = list_consume(conn->incoming)) == NULL) {
            /* tell sms/wapbox to die */
            msg = msg_create(admin);
            msg->admin.command = restart ? cmd_restart : cmd_shutdown;
            send_msg(conn, msg);
            msg_destroy(msg);
            break;
        }
        if (msg_type(msg) == heartbeat) {
            debug("bb.boxc", 0, "boxc_sender: catch an heartbeat - we are alive");
            msg_destroy(msg);
            continue;
        }
        boxc_sent_push(conn, msg);
        if (!conn->alive || send_msg(conn, msg) == -1) {
            /* we got message here */
            boxc_sent_pop(conn, msg);
            list_produce(conn->retry, msg);
            break;
        }
        msg_destroy(msg);
        debug("bb.boxc", 0, "boxc_sender: sent message to <%s>",
               octstr_get_cstr(conn->client_ip));
    }
    /* the client closes the connection, after that die in receiver */
    /* conn->alive = 0; */

    /* set conn to unroutable */
    conn->routable = 0;

    list_remove_producer(flow_threads);
}

/*---------------------------------------------------------------
 * accept/create/kill thingies
 */


static Boxc *boxc_create(int fd, Octstr *ip, int ssl)
{
    Boxc *boxc;

    boxc = gw_malloc(sizeof(Boxc));
    boxc->is_wap = 0;
    boxc->load = 0;
    boxc->conn = conn_wrap_fd(fd, ssl);
    boxc->id = counter_increase(boxid);
    boxc->client_ip = ip;
    boxc->alive = 1;
    boxc->connect_time = time(NULL);
    boxc->boxc_id = NULL;
    boxc->routable = 0;
    return boxc;
}

static void boxc_destroy(Boxc *boxc)
{
    if (boxc == NULL)
	    return;

    /* do nothing to the lists, as they are only references */

    if (boxc->conn)
	    conn_destroy(boxc->conn);
    octstr_destroy(boxc->client_ip);
    octstr_destroy(boxc->boxc_id);
    gw_free(boxc);
}



static Boxc *accept_boxc(int fd, int ssl)
{
    Boxc *newconn;
    Octstr *ip;

    int newfd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    client_addr_len = sizeof(client_addr);

    newfd = accept(fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (newfd < 0)
	    return NULL;

    ip = host_ip(client_addr);

    if (is_allowed_ip(box_allow_ip, box_deny_ip, ip) == 0) {
        info(0, "Box connection tried from denied host <%s>, disconnected",
                octstr_get_cstr(ip));
        octstr_destroy(ip);
        close(newfd);
        return NULL;
    }
    newconn = boxc_create(newfd, ip, ssl);

    /*
     * check if the SSL handshake was successfull, otherwise
     * this is no valid box connection any more
     */
#ifdef HAVE_LIBSSL
     if (ssl && !conn_get_ssl(newconn->conn))
        return NULL;
#endif

    if (ssl)
        info(0, "Client connected from <%s> using SSL", octstr_get_cstr(ip));
    else
        info(0, "Client connected from <%s>", octstr_get_cstr(ip));


    /* XXX TODO: do the hand-shake, baby, yeah-yeah! */

    return newconn;
}



static void run_smsbox(void *arg)
{
    int fd;
    Boxc *newconn;
    long sender;
    Msg *msg;
    List *keys;
    Octstr *key;

    list_add_producer(flow_threads);
    fd = (int)arg;
    newconn = accept_boxc(fd, smsbox_port_ssl);
    if (newconn == NULL) {
	    list_remove_producer(flow_threads);
	    return;
    }
    newconn->incoming = list_create();
    list_add_producer(newconn->incoming);
    newconn->retry = incoming_sms;
    newconn->outgoing = outgoing_sms;
    newconn->sent = dict_create(smsbox_max_pending, NULL);
    newconn->pending = semaphore_create(smsbox_max_pending);

    sender = gwthread_create(boxc_sender, newconn);
    if (sender == -1) {
	    error(0, "Failed to start a new thread, disconnecting client <%s>",
	          octstr_get_cstr(newconn->client_ip));
	    goto cleanup;
    }
    /*
     * We register newconn in the smsbox_list here but mark newconn as routable
     * after identification or first message received from smsbox. So we can avoid
     * a race condition for routable smsboxes (otherwise between startup and
     * registration we will forward some messages to smsbox).
     */
    gw_rwlock_wrlock(smsbox_list_rwlock);
    list_append(smsbox_list, newconn);
    gw_rwlock_unlock(smsbox_list_rwlock);

    list_add_producer(newconn->outgoing);
    boxc_receiver(newconn);
    list_remove_producer(newconn->outgoing);

    /* remove us from smsbox routing list */
    gw_rwlock_wrlock(smsbox_list_rwlock);
    list_delete_equal(smsbox_list, newconn);
    if (newconn->boxc_id) {
        dict_remove(smsbox_by_id, newconn->boxc_id);
    }
    gw_rwlock_unlock(smsbox_list_rwlock);

    /*
     * check if we in the shutdown phase and sms dequeueing thread
     *   has removed the producer already
     */
    if (list_producer_count(newconn->incoming) > 0)
        list_remove_producer(newconn->incoming);

    /* check if we are still waiting for ack's and semaphore locked */
    if (dict_key_count(newconn->sent) >= smsbox_max_pending)
        semaphore_up(newconn->pending); /* allow sender to go down */
        
    gwthread_join(sender);

    /* put not acked msgs into incoming queue */    
    keys = dict_keys(newconn->sent);
    while((key = list_extract_first(keys)) != NULL) {
        msg = dict_remove(newconn->sent, key);
        list_produce(incoming_sms, msg);
        octstr_destroy(key);
    }
    gw_assert(list_len(keys) == 0);
    list_destroy(keys, octstr_destroy_item);

    /* clear our send queue */
    while((msg = list_extract_first(newconn->incoming)) != NULL) {
        list_produce(incoming_sms, msg);
    }

cleanup:
    gw_assert(list_len(newconn->incoming) == 0);
    list_destroy(newconn->incoming, NULL);
    gw_assert(dict_key_count(newconn->sent) == 0);
    dict_destroy(newconn->sent);
    semaphore_destroy(newconn->pending);
    boxc_destroy(newconn);

    /* wakeup the dequeueing thread */
    gwthread_wakeup(sms_dequeue_thread);

    list_remove_producer(flow_threads);
}



static void run_wapbox(void *arg)
{
    int fd;
    Boxc *newconn;
    List *newlist;
    long sender;

    list_add_producer(flow_threads);
    fd = (int)arg;
    newconn = accept_boxc(fd, wapbox_port_ssl);
    if (newconn == NULL) {
	    list_remove_producer(flow_threads);
	    return;
    }
    newconn->is_wap = 1;
    
    /*
     * create a new incoming list for just that box,
     * and add it to list of list pointers, so we can start
     * to route messages to it.
     */

    debug("bb", 0, "setting up systems for new wapbox");
    
    newlist = list_create();
    list_add_producer(newlist);  /* this is released by the 
    	    	    	    	    sender/receiver if it exits */
    
    newconn->incoming = newlist;
    newconn->retry = incoming_wdp;
    newconn->outgoing = outgoing_wdp;

    sender = gwthread_create(boxc_sender, newconn);
    if (sender == -1) {
	    error(0, "Failed to start a new thread, disconnecting client <%s>",
	          octstr_get_cstr(newconn->client_ip));
	    goto cleanup;
    }
    list_append(wapbox_list, newconn);
    list_add_producer(newconn->outgoing);
    boxc_receiver(newconn);

    /* cleanup after receiver has exited */
    
    list_remove_producer(newconn->outgoing);
    list_lock(wapbox_list);
    list_delete_equal(wapbox_list, newconn);
    list_unlock(wapbox_list);

    while (list_producer_count(newlist) > 0)
	    list_remove_producer(newlist);

    newconn->alive = 0;
    
    gwthread_join(sender);

cleanup:
    gw_assert(list_len(newlist) == 0);
    list_destroy(newlist, NULL);
    boxc_destroy(newconn);

    list_remove_producer(flow_threads);
}


/*------------------------------------------------
 * main single thread functions
 */

typedef struct _addrpar {
    Octstr *address;
    int	port;
    int wapboxid;
} AddrPar;

static void ap_destroy(AddrPar *addr)
{
    octstr_destroy(addr->address);
    gw_free(addr);
}

static int cmp_route(void *ap, void *ms)
{
    AddrPar *addr = ap;
    Msg *msg = ms;
    
    if (msg->wdp_datagram.source_port == addr->port  &&
	    octstr_compare(msg->wdp_datagram.source_address, addr->address)==0)
	return 1;

    return 0;
}

static int cmp_boxc(void *bc, void *ap)
{
    Boxc *boxc = bc;
    AddrPar *addr = ap;

    if (boxc->id == addr->wapboxid) return 1;
        return 0;
}

static Boxc *route_msg(List *route_info, Msg *msg)
{
    AddrPar *ap;
    Boxc *conn, *best;
    int i, b, len;
    
    ap = list_search(route_info, msg, cmp_route);
    if (ap == NULL) {
	    debug("bb.boxc", 0, "Did not find previous routing info for WDP, "
	    	  "generating new");
route:

	    if (list_len(wapbox_list) == 0)
	        return NULL;

	    list_lock(wapbox_list);

	/* take random wapbox from list, and then check all wapboxes
	 * and select the one with lowest load level - if tied, the first
	 * one
	 */
	    len = list_len(wapbox_list);
	    b = gw_rand() % len;
	    best = list_get(wapbox_list, b);

	    for(i = 0; i < list_len(wapbox_list); i++) {
	        conn = list_get(wapbox_list, (i+b) % len);
	        if (conn != NULL && best != NULL)
		        if (conn->load < best->load)
		            best = conn;
	    }
	    if (best == NULL) {
	        warning(0, "wapbox_list empty!");
	        list_unlock(wapbox_list);
	        return NULL;
	    }
	    conn = best;
	    conn->load++;	/* simulate new client until we get new values */
	
	    ap = gw_malloc(sizeof(AddrPar));
	    ap->address = octstr_duplicate(msg->wdp_datagram.source_address);
	    ap->port = msg->wdp_datagram.source_port;
	    ap->wapboxid = conn->id;
	    list_produce(route_info, ap);

	    list_unlock(wapbox_list);
    } else
	    conn = list_search(wapbox_list, ap, cmp_boxc);

    if (conn == NULL) {
	/* routing failed; wapbox has disappeared!
	 * ..remove routing info and re-route   */

	    debug("bb.boxc", 0, "Old wapbox has disappeared, re-routing");

	    list_delete_equal(route_info, ap);
	    ap_destroy(ap);
	    goto route;
    }
    return conn;
}


/*
 * this thread listens to incoming_wdp list
 * and then routs messages to proper wapbox
 */
static void wdp_to_wapboxes(void *arg)
{
    List *route_info;
    AddrPar *ap;
    Boxc *conn;
    Msg *msg;
    int i;

    list_add_producer(flow_threads);
    list_add_producer(wapbox_list);

    route_info = list_create();

    
    while(bb_status != BB_DEAD) {

	    list_consume(suspended);	/* block here if suspended */

	    if ((msg = list_consume(incoming_wdp)) == NULL)
	         break;

	    gw_assert(msg_type(msg) == wdp_datagram);

	    conn = route_msg(route_info, msg);
	    if (conn == NULL) {
	        warning(0, "Cannot route message, discard it");
	        msg_destroy(msg);
	        continue;
	    }
	    list_produce(conn->incoming, msg);
    }
    debug("bb", 0, "wdp_to_wapboxes: destroying lists");
    while((ap = list_extract_first(route_info)) != NULL)
	ap_destroy(ap);

    gw_assert(list_len(route_info) == 0);
    list_destroy(route_info, NULL);

    list_lock(wapbox_list);
    for(i=0; i < list_len(wapbox_list); i++) {
	    conn = list_get(wapbox_list, i);
	    list_remove_producer(conn->incoming);
	    conn->alive = 0;
    }
    list_unlock(wapbox_list);

    list_remove_producer(wapbox_list);
    list_remove_producer(flow_threads);
}


static void wait_for_connections(int fd, void (*function) (void *arg), 
    	    	    	    	 List *waited)
{
    int ret;
    int timeout = 10; /* 10 sec. */

    gw_assert(function != NULL);
    
    while(bb_status != BB_DEAD) {

	/* if we are being shutdowned, as long as there is
	 * messages in incoming list allow new connections, but when
	 * list is empty, exit.
         * Note: We have timeout (defined above) for which we allow new connections.
         *           Otherwise we wait here for ever!
	 */
	    if (bb_status == BB_SHUTDOWN) {
	        ret = list_wait_until_nonempty(waited);
	        if (ret == -1 || !timeout)
                    break;
                else
                    timeout--;
	    }

            /* block here if suspended */
            list_consume(suspended);

            ret = gwthread_pollfd(fd, POLLIN, 1.0);
	    if (ret > 0) {
	        gwthread_create(function, (void *)fd);
	        gwthread_sleep(1.0);
	    } else if (ret < 0) {
	        if(errno==EINTR) continue;
	        if(errno==EAGAIN) continue;
	        error(errno, "wait_for_connections failed");
	    }
    }
}



static void smsboxc_run(void *arg)
{
    int fd;
    int port;

    list_add_producer(flow_threads);
    gwthread_wakeup(MAIN_THREAD_ID);
    port = (int)arg;
    
    fd = make_server_socket(port, NULL); 
    	/* XXX add interface_name if required */

    if (fd < 0) {
	    panic(0, "Could not open smsbox port %d", port);
    }

    /*
     * infinitely wait for new connections;
     * to shut down the system, SIGTERM is send and then
     * select drops with error, so we can check the status
     */

    wait_for_connections(fd, run_smsbox, incoming_sms);

    list_remove_producer(smsbox_list);

    /* continue avalanche */
    list_remove_producer(outgoing_sms);

    /* all connections do the same, so that all must remove() before it
     * is completely over
     */

    while(list_wait_until_nonempty(smsbox_list) == 1)
        gwthread_sleep(1.0);

    /* close listen socket */
    close(fd);

    gwthread_wakeup(sms_dequeue_thread);
    gwthread_join(sms_dequeue_thread);

    list_destroy(smsbox_list, NULL);
    smsbox_list = NULL;
    gw_rwlock_destroy(smsbox_list_rwlock);
    smsbox_list_rwlock = NULL;

    /* destroy things related to smsbox routing */
    dict_destroy(smsbox_by_id);
    smsbox_by_id = NULL;
    dict_destroy(smsbox_by_smsc);
    smsbox_by_smsc = NULL;
    dict_destroy(smsbox_by_receiver);
    smsbox_by_receiver = NULL;
    
    list_remove_producer(flow_threads);
}


static void wapboxc_run(void *arg)
{
    int fd, port;

    list_add_producer(flow_threads);
    gwthread_wakeup(MAIN_THREAD_ID);
    port = (int)arg;
    
    fd = make_server_socket(port, NULL);
    	/* XXX add interface_name if required */

    if (fd < 0) {
	    panic(0, "Could not open wapbox port %d", port);
    }

    wait_for_connections(fd, run_wapbox, incoming_wdp);

    /* continue avalanche */

    list_remove_producer(outgoing_wdp);


    /* wait for all connections to die and then remove list
     */
    
    while(list_wait_until_nonempty(wapbox_list) == 1)
        gwthread_sleep(1.0);

    /* wait for wdp_to_wapboxes to exit */
    while(list_consume(wapbox_list)!=NULL)
	;
    
    /* close listen socket */
    close(fd);   
 
    list_destroy(wapbox_list, NULL);
    wapbox_list = NULL;
    
    list_remove_producer(flow_threads);
}


/*
 * Populates the corresponding smsbox_by_foobar dictionary hash tables
 */
static void init_smsbox_routes(Cfg *cfg)
{
    CfgGroup *grp;
    List *list, *items;
    Octstr *boxc_id, *smsc_ids, *shortcuts;
    int i;

    boxc_id = smsc_ids = shortcuts = NULL;

    list = cfg_get_multi_group(cfg, octstr_imm("smsbox-route")); 
 
    /* loop multi-group "smsbox-route" */
    while (list && (grp = list_extract_first(list)) != NULL) { 
         
        if ((boxc_id = cfg_get(grp, octstr_imm("smsbox-id"))) == NULL) { 
            grp_dump(grp); 
            panic(0,"'smsbox-route' group without valid 'smsbox-id' directive!"); 
        }

        /*
         * If smsc-ids are given, then any message comming from the specified
         * smsc-id will be routed to this smsbox instance.
         * If shortcuts are given, then any message with receiver number 
         * matching those will be routed to this smsbox instance.
         */
        smsc_ids = cfg_get(grp, octstr_imm("smsc-ids"));
        shortcuts = cfg_get(grp, octstr_imm("shortcuts"));

        /* now parse the smsc-ids and shortcuts semicolon separated list */
        if (smsc_ids) {
            items = octstr_split(smsc_ids, octstr_imm(";"));
            for (i = 0; i < list_len(items); i++) {
                Octstr *item = list_get(items, i);
                octstr_strip_blanks(item);

                debug("bb.boxc",0,"Adding smsbox routing to id <%s> for smsc id <%s>",
                      octstr_get_cstr(boxc_id), octstr_get_cstr(item));

                if (!dict_put_once(smsbox_by_smsc, item, octstr_duplicate(boxc_id)))
                    panic(0, "Routing for smsc-id <%s> already exists!",
                          octstr_get_cstr(item));
            }
            list_destroy(items, octstr_destroy_item);
            octstr_destroy(smsc_ids);
        }

        if (shortcuts) {
            items = octstr_split(shortcuts, octstr_imm(";"));
            for (i = 0; i < list_len(items); i++) {
                Octstr *item = list_get(items, i);
                octstr_strip_blanks(item);

                debug("bb.boxc",0,"Adding smsbox routing to id <%s> for receiver no <%s>",
                      octstr_get_cstr(boxc_id), octstr_get_cstr(item));
            
                if (!dict_put_once(smsbox_by_receiver, item, octstr_duplicate(boxc_id)))
                    panic(0, "Routing for receiver no <%s> already exists!",
                          octstr_get_cstr(item));
            }
            list_destroy(items, octstr_destroy_item);
            octstr_destroy(shortcuts);
        }
        octstr_destroy(boxc_id);
    }

    list_destroy(list, NULL);
}


/*-------------------------------------------------------------
 * public functions
 *
 * SMSBOX
 */

int smsbox_start(Cfg *cfg)
{
    CfgGroup *grp;
    
    if (smsbox_running) return -1;

    debug("bb", 0, "starting smsbox connection module");

    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    if (cfg_get_integer(&smsbox_port, grp, octstr_imm("smsbox-port")) == -1) {
	    error(0, "Missing smsbox-port variable, cannot start smsboxes");
	    return -1;
    }
#ifdef HAVE_LIBSSL
    cfg_get_bool(&smsbox_port_ssl, grp, octstr_imm("smsbox-port-ssl"));
#endif /* HAVE_LIBSSL */

    if (smsbox_port_ssl)
        debug("bb", 0, "smsbox connection module is SSL-enabled");
        
    if (cfg_get_integer(&smsbox_max_pending, grp, octstr_imm("smsbox-max-pending")) == -1) {
        smsbox_max_pending = SMSBOX_MAX_PENDING;
        info(0, "BOXC: 'smsbox-max-pending' not set, using default (%ld).", smsbox_max_pending);
    }
    
    smsbox_list = list_create();	/* have a list of connections */
    smsbox_list_rwlock = gw_rwlock_create();
    if (!boxid)
        boxid = counter_create();

    /* the smsbox routing specific inits */
    smsbox_by_id = dict_create(10, NULL);  /* and a hash directory of identified */
    smsbox_by_smsc = dict_create(30, (void(*)(void *)) octstr_destroy);
    smsbox_by_receiver = dict_create(50, (void(*)(void *)) octstr_destroy);

    /* load the defined smsbox routing rules */
    init_smsbox_routes(cfg);

    list_add_producer(outgoing_sms);
    list_add_producer(smsbox_list);

    smsbox_running = 1;
    
    if ((sms_dequeue_thread = gwthread_create(sms_to_smsboxes, NULL)) == -1)
 	    panic(0, "Failed to start a new thread for smsbox routing");

    if (gwthread_create(smsboxc_run, (void *)smsbox_port) == -1)
	    panic(0, "Failed to start a new thread for smsbox connections");

    return 0;
}


int smsbox_restart(Cfg *cfg)
{
    if (!smsbox_running) return -1;
    
    /* send new config to clients */

    return 0;
}



/* WAPBOX */

int wapbox_start(Cfg *cfg)
{
    CfgGroup *grp;

    if (wapbox_running) return -1;

    debug("bb", 0, "starting wapbox connection module");
    
    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    
    if (cfg_get_integer(&wapbox_port, grp, octstr_imm("wapbox-port")) == -1) {
	    error(0, "Missing wapbox-port variable, cannot start WAP");
	    return -1;
    }
#ifdef HAVE_LIBSSL
    cfg_get_bool(&wapbox_port_ssl, grp, octstr_imm("wapbox-port-ssl"));
#endif /* HAVE_LIBSSL */
  
    box_allow_ip = cfg_get(grp, octstr_imm("box-allow-ip"));
    if (box_allow_ip == NULL)
    	box_allow_ip = octstr_create("");
    box_deny_ip = cfg_get(grp, octstr_imm("box-deny-ip"));
    if (box_deny_ip == NULL)
    	box_deny_ip = octstr_create("");
    if (box_allow_ip != NULL && box_deny_ip == NULL)
	    info(0, "Box connection allowed IPs defined without any denied...");
    
    wapbox_list = list_create();	/* have a list of connections */
    list_add_producer(outgoing_wdp);
    if (!boxid)
        boxid = counter_create();

    if (gwthread_create(wdp_to_wapboxes, NULL) == -1)
 	    panic(0, "Failed to start a new thread for wapbox routing");
 
    if (gwthread_create(wapboxc_run, (void *)wapbox_port) == -1)
	    panic(0, "Failed to start a new thread for wapbox connections");

    wapbox_running = 1;
    return 0;
}


Octstr *boxc_status(int status_type)
{
    Octstr *tmp;
    char *lb, *ws;
    int i, boxes, para = 0;
    time_t orig, t;
    Boxc *bi;

    orig = time(NULL);

    /*
     * XXX: this will cause segmentation fault if this is called
     *    between 'destroy_list and setting list to NULL calls.
     *    Ok, this has to be fixed, but now I am too tired.
     */
    
    if ((lb = bb_status_linebreak(status_type))==NULL)
	    return octstr_create("Un-supported format");

    if (status_type == BBSTATUS_HTML)
	    ws = "&nbsp;&nbsp;&nbsp;&nbsp;";
    else if (status_type == BBSTATUS_TEXT)
	    ws = "    ";
    else
	    ws = "";

    if (status_type == BBSTATUS_HTML || status_type == BBSTATUS_WML)
	    para = 1;
    
    if (status_type == BBSTATUS_XML) {
        tmp = octstr_create ("");
        octstr_append_cstr(tmp, "<boxes>\n\t");
    }
    else
        tmp = octstr_format("%sBox connections:%s", para ? "<p>" : "", lb);
    boxes = 0;
    
    if (wapbox_list) {
	    list_lock(wapbox_list);
	    for(i=0; i < list_len(wapbox_list); i++) {
	        bi = list_get(wapbox_list, i);
	        if (bi->alive == 0)
		        continue;
	        t = orig - bi->connect_time;
            if (status_type == BBSTATUS_XML)
	            octstr_format_append(tmp,
		        "<box>\n\t\t<type>wapbox</type>\n\t\t<IP>%s</IP>\n"
                "\t\t<status>on-line %ldd %ldh %ldm %lds</status>\n"
                "\t\t<ssl>%s</ssl>\n\t</box>\n",
				octstr_get_cstr(bi->client_ip),
				t/3600/24, t/3600%24, t/60%60, t%60,
#ifdef HAVE_LIBSSL
                conn_get_ssl(bi->conn) != NULL ? "yes" : "no"
#else 
                "not installed"
#endif
                );
            else
	            octstr_format_append(tmp,
		        "%swapbox, IP %s (on-line %ldd %ldh %ldm %lds) %s %s",
				ws, octstr_get_cstr(bi->client_ip),
				t/3600/24, t/3600%24, t/60%60, t%60, 
#ifdef HAVE_LIBSSL
                conn_get_ssl(bi->conn) != NULL ? "using SSL" : "",
#else
                "",
#endif 
                lb);
	            boxes++;
	       }
	       list_unlock(wapbox_list);
        }
        if (smsbox_list) {
            gw_rwlock_rdlock(smsbox_list_rwlock);
	    for(i=0; i < list_len(smsbox_list); i++) {
	        bi = list_get(smsbox_list, i);
	        if (bi->alive == 0)
		        continue;
	        t = orig - bi->connect_time;
            if (status_type == BBSTATUS_XML)
	            octstr_format_append(tmp, "<box>\n\t\t<type>smsbox</type>\n"
                    "\t\t<id>%s</id>\n\t\t<IP>%s</IP>\n"
                    "\t\t<queue>%ld</queue>\n"
                    "\t\t<status>on-line %ldd %ldh %ldm %lds</status>\n"
                    "\t\t<ssl>%s</ssl>\n\t</box>",
                    (bi->boxc_id ? octstr_get_cstr(bi->boxc_id) : ""),
		            octstr_get_cstr(bi->client_ip),
		            list_len(bi->incoming) + dict_key_count(bi->sent),
		            t/3600/24, t/3600%24, t/60%60, t%60,
#ifdef HAVE_LIBSSL
                    conn_get_ssl(bi->conn) != NULL ? "yes" : "no"
#else 
                    "not installed"
#endif
                    );
            else
                octstr_format_append(tmp, "%ssmsbox:%s, IP %s (%ld queued), (on-line %ldd %ldh %ldm %lds) %s %s",
                    ws, (bi->boxc_id ? octstr_get_cstr(bi->boxc_id) : "(none)"), 
                    octstr_get_cstr(bi->client_ip), list_len(bi->incoming) + dict_key_count(bi->sent),
		            t/3600/24, t/3600%24, t/60%60, t%60, 
#ifdef HAVE_LIBSSL
                    conn_get_ssl(bi->conn) != NULL ? "using SSL" : "",
#else
                    "",
#endif 
                    lb);
	       boxes++;
	    }
	    gw_rwlock_unlock(smsbox_list_rwlock);
    }
    if (boxes == 0 && status_type != BBSTATUS_XML) {
	    octstr_destroy(tmp);
	    tmp = octstr_format("%sNo boxes connected", para ? "<p>" : "");
    }
    if (para)
	    octstr_append_cstr(tmp, "</p>");
    if (status_type == BBSTATUS_XML)
        octstr_append_cstr(tmp, "</boxes>\n");
    else
        octstr_append_cstr(tmp, "\n\n");
    return tmp;
}


int boxc_incoming_wdp_queue(void)
{
    int i, q = 0;
    Boxc *boxc;
    
    if (wapbox_list) {
	    list_lock(wapbox_list);
	    for(i=0; i < list_len(wapbox_list); i++) {
	        boxc = list_get(wapbox_list, i);
	        q += list_len(boxc->incoming);
	    }
	    list_unlock(wapbox_list);
    }
    return q;
}


void boxc_cleanup(void)
{
    octstr_destroy(box_allow_ip);
    octstr_destroy(box_deny_ip);
    box_allow_ip = NULL;
    box_deny_ip = NULL;
    counter_destroy(boxid);
    boxid = NULL;
}


/*
 * Route the incoming message to one of the following input queues:
 *   a specific smsbox conn
 *   a random smsbox conn if no shortcut routing and msg->sms.boxc_id match
 *
 * BEWARE: All logic inside here should be fast, hence speed processing
 * optimized, because every single MO message passes this function and we 
 * have to ensure that no unncessary overhead is done.
 */
int route_incoming_to_boxc(Msg *msg)
{
    Boxc *bc = NULL, *best = NULL;
    Octstr *s, *r;
    long len, b, i;
    int full_found = 0;

    s = r = NULL;
    gw_assert(msg_type(msg) == sms);

    /* msg_dump(msg, 0); */

    /* 
     * We have a specific route to pass this msg to smsbox-id 
     * Lookup the connection in the dictionary.
     */
    gw_rwlock_rdlock(smsbox_list_rwlock);
    if (list_len(smsbox_list) == 0) {
        gw_rwlock_unlock(smsbox_list_rwlock);
    	warning(0, "smsbox_list empty!");
        if (max_incoming_sms_qlength < 0 || max_incoming_sms_qlength > list_len(incoming_sms)) {
            list_produce(incoming_sms, msg);
	    return 0;
         }
         else
             return -1;
    }

    if (octstr_len(msg->sms.boxc_id) > 0) {
        
        bc = dict_get(smsbox_by_id, msg->sms.boxc_id);
        if (bc == NULL) {
            /*
             * something is wrong, this was the smsbox connection we used
             * for sending, so it seems this smsbox is gone
             */
            warning(0,"Could not route message to smsbox id <%s>, smsbox is gone!",
                  octstr_get_cstr(msg->sms.boxc_id));
        }
    }
    else {
        /*
         * Check if we have a "smsbox-route" for this msg.
         * Where the shortcut route has a higher priority then the smsc-id rule.
         */
        s = (msg->sms.smsc_id ? dict_get(smsbox_by_smsc, msg->sms.smsc_id) : NULL);
        r = (msg->sms.receiver ? dict_get(smsbox_by_receiver, msg->sms.receiver) : NULL);
        bc = r ? dict_get(smsbox_by_id, r) : (s ? dict_get(smsbox_by_id, s) : NULL);
    }

    /* check if we found our routing */
    if (bc != NULL) {
        if (max_incoming_sms_qlength < 0 || max_incoming_sms_qlength > list_len(bc->incoming)) {
            list_produce(bc->incoming, msg);
            gw_rwlock_unlock(smsbox_list_rwlock);
            return 1; /* we are done */
        }
        else {
            gw_rwlock_unlock(smsbox_list_rwlock);
            return -1;
        }
    }
    else if (s != NULL || r != NULL || octstr_len(msg->sms.boxc_id) > 0) {
        gw_rwlock_unlock(smsbox_list_rwlock);
        /*
         * we have routing defined, but no smsbox connected at the moment.
         * put msg into global incoming queue and wait until smsbox with
         * such boxc_id connected.
         */
        if (max_incoming_sms_qlength < 0 || max_incoming_sms_qlength > list_len(incoming_sms)) {
            list_produce(incoming_sms, msg);
            return 0;
        }
        else
            return -1;
    }

    /*
     * ok, none of the routing things applied previously, so route it to
     * a random smsbox.
     */

    /* take random smsbox from list, and then check all smsboxes
     * and select the one with lowest load level - if tied, the first
     * one
     */
    len = list_len(smsbox_list);
    b = gw_rand() % len;

    for(i = 0; i < list_len(smsbox_list); i++) {
	bc = list_get(smsbox_list, (i+b) % len);

        if (bc->boxc_id != NULL || bc->routable == 0)
            bc = NULL;

        if (bc != NULL && max_incoming_sms_qlength > 0 &&
            list_len(bc->incoming) > max_incoming_sms_qlength) {
            full_found = 1;
            bc = NULL;
        }

        if ((bc != NULL && best != NULL && bc->load < best->load) ||
             (bc != NULL && best == NULL)) {
	    best = bc;
        }
    }

    if (best != NULL) {
        best->load++;
        list_produce(best->incoming, msg);
    }

    gw_rwlock_unlock(smsbox_list_rwlock);

    if (best == NULL && full_found == 0) {
	warning(0, "smsbox_list empty!");
        if (max_incoming_sms_qlength < 0 || max_incoming_sms_qlength > list_len(incoming_sms)) {
            list_produce(incoming_sms, msg);
	    return 0;
         }
         else
             return -1;
    }
    else if (best == NULL && full_found == 1)
        return -1;

    return 1;
}

static void sms_to_smsboxes(void *arg)
{
    Msg *newmsg, *startmsg, *msg;
    long i, len;
    int ret = -1;
    Boxc *boxc;

    list_add_producer(flow_threads);

    newmsg = startmsg = msg = NULL;

    while(bb_status != BB_DEAD) {

        if (newmsg == startmsg) {
            /* check if we are in shutdown phase */
            if (list_producer_count(smsbox_list) == 0)
                break;

            if (ret == 0 || ret == -1) {
                /* debug("", 0, "time to sleep"); */
                gwthread_sleep(60.0);
                /* debug("", 0, "wake up list len %ld", list_len(incoming_sms)); */
                /* shutdown ? */
                if (list_producer_count(smsbox_list) == 0 && list_len(smsbox_list) == 0)
                    break;
            }
            startmsg = msg = list_consume(incoming_sms);
            /* debug("", 0, "list_consume done 1"); */
            newmsg = NULL;
        }
        else {
            newmsg = msg = list_consume(incoming_sms);
        }

        if (msg == NULL)
            break;

        gw_assert(msg_type(msg) == sms);

        /* debug("bb.sms", 0, "sms_boxc_router: handling message (%p vs %p)",
	          msg, startmsg); */

        ret = route_incoming_to_boxc(msg);
        if (ret == 1)
            startmsg = newmsg = NULL;
        else if (ret == -1) {
            list_produce(incoming_sms, msg);
        }
    }

    gw_rwlock_rdlock(smsbox_list_rwlock);
    len = list_len(smsbox_list);
    for (i=0; i < len; i++) {
        boxc = list_get(smsbox_list, i);
        list_remove_producer(boxc->incoming);
    }
    gw_rwlock_unlock(smsbox_list_rwlock);

    list_remove_producer(flow_threads);
}
