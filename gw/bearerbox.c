/*
 * Bearer box
 *
 * (WAP/SMS) Gateway
 *
 * Kalle Marjola 1999 for Wapit ltd.
 */

/*
 * Bearer box is the router/load balance part of the Gateway.
 *
 * It's responsibility is to connect SMS Centers, open ports for
 * CSD Routers and accept connections from SMS and WAP Boxes.
 *
 * It receivers SMS Messages and WAP datagrams which it queues and
 * sends to appropriate boxes. It receives replies from SMS and
 * WAP Boxes and sends them via SMS Centers and CSD Router connection
 *
 * The bearerbox is multi-threaded application.
 *
 * - Main thread handles all heartbeat checks and accepts new Box and
 *   HTTP connections.
 *
 * - SMSC threads connect specified SMS Center and receive and send
 *   messages concerning it
 *
 * - CSDR thread listens to WAP WDP packets, and similarly sends them
 *
 * - SMS BOX Connection does all the required data transfer with one
 *   sms box. On thread is started for each SMS BOX Connection.
 *
 * - WAP BOX Connection is similar to SMS BO Connection but handles
 *   all the required wap messages between WAP Boxes and Bearerbox
 *
 * - HTTP request thread is started for each HTTP admin command
 *
 */


#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>

#include "gwlib.h"

#include "cgi.h"
#include "urltrans.h"

#include "bb.h"
#include "msg.h"
#include "bb_msg.h"

#include "smsc.h"
#include "csdr.h"
#include "boxc.h"
#include "smsbox_req.h"

/* bearer box thread types */
enum {
    BB_TTYPE_SMSC,
    BB_TTYPE_CSDR,
    BB_TTYPE_SMS_BOX,
    BB_TTYPE_WAP_BOX,
};

enum {
    BB_STATUS_CREATED,		/* created but not functioning, yet */
    BB_STATUS_OK,
    BB_STATUS_SUSPENDED,	/* reserved for future use */
    BB_STATUS_KILLED,		/* main program or other has killed */
    BB_STATUS_DEAD,		/* thread itself has died, can be removed */
};

typedef struct bb_t {
    int	type;
    int id;		/* id number */
    sig_atomic_t status;

    pthread_t thread;

    SMSCenter *smsc;   	/* if SMSC thread */
    CSDRouter *csdr;   	/* if CSD Router thread */
    BOXC *boxc;		/* if SMS/WAP box */

    time_t heartbeat;	/* last update of our status */

    ConfigGroup *grp;	/* configuration */
} BBThread;






/*
 * Bearer box main object
 * (this could be a list of global variables, but I like it this way)
 */

typedef struct bb_s {
    BBThread **threads;
    int num_threads;
    int thread_limit;

    int id_max;

    RQueue	*request_queue;
    RQueue	*reply_queue;

    int max_queue;		/* limit to queue length. If reached, no
				 * further messages accepted until room available
				 */
    
    float	mean_req_ql;  	/* mean request queue length */
    float	mean_rep_ql;  	/* mean reply queue length */

    sig_atomic_t	abort_program;	/* 0 nothing, 1 not receiving new, 2 queus
					 * emptied */

    sig_atomic_t	suspended;
    sig_atomic_t	accept_pending;

    int	heartbeat_freq;		/* basic heartbeat writing frequency
				 * (in seconds) - double this and we kill */
    int http_port;		/* adminstration port */
    int wapbox_port;		/* wap box port */
    int smsbox_port;		/* sms box port */

    char *allow_ip;		/* hosts allowed to connect */
    char *deny_ip;		/* hosts denied to connect */
    
    char *admin_username;	/* for HTTP-adminstration */
    char *admin_password;	/* ditto */


    int http_fd, wap_fd, sms_fd;	/* socket FDs */
    
    char *pid_file;		/* our pid */
    char *global_prefix;	/* global normalization string */
    
    Mutex *mutex;	/* for heartbeat/structure update */
} BearerBox;


static BearerBox *bbox = NULL;
static int http_sendsms_fd = -1;
static char *http_sendsms_allow_ip = NULL;
static char *http_sendsms_deny_ip = NULL;

static time_t start_time;


/*--------------------------------------------------------
 * FORWARD DECLARATIONS
 */

static BBThread *internal_smsbox(void);
static void print_threads(char *buffer);
static void print_queues(char *buffer);



/*--------------------------------------------------------------------
 * UTILITIES
 */

/*
 * routing information for (WAP) traffic
 *
 * note that this is a stupid linear table, we should use
 * a sorted list so that we can use a binary search or something...
 */

Mutex *route_mutex;

typedef struct routeinfo {
    char *route_match;
    int receiver_id;
    time_t tag;		/* last time used */
} RouteInfo;

static RouteInfo *route_info = NULL;
static int route_count = 0;
static int route_limit = 0;

/*----------------------------------------------------------------
 * WAP datagram routing routines
 */


/*
 * this is 'normal' binary search except that index of last compare
 * is inserted into -1
 *
 * Return 0 if last matched, <0 or >0 if not. '*i' is set as last comprasion
 * index
 */
static int bsearch_receiver(char *str, int *index)
{
    int lo = 1;
    int hi = route_count;
    int cmp = -1;
    *index = -1;

    while(hi >= lo) {
	*index = (lo+hi)/2 - 1;
	cmp = strcmp(str, route_info[*index].route_match);
	if (cmp == 0) return 0;
	if (cmp < 0) hi = *index;
	else lo = *index + 2;
    }
    return cmp;
}

/*
 * find receiver with matching routing string.
 * return id of the receiver, or -1 if not found
 */
static int find_receiver(RQueueItem *rqi)
{
    int i, ret;
    
    if (rqi->routing_info == NULL)
	return -1;

    mutex_lock(route_mutex);
    ret = bsearch_receiver(rqi->routing_info, &i);
    if (ret == 0)
	route_info[i].tag = time(NULL);
    
    mutex_unlock(route_mutex);
    if (ret != 0)
	return -1;
    
    return route_info[i].receiver_id;
}

/*
 * delete given receiver from table. Return number of addresses removed
 */
static int del_receiver_id(int id)
{
    int index;
    int del = 0;
    
    mutex_lock(route_mutex);

    for(index=0; index < route_count; index++) {
	if (route_info[index].receiver_id == id) {
	    memmove(route_info+index, route_info+index+1, route_count-index-1);
	    route_count--;
	    index--;
	    del++;
	}
    }
    mutex_unlock(route_mutex);
    return del;
}

/*
 * add receiver of the given string and link it to given 'id'
 */
static int add_receiver(RQueue *queue, RQueueItem *msg, int old_id, int new_id)
{
    RouteInfo *new;
    int ret, index;
    char *p;

    p = strdup(msg->routing_info);
    if (p == NULL) {
	error(errno, "Failed to allocate room for router");
	return -1;
    }
    mutex_lock(route_mutex);

    ret = bsearch_receiver(msg->routing_info, &index);
    if (ret == 0) {
	warning(0, "Trying to re-insert already known");
	free(p);
	goto end;
    }
    /*
     * reallocate more room if needed
     */
    
    if (route_count >= route_limit) {
	route_limit += 1024;
	new = realloc(route_info, route_limit * sizeof(RouteInfo));
	if (new == NULL) {
	    error(errno, "Failed to add a new receiver routing info");
	    ret = -1;
	    goto end;
	}
	route_info = new;
    }
    /*
     * insert the new one
     */
    if (ret < 0) {
	memmove(route_info+index+2, route_info+index+1, route_count-index-1);
	index++;
    } else
	memmove(route_info+index+1, route_info+index, route_count-index);
    
    route_info[index].route_match = p;
    route_info[index].receiver_id = new_id;
    route_info[index].tag = time(NULL);
    route_count++;

    /*
     * we 'steal' all messages with identical routing str 
     */
    rq_change_destination(queue, msg->msg_class, msg->msg_type,
			  msg->routing_info, old_id, new_id);
	
    ret = 0;
end:
    mutex_unlock(route_mutex);

    return ret;
}

/*
 * check routing list and delete all that are older than
 * 10 minutes (600 seconds) (non-used for that period)
 *
 * Return number of references deleted
 */
static int check_receivers()
{
    int i;
    int tot = 0;
    time_t now;

    mutex_lock(route_mutex);

    now = time(NULL);
    
    for(i=0; i < route_count; i++)
	if (route_info[i].tag + 600 < now) {
	    memmove(route_info+i, route_info+i+1, route_count-i-1);
	    route_count--;
	    i--;
	    tot++;
	}
    mutex_unlock(route_mutex);
    return tot;
}

    


/* route received message; mainly used to find a corresponding SMSC/CSDR
 * to MT message; ACK/NACK already should know the receiver
 *
 * return 0 on success, -1 on failure
 */
static int route_msg(BBThread *bbt, RQueueItem *msg)
{
    BBThread *thr;
    int i, ret, backup = -1;
    int backup_backup = -1;
    int bad_choice = -1;

    if (msg->source > -1)	/* if we have already routed message */
	return 0;
    if (bbt != NULL)
	msg->source = bbt->id;
    else
	msg->source = -1;	/* unknown */

    if (msg->msg_type == R_MSG_TYPE_MO) {
	if (msg->msg_class == R_MSG_CLASS_SMS) {
	    msg->destination= -1;
	    return 0;	      	/* no direct destination, leave it
				 * to load balancing functions */
	}
	/* WAP
	* .. if a WAP is connectionless, send to any wap box.. so
	* need to set the destination. But if the message is connectioned
	* session, we must route all to same wap box... */

	msg->destination = find_receiver(msg);
	/*
	 * note that if we did NOT find a receiver (-1 returned)
	 * we do not care but leave that to wapbox connection to take
	 * care of
	 */
	return 0; 
    }
    /* if we have gone this far, this must be a mobile terminated
     * (new) message from sms/wap box to SMSC/CSD Router
     *
     * This means that we must use some heurastics to find to which
     * one we will send this...
     */

    /* possible bottleneck? deal with this later */
    
    mutex_lock(bbox->mutex);	

    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL) {
	    if ((thr->type == BB_TTYPE_SMSC ||
		thr->type == BB_TTYPE_CSDR)
		&&
		thr->status != BB_STATUS_DEAD) {

		/* Route WAP according to CSDR port and IP
		 */
		if (msg->msg_class == R_MSG_CLASS_WAP &&
		    thr->type == BB_TTYPE_CSDR &&
		    (thr->status == BB_STATUS_OK ||
		     thr->status == BB_STATUS_CREATED)) {

		    if (csdr_is_to_us(thr->csdr, msg->msg) == 1) { 
			msg->destination = thr->id;
			break;
		    }
		}
		if (thr->type == BB_TTYPE_SMSC)
		    ret = smsc_receiver(thr->smsc,
		       octstr_get_cstr(msg->msg->smart_sms.receiver));
		else
		    ret = 0;
		
		if (ret == 1) {
		    if (thr->status == BB_STATUS_OK ||
			thr->status == BB_STATUS_CREATED)

			msg->destination = thr->id;
		    else
			bad_choice = thr->id;
		    break;
		} else if (ret == 2)
		    backup = thr->id;
		else if (ret == 3)
		    backup_backup = thr->id;
	    }
	}
    }
    mutex_unlock(bbox->mutex);

    if (msg->destination == -1) {
	if (backup >= 0)
	    msg->destination = backup;
	else if (backup_backup >= 0) {
	    info(0, "Using backup-default router because default is down"); 
	    msg->destination = backup_backup;
	}
	else if (bad_choice >= 0) {
	    info(0, "Forced to route to a suspended/non-answering receiver (%d)...",
		 bad_choice); 
	    msg->destination = bad_choice;
	}
	else {
	    error(0, "Cannot route receiver <%s>, Tough.",
		  msg_type(msg->msg) == smart_sms ?
		  octstr_get_cstr(msg->msg->smart_sms.receiver) :
		  octstr_get_cstr(msg->msg->wdp_datagram.destination_address));
	    return -1;
	}
    }
    return 0;
}


/*
 * normalize 'number'
 *
 * return -1 on error, 0 if no match in dial_prefixes and 1 if match found
 * If the 'number' needs normalization, it is done.
 */

static int normalize_number(char *dial_prefixes, Octstr **number)
{
    char *t, *p, *official, *start;
    int len, official_len;
    
    if (dial_prefixes == NULL || dial_prefixes[0] == '\0')
	return 0;

    t = official = dial_prefixes;

    debug(0, "Normalizing <%s> with <%s>",octstr_get_cstr(*number),
	  dial_prefixes);
    
    while(1) {

	for(p = octstr_get_cstr(*number), start = t, len = 0; ; t++, p++, len++) {
	    if (*t == ',' || *t == ';' || *t == '\0') {
		if (start != official) {
		    Octstr *nstr;
		    nstr = octstr_create_limited(official, official_len);
		    if (nstr == NULL)
			goto error;
		    if (octstr_insert_data(nstr, official_len,
					   octstr_get_cstr(*number) + len,
					   octstr_len(*number) - len) < 0)
			goto error;
		    octstr_destroy(*number);
		    *number = nstr;
		}
		return 1;
	    }
	    if (*p == '\0' || *t != *p)
		break;		/* not matching */
	}
	for(; *t != ',' && *t != ';' && *t != '\0'; t++, len++)
	    ;
	if (*t == '\0') break;
	if (start == official) official_len = len;
	if (*t == ';') official = t+1;
	t++;
    }
    return 0;
error:
    error(0, "Memory allocation failed");
    return -1;
}


static void normalize_numbers(RQueueItem *msg, SMSCenter *from)
{
    char *p;
    int sr, rr;

    sr = rr = 0;
    if (from != NULL) {
	p = smsc_dial_prefix(from);
	sr = normalize_number(p, &(msg->msg->smart_sms.sender));
	rr = normalize_number(p, &(msg->msg->smart_sms.receiver));
    }
    if (sr == 0) sr = normalize_number(bbox->global_prefix, &(msg->msg->smart_sms.sender));
    if (rr == 0) rr = normalize_number(bbox->global_prefix, &(msg->msg->smart_sms.receiver));

    if (sr == -1 || rr == -1)
	error(0, "Problems during number normalization");
}


/*----------------------------------------------------
 * MAIN THREAD FDUNCTIONS
 *
 * (although these are similar currently they are separated because
 *  there is no guarentee that they shall remain so identical)
 */

#define HEARTBEAT_UPDATE(ot, lt, us)	{ \
        ot = time(NULL); \
	if ((ot) - (lt) > bbox->heartbeat_freq) \
             { update_heartbeat(us); lt = ot; } \
        }

/*
 * update heartbeat in main program structure
 */
static void update_heartbeat(BBThread *thr)
{
    mutex_lock(bbox->mutex);

    thr->heartbeat = time(NULL);
    
    mutex_unlock(bbox->mutex);
}


/*
 * SMS Center thread - listen to SMSC and get messages
 * likewise do standard operations (heartbeat, dying) and
 * catch our messages from reply message queue
 */
static void *smscenter_thread(void *arg)
{
    BBThread	*us;
    RQueueItem	*msg;
    time_t	our_time, last_time;
    int 	ret;
    
    us = arg;
    us->status = BB_STATUS_OK;
    last_time = time(NULL);

    info(0, "SMSCenter thread [%d] <%s>", us->id, smsc_name(us->smsc));
    
    while(bbox->abort_program < 2) {
	if (us->status == BB_STATUS_KILLED) {
	    warning(0, "SMSC: <%s> back in line!",
		    smsc_name(us->smsc));
	    us->status = BB_STATUS_OK;
	}
	HEARTBEAT_UPDATE(our_time, last_time, us);

	/* check for any messages to us in reply-queue
	 */

	msg = rq_pull_msg(bbox->reply_queue, us->id);
	if (msg) {
	    ret = smsc_send_message(us->smsc, msg, bbox->request_queue);
	    if (ret == -1)
		break;
	    continue;
	}
	/* check for any new messages from SMSC
	 */
	if (bbox->abort_program == 0 &&
	    bbox->suspended == 0 &&
	    rq_queue_len(bbox->request_queue, NULL) < bbox->max_queue) {

	    ret = smsc_get_message(us->smsc, &msg);
	    if (ret == -1) {
		error(0, "SMSC: <%s> failed permanently, killing thread",
		      smsc_name(us->smsc));
		break;		/* kill us */
	    }
	    if (ret == 1) {
		debug(0, "SMSC: Received a message from <%s>",
		      smsc_name(us->smsc));
		normalize_numbers(msg, us->smsc);
		route_msg(us, msg);
	    
		rq_push_msg(bbox->request_queue, msg);
		continue;
	    }
	}
	usleep(1000);
    }
    warning(0, "SMSC: Closing and dying...");
    mutex_lock(bbox->mutex);
    smsc_close(us->smsc);
    us->smsc = NULL;
    us->status = BB_STATUS_DEAD;
    mutex_unlock(bbox->mutex);
    return NULL;
}


/*
 * CSD Router thread.. listen to UDP packets from CSD Router etc.
 */
static void *csdrouter_thread(void *arg)
{
    BBThread	*us;
    RQueueItem	*msg;
    time_t	our_time, last_time;
    int		ret;
    
    us = arg;
    us->status = BB_STATUS_OK;
    last_time = time(NULL);
    
    while(!bbox->abort_program) {
	if (us->status == BB_STATUS_KILLED) break;
	HEARTBEAT_UPDATE(our_time, last_time, us);

	/* check for any messages to us in reply-queue
	 */
	msg = rq_pull_msg(bbox->reply_queue, us->id);
	if (msg) {
	    ret = csdr_send_message(us->csdr, msg);
	    if (msg->msg_type == R_MSG_TYPE_MT) {
		if (ret < 0)
		    msg->msg_type = R_MSG_TYPE_NACK;
		else
		    msg->msg_type = R_MSG_TYPE_ACK;

		rq_push_msg(bbox->request_queue, msg);
	    }
	    continue;	/* is this necessary? */
	}
	/* check for any new messages from CSD Router
	 */
	if (bbox->abort_program == 0 &&
	    bbox->suspended == 0 &&
	    rq_queue_len(bbox->request_queue, NULL) < bbox->max_queue) {

	    msg = csdr_get_message(us->csdr);
	    if (msg) {
		route_msg(us, msg);
		rq_push_msg(bbox->request_queue, msg);
		continue;	/* is this necessary? */
	    }
	}
	usleep(1000);
    }
    warning(0, "CSDR: Closing and dying...");
    mutex_lock(bbox->mutex);

    csdr_close(us->csdr);
    us->csdr = NULL;
    us->status = BB_STATUS_DEAD;

    mutex_unlock(bbox->mutex);
    return NULL;
}


/*
 * WAP BOX Connection
 */
static void *wapboxconnection_thread(void *arg)
{
    BBThread	*us;
    RQueueItem	*msg;
    time_t	our_time, last_time;
    int 	ret;

    us = arg;
    us->boxc = boxc_open(bbox->wap_fd, bbox->allow_ip, bbox->deny_ip);
	
    bbox->accept_pending--;
    if (us->boxc == NULL)
	goto disconnect;

    us->status = BB_STATUS_OK;
    last_time = time(NULL);
    
    while(us->boxc != NULL && !bbox->abort_program) {
	if (us->status == BB_STATUS_KILLED) break;

	our_time = time(NULL);
	if (our_time - last_time > bbox->heartbeat_freq) {
	    if (us->boxc->box_heartbeat + bbox->heartbeat_freq * 2 < our_time) {
		
		warning(0, "WAPBOXC: Other end has stopped beating");
		break;
	    }
	    update_heartbeat(us);
	    last_time = our_time;
	}

	/* check for any messages to us in request-queue,
	 * if any, put into socket and if accepted, add ACK
	 * about that to reply-queue, otherwise NACK
	 */
	msg = rq_pull_msg(bbox->request_queue, us->id);
	if (msg == NULL) {
	    msg = rq_pull_msg_class(bbox->request_queue, R_MSG_CLASS_WAP);

	    /*
	     * if we catch an general WAP message (sent to '-1') lets
	     * catch them _all_
	     */
	    if (msg)
		add_receiver(bbox->request_queue, msg, -1, us->id);
		
	}
	if (msg) {
	    warning(0, "WAPBOXC: wap-message read from queue and discarded"); 
	    ret = boxc_send_message(us->boxc, msg, bbox->reply_queue); 
	    if (ret < 0) {
		error(0, "WAPBOXC: [%d] send message failed, killing", us->id);
		break;
	    }
	    continue;
	}
	
	/* read socket, adding any new messages to reply-queue */
	/* if socket is closed, set us to die-mode */

	ret = boxc_get_message(us->boxc, &msg);
	if (ret < 0) {
	    error(0, "WAPBOXC: [%d] get message failed, killing", us->id);
	    break;
	} else if (ret > 0) {
	    route_msg(us, msg);
	    rq_push_msg(bbox->reply_queue, msg);
	    continue;
	}
    }
disconnect:    
    warning(0, "WAPBOXC: Closing and dying...");
    /*
     * route all WAP messages routed to us to unknown receiver (-1)
     */
    ret = rq_change_destination(bbox->request_queue, R_MSG_CLASS_WAP,
				R_MSG_TYPE_MO, NULL, us->id, -1);

    if (ret > 0)
	info(0, "WAPBOXC: Re-routed %d WAP messages to new unknown WAP box", ret);
    /*
     * remove any references to us
     */
    ret = del_receiver_id(us->id);
    if (ret > 0)
	info(0, "WAPBOXC: Deleted %d WAP routing references to us", ret);
    
    mutex_lock(bbox->mutex);
    
    boxc_close(us->boxc);
    us->boxc = NULL;
    us->status = BB_STATUS_DEAD;

    mutex_unlock(bbox->mutex);
    return NULL;
}


/*
 * SMS BOX Connection
 */
static void *smsboxconnection_thread(void *arg)
{
    BBThread	*us;
    time_t	our_time, last_time;
    int		ret, written = 0;
    RQueueItem	*msg;
    
    us = arg;
    if (us->boxc == NULL)
	us->boxc = boxc_open(bbox->sms_fd, bbox->allow_ip, bbox->deny_ip);
    bbox->accept_pending--;
    if (us->boxc == NULL)
	goto disconnect;

    us->status = BB_STATUS_OK;
    last_time = time(NULL);
    
    while(us->boxc != NULL && bbox->abort_program < 2) {
	if (us->status == BB_STATUS_KILLED) break;

	our_time = time(NULL);
	if (our_time - last_time > bbox->heartbeat_freq) {
	    if (us->boxc->fd != BOXC_THREAD &&
		us->boxc->box_heartbeat + bbox->heartbeat_freq * 2 < our_time) {

		warning(0, "SMSBOXC: Other end has stopped beating");
		break;
	    }
	    update_heartbeat(us);
	    last_time = our_time;
	}
	if (us->boxc->fd == BOXC_THREAD)
	    us->boxc->load = smsbox_req_count();

	if (written < 0)
	    written = 0;

	/* check for any messages to us in request-queue,
	 * if any, put into socket and if accepted, add ACK
	 * about that to reply-queue, otherwise NACK (unless it
	 *  was an ACK/NACK message already)
	 *
	 * NOTE: there should be something load balance here?
	 */

	if (written + us->boxc->load < 100) {
	    msg = rq_pull_msg(bbox->request_queue, us->id);
	    if (msg == NULL) {
		msg = rq_pull_msg_class(bbox->request_queue, R_MSG_CLASS_SMS);
	    }
	    if (msg) {
		ret = boxc_send_message(us->boxc, msg, bbox->reply_queue);
		if (ret < 0) {
		    error(0, "SMSBOXC: [%d] send message failed, killing", us->id);
		    break;
		}
		written++;
		continue;
	    }
	}

	/* for internal thread, all messages are automatically appended to
	 * reply queue, no need to call get_message
	 */
	
	if (us->boxc->fd == BOXC_THREAD) {
	    written--;
	    continue;
	}
	
	/* read socket, adding any new messages to reply-queue */
	/* if socket is closed, set us to die-mode */

	ret = boxc_get_message(us->boxc, &msg);
	if (ret < 0) {
	    error(0, "SMSBOXC: [%d] get message failed, killing", us->id);
	    break;
	} else if (ret > 0) {
	    normalize_numbers(msg, NULL);
	    route_msg(us, msg);
	    rq_push_msg(bbox->reply_queue, msg);
	    written--;
	    continue;
	}
	written--;
	usleep(1000);
    }
disconnect:    
    warning(0, "SMSBOXC: Closing and dying...");
    mutex_lock(bbox->mutex);

    boxc_close(us->boxc);
    us->boxc = NULL;
    us->status = BB_STATUS_DEAD;

    mutex_unlock(bbox->mutex);
    return NULL;
}

/*
 * Internal thread SMS BOX writer. Passed as function pointer
 * to smsbox_req_init, and is then called by smsbox_req_thread
 * when we have a ready SMS Message
 */
int thread_writer(Msg *msg)
{
    RQueueItem *rqi;

    rqi = rqi_new(R_MSG_CLASS_SMS, R_MSG_TYPE_MT);
    if (rqi == NULL) {
	msg_destroy(msg);
	return -1;
    }
    rqi->msg = msg;
    normalize_numbers(rqi, NULL);
    route_msg(internal_smsbox(), rqi);
    rq_push_msg(bbox->reply_queue, rqi);
    debug(0, "SMSBox: wrote <%s> into queue",
	  octstr_get_cstr(msg->smart_sms.msgdata));
    return 0;
}



/*---------------------------------------------------------------------
 * BEARER BOX THREAD FUNCTIONS (utitilies)
 */

/*
 * find first empty thread place in bearer box structure
 */
int find_bbt_index(void)
{
    BBThread *thr, **new;
    int i, ns, in;
    
    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr == NULL)
	    return i;
    }
    ns = bbox->thread_limit * 2;
    new = realloc(bbox->threads, sizeof(BBThread *) * ns);
    if (new == NULL) {
	error(0, "Failed to realloc thread space!");
	return -1;
    }
    in = i;
    bbox->thread_limit = ns;
    for(; i < bbox->thread_limit; i++)
	bbox->threads[i] = NULL;

    return in;
}

/*
 * find new id for the new thread
 */
int find_bbt_id(void)
{
    BBThread *thr;
    int i, max = bbox->id_max;
    
    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL && thr->id >= max) {
	    max = thr->id+1;
	    if (max > ID_MAX) {
		max = bbox->id_max = 1;
		i = -1;			/* restart */
	    }
	}
    }
    return max;
}


/*
 * create a new thread data structure
 */
static BBThread *create_bbt(int type)
{
    int id;
    int index;
    
    BBThread	*nt;
    
    nt = malloc(sizeof(BBThread));
    if (nt == NULL) {
	error(errno, "Malloc failed at create_bbt");
	return NULL;
    }
    nt->type = type;
    nt->status = BB_STATUS_CREATED;
    nt->heartbeat = time(NULL);

    nt->smsc = NULL;
    nt->csdr = NULL;
    nt->boxc = NULL;

    mutex_lock(bbox->mutex);

    id = find_bbt_id();
    index = find_bbt_index();
    nt->id = id;
    bbox->threads[index] = nt;
    bbox->id_max = id;

    mutex_unlock(bbox->mutex);
    return nt;
}


/*
 * delete thread structure and data associated
 */
static void del_bbt(BBThread *thr)
{
    smsc_close(thr->smsc);
    csdr_close(thr->csdr);
    boxc_close(thr->boxc);
    free(thr);
}


/*
 * create a new SMSC thread
 */
static void new_bbt_smsc(SMSCenter *smsc)
{
    BBThread *nt;
    
    nt = create_bbt(BB_TTYPE_SMSC);
    if (nt != NULL) {
	nt->smsc = smsc;
	(void)start_thread(1, smscenter_thread, nt, 0);
	debug(0, "Created a new SMSC thread");
    }
    else
	error(0, "Failed to create a new thread!");
}


/*
 * create a new UDP Thread (CSD Router thread)
 */
static void new_bbt_csdr(CSDRouter *csdr)
{
    BBThread *nt;
    
    nt = create_bbt(BB_TTYPE_CSDR);
    if (nt != NULL) {
	nt->csdr = csdr;
	(void)start_thread(1, csdrouter_thread, nt, 0);
	debug(0, "Created a new CSDR thread");
    }
    else
	error(0, "Failed to create a new thread!");
}


/*
 * create a new WAP BOX Thread
 */
static void new_bbt_wapbox()
{
    BBThread *nt;
    nt = create_bbt(BB_TTYPE_WAP_BOX);
    if (nt != NULL) {
	bbox->accept_pending++;
	(void)start_thread(1, wapboxconnection_thread, nt, 0);
	debug(0, "Created a new WAP BOX thread (id = %d)", nt->id);
    }
    else
	error(0, "Failed to create a new thread!");
}


/*
 * create a new SMS BOX Thread
 */
static void new_bbt_smsbox()
{
    BBThread *nt;
    nt = create_bbt(BB_TTYPE_SMS_BOX);
    if (nt != NULL) {
	bbox->accept_pending++;
	(void)start_thread(1, smsboxconnection_thread, nt, 0);
	debug(0, "Created a new SMS BOX thread (id = %d)", nt->id);
    }
    else
	error(0, "Failed to create a new thread!");
}

/*
 * return a name for given status
 */
static char *bbt_status_name(int status)
{
    switch(status) {
    case BB_STATUS_CREATED:
	return "Created";
    case BB_STATUS_OK:
	return "Running";
    case BB_STATUS_SUSPENDED:
	return "Suspended";
    case BB_STATUS_KILLED:
	return "Killed";
    case BB_STATUS_DEAD:
	return "Dead";
    default:
	return "Unknown";
    }
}

/*
 * kill a threads with given id
 */
static int bbt_kill(int id)
{
    BBThread *thr;
    int i, num, del, ret;
    num = del = 0;
    ret = -1;
    
    mutex_lock(bbox->mutex);
    
    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL && thr->id == id) {
	    thr->status = BB_STATUS_KILLED;
	    ret = 0;
	    break;
	}
    }
    mutex_unlock(bbox->mutex);
    return ret;
}

/*
 * check if we have an internal smsbox. Return pointer to it
 * if we do, NULL otherwise
 */
static BBThread *internal_smsbox(void)
{
    BBThread *thr;
    int i;
    
    mutex_lock(bbox->mutex);

    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL) {
	    if (thr->type == BB_TTYPE_SMS_BOX &&
		thr->boxc != NULL &&
		thr->boxc->fd == BOXC_THREAD)

		break;
	}
    }
    mutex_unlock(bbox->mutex);

    if (i == bbox->thread_limit)
	thr = NULL;
    
    return thr;
}

/*-----------------------------------------------------------
 * HTTP ADMINSTRATION
 */

static char *http_admin_command(char *command, CGIArg *list)
{
    char *val;
    
    if (cgiarg_get(list, "username", &val) == -1 ||
	bbox->admin_username == NULL ||
	strcasecmp(bbox->admin_username, val) != 0 ||
	cgiarg_get(list, "password", &val) == -1 ||
	bbox->admin_password == NULL ||
	strcmp(bbox->admin_password, val) != 0)

	return "Authorization failed";

    if (bbox->abort_program > 0)
	return "The avalance has already started, too late to do anything";

    if (strcasecmp(command, "/cgi-bin/stop") == 0) {
	if (bbox->suspended != 0)
	    return "Already suspended";
	else {
	    bbox->suspended = 1;
	    info(0, "Suspended via HTTP-admin");
	    return "Suspended";
	}
    }
    else if (strcasecmp(command, "/cgi-bin/start") == 0) {
	if (bbox->suspended == 0)
	    return "Already running";
	else {
	    bbox->suspended = 0;
	    info(0, "Re-started via HTTP-admin");
	    return "Re-started";
	}
    }
    else if (strcasecmp(command, "/cgi-bin/kill") == 0) {
	bbox->abort_program = 1;
	warning(0, "Shutdown initiated via HTTP-admin");
	return "Shutdown started";
    }
    else if (strcasecmp(command, "/cgi-bin/disconnect") == 0) {
	if (cgiarg_get(list, "id", &val) == -1)
	    return "Id number missing";
	if (bbt_kill(atoi(val)) == -1)
	    return "Failed (no such id or other error)";
	else
	    return "Disconnected";
    }
    else
	return "Unknown request.";
}



static void *http_request_thread(void *arg)
{
    int client, ret;
    char *path = NULL, *args = NULL, *client_ip = NULL;
    char answerbuf[10*1024];
    char *answer = answerbuf;
    CGIArg *arglist = NULL;
    
    client = httpserver_get_request(bbox->http_fd, &client_ip, &path, &args);
    bbox->accept_pending--;
    if (client == -1) {
	error(0, "HTTP: Failed to get request from client, killing thread");
	return NULL;
    }

    ret = 0;
    if (http_sendsms_allow_ip != NULL)
	ret = check_ip(http_sendsms_allow_ip, client_ip, NULL);
    if (ret < 1 && http_sendsms_deny_ip != NULL)
	if (check_ip(http_sendsms_deny_ip, client_ip, NULL) == 1) {
	    warning(0, "Non-allowed connect tried from <%s>, ignored",
		    client_ip);
	    goto done;
	}
    /* print client information */

    info(0, "Get HTTP request < %s > from < %s >", path, client_ip);
    
    if (strcmp(path, "/cgi-bin/status") == 0) {
	char buf[1024], buf2[1024];
	int t;
	
	print_queues(buf);
	print_threads(buf2);
	t = time(NULL) - start_time;
	
	sprintf(answer,
		"<pre>%s (Time %dd %dh %dm %ds)\n\nQUEUE STATUS:\n%s\n\nTHREAD STATUS:\n%s</pre>",
		(bbox->abort_program > 0) ? "Gateway is going down..." : 
		(bbox->suspended > 0) ? "Gateway is currently suspended" :
		"Gateway is running", t/3600/24, t/3600 % 24, t/60 % 60, t % 60,
		buf, buf2);
    } else {

	if(args!=NULL) 
		arglist = cgiarg_decode_to_list(args);

	answer = http_admin_command(path, arglist);
	
	cgiarg_destroy_list(arglist);
    }
    if (httpserver_answer(client, answer) == -1)
	error(0, "HTTP: Error responding to client. Too bad.");

    /* answer closes the socket */
done:
    free(path);
    free(args);
    free(client_ip);
    return NULL;
}


static void http_start_thread()
{
    bbox->accept_pending++;
    
    (void)start_thread(1, http_request_thread, NULL, 0);

    debug(0, "Created a new HTTP adminstration thread");
}


/*---------------------------------------------------------
 * internal sms box http sendsms
 */

static void *sendsms_thread(void *arg)
{
    int client;
    char *path = NULL, *args = NULL, *client_ip = NULL;
    char *answer;
    CGIArg *arglist;
    
    client = httpserver_get_request(http_sendsms_fd, &client_ip, &path, &args);
    bbox->accept_pending--;
    
    if (client == -1) {
	error(0, "Failed to get request from client, killing thread");
	return NULL;
    }
    /* print client information */

    info(0, "smsbox: Get HTTP request < %s > from < %s >", path, client_ip);
    
    if (strcmp(path, "/cgi-bin/sendsms") == 0) {

	arglist = cgiarg_decode_to_list(args);
	answer = smsbox_req_sendsms(arglist);

	cgiarg_destroy_list(arglist);
    } else
	answer = "unknown request";
    info(0, "%s", answer);

    if (httpserver_answer(client, answer) == -1)
	error(0, "Error responding to client. Too bad.");

    /* answer closes the socket */
    free(path);
    free(args);
    free(client_ip);
    return NULL;
}


static void sendsms_start_thread()
{
    bbox->accept_pending++;

    (void)start_thread(1, sendsms_thread, NULL, 0);

    debug(0, "Created a new HTTP adminstration thread");
}



/*------------------------------------------------------------
 * MAIN PROGRAM (and general running utilities)
 *
 */


/* garbage collector
 * remove all messages without receiver; if the message is not an ACK/NACK,
 * put appropriate NACK to other queue
 */
static void check_queues(void)
{
    time_t now;
    RQueueItem *ptr, *prev;
    
    mutex_lock(bbox->request_queue->mutex);

    ptr = bbox->request_queue->first;
    prev = NULL;
    now = time(NULL);
    while(ptr != NULL) {
	/*
	 * TODO:
	 * check message type and check if there is any eligble
	 * receiver. Nuke if there is no, and put a NACK into
	 * queue unless it was ACK/NACK message
	 */
	
	if (ptr->time_tag + 300 < now)
	    warning(0, "We have a message older than 5 minutes in queue!");

	/* TODO: send mail or something like that! */
	
	ptr = ptr->next;
    }
    mutex_unlock(bbox->request_queue->mutex);
}

/*
 * check all threads in the main structure and destroy those
 * who are dead.
 */
static void check_threads(void)
{
    BBThread *thr;
    int i, num, del;
    num = del = 0;
    
    mutex_lock(bbox->mutex);

    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL) {
	    if (thr->status == BB_STATUS_DEAD) {
		bbox->threads[i] = NULL;
		del_bbt(thr);
		del++;
	    }
	    else {
		num++;
		/*
		 * if we are driving into oblivion, we should set all
		 * SMSCenters as killed, so they do not keep on retrying
		 * doomed re-opens (or not perhaps doomed, but you got the
		 * point, righto?)
		 */
		if (thr->type == BB_TTYPE_SMSC && bbox->abort_program > 0)
		    smsc_set_killed(thr->smsc, 1);
	    }
	}
    }
    bbox->num_threads = num;

    mutex_unlock(bbox->mutex);
}


/*
 * check heartbeat of all threads - if someone has stopped updating,
 * kill it (and restart new...or something)
 */
static void check_heartbeats(void)
{
    BBThread *thr;
    int i;
    time_t now;
    mutex_lock(bbox->mutex);

    now = time(NULL);
    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL && thr->status == BB_STATUS_OK) {
	    if (now - thr->heartbeat > 2 * bbox->heartbeat_freq) {
		warning(0, "Thread %d (id %d) type %d has stopped beating!",
			i, thr->id, thr->type);

		if (thr->status != BB_STATUS_DEAD)
		    thr->status = BB_STATUS_KILLED;
	    }
	}
    }
    mutex_unlock(bbox->mutex);
}

/*
 * print the current and mean queue length of both queues
 * into target buffer, which must be large enough (around 150 chars)
 */
static void print_queues(char *buffer)
{
    int rq, rp;
    int totp, totq;
    time_t trp, trq, now;
    
    rq = rq_queue_len(bbox->request_queue, &totq);
    rp = rq_queue_len(bbox->reply_queue, &totp);
    trq = rq_oldest_message(bbox->request_queue);
    trp = rq_oldest_message(bbox->reply_queue);
    now = time(NULL);
    
    mutex_lock(bbox->mutex);

    sprintf(buffer,"Request queue length %d, oldest %ds old; mean %.1f, total %d messages\n"
	    "Reply queue length %d; oldest %ds old; mean %.1f, total %d messages",
	    rq, (int)(now-trq), bbox->mean_req_ql, totq, 
	    rp, (int)(now-trp), bbox->mean_rep_ql, totp);
	    
    mutex_unlock(bbox->mutex);
}

/*
 * function to update average queue length function. Takes
 * value each heartbeat moment and the value is average of last 10
 * heartbeats
 */
static void update_queue_watcher()
{
    static int req_ql[10], rep_ql[10];
    static int index = 0;
    static int c = 0;
    int i, id;
    int req, rep;
    time_t limit;
    
    req = rq_queue_len(bbox->request_queue, NULL);
    rep = rq_queue_len(bbox->reply_queue, NULL);

    if (bbox->abort_program == 1) {
	/*
	 * if we being terminated, check that no one has accessed
	 * queues for a while. 3 seconds should be sufficient for
	 * sms/wap boxes to handle messages. If it is not, too bad.
	 *
	 * note that if someone continuasly sends sms via HTTP
	 * interface the program will never die. So we should send a
	 * kill notification to boxes, too... TODO
	 */
	limit = time(NULL);
	if (req == 0 && rep == 0 && (rq_last_mod(bbox->request_queue) < limit-3  ||
				     rq_last_mod(bbox->reply_queue) < limit-2))
	    bbox->abort_program = 2;     	/* time to die... */
    }
    req_ql[index%10] = req;
    rep_ql[index%10] = rep;
    index++;
    if (index > ID_MAX)
	index=10;

    id = (index > 10) ? 10 : index;

    req = rep = 0;
    for(i=0; i<id; i++) {
	req += req_ql[i];
	rep += rep_ql[i];
    }
    mutex_lock(bbox->mutex);

    bbox->mean_req_ql = req / id;
    bbox->mean_rep_ql = rep / id;

    mutex_unlock(bbox->mutex);

    c++;
    if (c % 20 == 19)
	check_queues();

    if (c >= 40) {
	limit = time(NULL) - 35;
	
	if (rq_last_mod(bbox->request_queue) > limit ||
	    rq_last_mod(bbox->reply_queue) > limit) {

	    char buf[1024];
	    print_queues(buf);
	    debug(0, "\n%s", buf);
	    c = 0;
	}
	else c = 20;
    }
}

/*
 * print information about running receiver threads
 * Info is put into buffer which must be large enough
 */
static void print_threads(char *buffer)
{
    BBThread *thr;
    int i;
    char buf[1024];

    buffer[0] = '\0';
    
    mutex_lock(bbox->mutex);

    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL && thr->status != BB_STATUS_DEAD) {
	    switch(thr->type) {
	    case BB_TTYPE_SMSC:
		sprintf(buf, "[%d] SMSC Connection %s (%s)\n", thr->id,
			smsc_name(thr->smsc), bbt_status_name(thr->status));
		break;
	    case BB_TTYPE_CSDR:
		sprintf(buf, "[%d] CSDR Connection (%s)\n", thr->id,
			bbt_status_name(thr->status));
		break;
	    case BB_TTYPE_SMS_BOX:
		if (thr->boxc->fd == BOXC_THREAD)
		    sprintf(buf, "[%d] Internal SMS BOX (%s)\n", thr->id,
			    bbt_status_name(thr->status));
		else
		    sprintf(buf, "[%d] SMS BOX Connection from <%s> (%s)\n", thr->id,
			    thr->boxc->client_ip, bbt_status_name(thr->status));
		break;
	    case BB_TTYPE_WAP_BOX: 
		sprintf(buf, "[%d] WAP BOX Connection from <%s> (%s)\n", thr->id,
			thr->boxc->client_ip, bbt_status_name(thr->status));
		break;
	    default:
		sprintf(buf, "Unknown connection type\n");
	    }
	    strcat(buffer, buf);
	}
    }
    mutex_unlock(bbox->mutex);

    if (bbox->http_port > -1) {
	sprintf(buf, "[n/a] HTTP-Adminstration at port %d\n", bbox->http_port);
	strcat(buffer, buf);
    }
}


/*
 * the main program, which opens new sockets and checks the heartbeat
 */
static void main_program(void)
{
    struct timeval tv;
    fd_set rf;
    int ret;
    char buf[1024];
    time_t last, now, last_sec;
    
    last = last_sec = time(NULL);
    
    while(bbox->abort_program < 2) {

	/* check heartbeat of all threads; if no response for a
	 * long time, delete thread. This also requires that
	 * any messages routed to it are temporarily re-routed or
	 * other nice logics, have to think about that
	 */

	now = time(NULL);
	if (now != last_sec) {		/* once a second or so */
	    update_queue_watcher();
	    last_sec = now;
	}
	if (now - last > bbox->heartbeat_freq) {
	    check_threads();		/* destroy killed */
	    check_heartbeats();		/* check if need to be marked as killed */
	    ret = check_receivers();
	    if (ret > 0)
		info(0, "%d old WAP routing infos deleted", ret);

	    last = now;
	}

	if (bbox->accept_pending)
	    continue;
	
	FD_ZERO(&rf);
	FD_SET(bbox->http_fd, &rf);
	if (!bbox->abort_program && http_sendsms_fd >= 0)
	    FD_SET(http_sendsms_fd, &rf);
	    
	FD_SET(bbox->wap_fd, &rf);
	FD_SET(bbox->sms_fd, &rf);
	FD_SET(0, &rf);
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	
	ret = select(FD_SETSIZE, &rf, NULL, NULL, &tv);
	if (ret > 0) {
	    /* if any file descriptor set, create a new thread
	     * to handle that particular connection
	     */
	    
	    if (FD_ISSET(bbox->http_fd, &rf))
		http_start_thread();
	    if (FD_ISSET(bbox->wap_fd, &rf))
		new_bbt_wapbox();
	    if (FD_ISSET(bbox->sms_fd, &rf))
		new_bbt_smsbox();
	    if (!bbox->abort_program && http_sendsms_fd >=0 &&
		FD_ISSET(http_sendsms_fd, &rf))

		sendsms_start_thread();

	    sleep(1);	/* sleep for a while... work around this */
	}
	else if (ret < 0) {
	    if(errno==EINTR) continue;
	    if(errno==EAGAIN) continue;
	    error(errno, "Main select failed");
	}
    }
    sleep(1);		/* some time for threads to die */
    check_threads();
    warning(0, "Bearer box terminating.. hopefully threads, too");
    print_threads(buf);
    info(0, "Threads:\n%s", buf);
    print_queues(buf);
    info(0, "\n%s", buf);
}


/*---------------------------------------------------------------------------
 * INITIALIZATION
 */

/*
 * the first initialization
 */
static void init_bb(Config *cfg)
{
    ConfigGroup *grp;
    char *logfile = NULL;
    char *p;
    int i, lvl = 0;
    
    bbox = malloc(sizeof(BearerBox));
    if (bbox == NULL)
	goto error;

    bbox->num_threads = 0;
    bbox->id_max = 1;
    bbox->abort_program = 0;
    bbox->suspended = 0;
    bbox->mutex = mutex_create();
    route_mutex = mutex_create();
    
    bbox->thread_limit = 20;
    bbox->http_port = BB_DEFAULT_HTTP_PORT;
    bbox->wapbox_port = BB_DEFAULT_WAPBOX_PORT;
    bbox->smsbox_port = BB_DEFAULT_SMSBOX_PORT;
    bbox->heartbeat_freq = BB_DEFAULT_HEARTBEAT;
    bbox->max_queue = BB_DEFAULT_MAX_QUEUE;
    bbox->pid_file = NULL;
    bbox->admin_username = NULL;
    bbox->admin_password = NULL;
    bbox->global_prefix = NULL;
    bbox->allow_ip = NULL;
    bbox->deny_ip = NULL;
    
    grp = config_first_group(cfg);
    while(grp != NULL) {
	if ((p = config_get(grp, "max-threads")) != NULL)
	    bbox->thread_limit = atoi(p);
	if ((p = config_get(grp, "http-port")) != NULL)
	    bbox->http_port = atoi(p);
	if ((p = config_get(grp, "wap-port")) != NULL)
	    bbox->wapbox_port = atoi(p);
	if ((p = config_get(grp, "sms-port")) != NULL)
	    bbox->smsbox_port = atoi(p);
	if ((p = config_get(grp, "global-prefix")) != NULL)
	    bbox->global_prefix = p;
	if ((p = config_get(grp, "allowed-hosts")) != NULL)
	    bbox->allow_ip = p;
	if ((p = config_get(grp, "denied-hosts")) != NULL)
	    bbox->deny_ip = p;
	if ((p = config_get(grp, "admin-username")) != NULL)
	    bbox->admin_username = p;
	if ((p = config_get(grp, "admin-password")) != NULL)
	    bbox->admin_password = p;
	if ((p = config_get(grp, "heartbeat-freq")) != NULL)
	    bbox->heartbeat_freq = atoi(p);
	if ((p = config_get(grp, "pid-file")) != NULL)
	    bbox->pid_file = p;
	if ((p = config_get(grp, "log-file")) != NULL)
	    logfile = p;
	if ((p = config_get(grp, "log-level")) != NULL)
	    lvl = atoi(p);
	
	grp = config_next_group(grp);
    }
    if (logfile != NULL)
	open_logfile(logfile, lvl);

    warning(0, "Gateway bearer box version %s starting", VERSION);

    if (bbox->allow_ip != NULL && bbox->deny_ip == NULL)
	warning(0, "Allow IP-string set without any IPs denied!");
    
    if (bbox->thread_limit < 5) {
	error(0, "Thread limit set to less than 5 (%d), set it 5",
	      bbox->thread_limit);
	bbox->thread_limit = 5;
    }
    bbox->threads = malloc(sizeof(BBThread *) * bbox->thread_limit);
    if (bbox->threads == NULL)
	goto error;
    bbox->request_queue = rq_new();
    bbox->reply_queue = rq_new();
    if (bbox->request_queue == NULL || bbox->reply_queue == NULL) {
	error(0, "Failed to create queues");
	goto error;
    }
    for(i=0; i < bbox->thread_limit; i++)
	bbox->threads[i] = NULL;

    bbox->http_fd = httpserver_setup(bbox->http_port);
    bbox->wap_fd = make_server_socket(bbox->wapbox_port);
    bbox->sms_fd = make_server_socket(bbox->smsbox_port);

    info(0, "Set HTTP-adminstation at port <%d>", bbox->http_port);
    

    if(bbox->http_fd < 0 || bbox->wap_fd < 0 || bbox->sms_fd < 0) {
	error(0, "Failed to open sockets");
	goto error;
    }
    return;
error:	
    panic(errno, "Failed to create bearerbox, exiting");
    return;	/* not needed, against stupid compilers */
}


/*
 * write the pid to the file, if set
 */
static void write_pid_file(void)
{
    FILE *f;
        
    if (bbox->pid_file != NULL) {
	f = fopen(bbox->pid_file, "w");
	fprintf(f, "%d\n", getpid());
	fclose(f);
    }
}


static void signal_handler(int signum)
{
    static time_t first_kill = -1;
    
    if (signum == SIGINT) {
	if (bbox->abort_program == 0) {
	    error(0, "SIGINT received, emptying queues...");
	    bbox->abort_program = 1;
	    first_kill = time(NULL);
	}
	else if (bbox->abort_program == 1) {
	    if (time(NULL) - first_kill > 2) {
		error(0, "New SIGINT received, killing neverthless...");
		bbox->abort_program = 2;
	    }
	}
    } else if (signum == SIGHUP) {
        warning(0, "SIGHUP received, catching and re-opening logs");
        reopen_log_files();
    }
}


static void setup_signal_handlers(void)
{
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
}





/* Read the configuration file and connect to all SMS centers and
 * CSD Routers.
 * Create a new thread for all successfully connected
 */
static void open_all_receivers(Config *cfg)
{
    SMSCenter *smsc;
    CSDRouter *csdr;
    ConfigGroup *grp;
        
    grp = config_first_group(cfg);
    while (grp != NULL) {
	if (config_get(grp, "smsc") != NULL) {
	    smsc = smsc_open(grp);
	    
	    if (smsc == NULL) {
		error(0, "Problems connecting to an SMSC, skipping.");
	    } else {
		new_bbt_smsc(smsc);
	    }
	}
	else if (config_get(grp, "csdr") != NULL) {
	    csdr = csdr_open(grp);
	    
	    if (csdr == NULL) {
		error(0, "Problems connecting to a CSDR, skipping.");
	    } else {
		new_bbt_csdr(csdr);
	    }
	}
	grp = config_next_group(grp);
    }
}


/*
 * this function creates an internal SMS BOX Thread
 * (it is much faster than sms box connected via tcp/ip)
 */
void create_internal_smsbox(Config *cfg)
{
    ConfigGroup *grp;
    BBThread *nt;
    URLTranslationList *translations;
    char *p;
    int sms_len, sendsms_port;
    char *global_sender;

    sms_len = 160;
    sendsms_port = -1;
    
    info(0, "Creating an internal SMS BOX");
    
    grp = config_first_group(cfg);
    while(grp != NULL) {
        if ((p = config_get(grp, "sendsms-port")) != NULL)
	    sendsms_port = atoi(p);
	
        if ((p = config_get(grp, "sms-length")) != NULL)
            sms_len = atoi(p);
        if ((p = config_get(grp, "global-sender")) != NULL)
            global_sender = p;
	
        if ((p = config_get(grp, "http-allowed-hosts")) != NULL)
            http_sendsms_allow_ip = p;
        if ((p = config_get(grp, "http-denied-hosts")) != NULL)
            http_sendsms_deny_ip = p;
	
        grp = config_next_group(grp);
    }
    if (http_sendsms_allow_ip != NULL && http_sendsms_deny_ip == NULL)
	warning(0, "Allow IP-string set without any IPs denied!");

    if (global_sender != NULL)
        info(0, "Internal SMS BOX global sender set as '%s'", global_sender);
    
    if (sendsms_port != bbox->http_port) {
	http_sendsms_fd = httpserver_setup(sendsms_port);
	if (http_sendsms_fd < 0)
	    error(0, "Failed to open sendsms HTTP socket <%d>, ignoring it",
		  sendsms_port);
	else
	    info(0, "Set up sendsms service at port %d", sendsms_port);
    }
    else if (sendsms_port == 0) {
	warning(0, "No sendssms-port set in smsconf, cannot send SMSes via HTTP");
	http_sendsms_fd = -1;
    }
    
    translations = urltrans_create();
    if (translations == NULL)
        panic(errno, "urltrans_create failed");
    if (urltrans_add_cfg(translations, cfg) == -1)
        panic(errno, "urltrans_add_cfg failed");

    smsbox_req_init(translations, sms_len, global_sender, thread_writer);
    
    nt = create_bbt(BB_TTYPE_SMS_BOX);
    if (nt != NULL) {
	bbox->accept_pending++;
	nt->boxc = boxc_open(BOXC_THREAD, NULL, NULL);
	if (nt->boxc != NULL)
	    (void)start_thread(1, smsboxconnection_thread, nt, 0);
    }
    else
	error(0, "Failed to create a new thread!");
    
    return;
}


int main(int argc, char **argv)
{
    int cf_index;
    Config *cfg;
        
    start_time = time(NULL);
    cf_index = get_and_set_debugs(argc, argv, NULL);

    setup_signal_handlers();
    cfg = config_from_file(argv[cf_index], "bearerbox.conf");
    if (cfg == NULL)
	panic(0, "No configuration, aborting.");
    
    init_bb(cfg);
    write_pid_file();
    open_all_receivers(cfg);

    if (cf_index+1 < argc) {
	cfg = config_from_file(argv[cf_index+1], "smsbox.smsconf");
	if (cfg == NULL)
	    info(0, "No internal SMS BOX");
	else
	    create_internal_smsbox(cfg);
    }
    else
	info(0, "No internal SMS BOX");
	
    main_program();

    return 0;
}


