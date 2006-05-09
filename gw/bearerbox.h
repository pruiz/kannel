/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2005 Kannel Group  
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
 * bearerbox.h
 *
 * General typedefs and functions for bearerbox
 */

#include "gwlib/gwlib.h"
#include "msg.h"
#include "smscconn.h"


/* general bearerbox state */

enum {
    BB_RUNNING = 0,
    BB_ISOLATED = 1,	/* do not receive new messgaes from UDP/SMSC */
    BB_SUSPENDED = 2,	/* do not transfer any messages */
    BB_SHUTDOWN = 3,
    BB_DEAD = 4,
    BB_FULL = 5         /* message queue too long, do not accept new messages */
};


/* type of output given by various status functions */
enum {
    BBSTATUS_HTML = 0,
    BBSTATUS_TEXT = 1,
    BBSTATUS_WML = 2,
    BBSTATUS_XML = 3
};

/*---------------------------------------------------------------
 * Module interface to core bearerbox
 *
 * Modules implement one or more of the following interfaces:
 *
 * XXX_start(Cfg *config) - start the module
 * XXX_restart(Cfg *config) - restart the module, according to new config
 * XXX_shutdown() - start the avalanche - started from UDP/SMSC
 * XXX_die() - final cleanup
 *
 * XXX_addwdp() - only for SMSC/UDP: add a new WDP message to outgoing system
 */


/*---------------
 * bb_boxc.c (SMS and WAPBOX connections)
 */

int smsbox_start(Cfg *config);
int smsbox_restart(Cfg *config);

int wapbox_start(Cfg *config);

Octstr *boxc_status(int status_type);
/* tell total number of messages in separate wapbox incoming queues */
int boxc_incoming_wdp_queue(void);

/* Clean up after box connections have died. */
void boxc_cleanup(void);

/*
 * Route the incoming message to one of the following input queues:
 *   a specific smsbox conn
 *   a random smsbox conn if no shortcut routing and msg->sms.boxc_id match.
 * @return -1 if incoming queue full; 0 otherwise.
 */
int route_incoming_to_boxc(Msg *msg);


/*---------------
 * bb_udp.c (UDP receiver/sender)
 */

int udp_start(Cfg *config);
/* int udp_restart(Cfg *config); */
int udp_shutdown(void);
int udp_die(void);	/* called when router dies */

/* add outgoing WDP. If fails, return -1 and msg is untouched, so
 * caller must think of new uses for it */
int udp_addwdp(Msg *msg);
/* tell total number of messages in separate UDP outgoing port queues */
int udp_outgoing_queue(void);



/*---------------
 * bb_smscconn.c (SMS Center connections)
 */

int smsc2_start(Cfg *config);
int smsc2_restart(Cfg *config);

void smsc2_suspend(void);    /* suspend (can still send but not receive) */
void smsc2_resume(void);     /* resume */
int smsc2_shutdown(void);
void smsc2_cleanup(void); /* final clean-up */

Octstr *smsc2_status(int status_type);
/* Route message to SMSC. If finds a good one, puts into it and returns 1
 * If finds only bad ones, but acceptable, queues and returns 0
 * (like all acceptable currently disconnected)
 * If cannot find nothing at all, returns -1 and message is NOT destroyed
 * (otherwise it is) */
int smsc2_rout(Msg *msg);

int smsc2_stop_smsc(Octstr *id);   /* shutdown a specific smsc */
int smsc2_restart_smsc(Octstr *id);  /* re-start a specific smsc */


/*---------------
 * bb_http.c (HTTP Admin)
 */

int httpadmin_start(Cfg *config);
/* int http_restart(Cfg *config); */
void httpadmin_stop(void);


/*-----------------
 * bb_store.c (SMS storing/retrieval functions)
 */

/* return number of SMS messages in current store (file) */
long store_messages(void);

/* assign ID and save given message to store. Return -1 if save failed */
int store_save(Msg *msg);

/*
 * Store ack/nack to the store file for a given message with a given status.
 * @return: -1 if save failed ; 0 otherwise.
 */
int store_save_ack(Msg *msg, ack_status_t status);

/* load store from file; delete any messages that have been relayed,
 * and create a new store file from remaining. Calling this function
 * might take a while, depending on store size
 * Return -1 if something fails (bb can then PANIC normally)
 */
int store_load(void(*receive_msg)(Msg*));

/* dump currently non-acknowledged messages into file. This is done
 * automatically now and then, but can be forced. Return -1 if file
 * problems
 */
int store_dump(void);

/* initialize system. Return -1 if fname is baad (too long), otherwise
 * load data from disk. dump_freq is preferred delay between each disk dump,
 * in seconds. */
#define BB_STORE_DEFAULT_DUMP_FREQ 10
int store_init(const Octstr *fname, long dump_freq);

/* init shutdown (system dies when all acks have been processed) */
void store_shutdown(void);

/* return all containing messages in the current store */
Octstr *store_status(int status_type);


/*-----------------
 * bb_alog.c (Custom access-log format handling)
 */

/* passes the access-log-format string from config to the module */
void bb_alog_init(const Octstr *format);

/* cleanup for internal things */
void bb_alog_shutdown(void);

/* called from bb_smscconn.c to log the various access-log events */
void bb_alog_sms(SMSCConn *conn, Msg *sms, const char *message);



/*----------------------------------------------------------------
 * Core bearerbox public functions;
 * used only via HTTP adminstration
 */

int bb_shutdown(void);
int bb_isolate(void);
int bb_suspend(void);
int bb_resume(void);
int bb_restart(void);
int bb_flush_dlr(void);
int bb_stop_smsc(Octstr *id);
int bb_restart_smsc(Octstr *id);

/* return string of current status */
Octstr *bb_print_status(int status_type);


/*----------------------------------------------------------------
 * common function to all (in bearerbox.c)
 */

/* return linebreak for given output format, or NULL if format
 * not supported */
char *bb_status_linebreak(int status_type);


