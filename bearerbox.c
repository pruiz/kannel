/*
 * Bearer box
 *
 * (WAP/SMS) Gateway
 *
 * Kalle Marjola 1999 for Wapit ltd.
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>

#include "wapitlib.h"
#include "config.h"
#include "http.h"

#include "bb_msg.h"
#include "msg.h"
#include "smsc.h"
#include "csdr.h"
#include "boxc.h"
#include "bb.h"

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

    int http_fd, wap_fd, sms_fd;	/* socket FDs */
    
    char *pid_file;		/* our pid */
    char *global_prefix;	/* global normalization string */
    
    pthread_mutex_t mutex;	/* for heartbeat/structure update */
} BearerBox;


static BearerBox *bbox = NULL;




/*--------------------------------------------------------
 * FORWARD DECLARATIONS
 */

static void print_threads(char *buffer);



/*--------------------------------------------------------------------
 * UTILITIES
 */

/*
 * routing information for (WAP) traffic
 *
 * note that this is a stupid linear table, we should use
 * a sorted list so that we can use a binary search or something...
 */

typedef struct routeinfo {
    char *route_match;
    int receiver_id;
} RouteInfo;

static RouteInfo *route_info = NULL;
static int route_count = 0;
static int route_limit = 0;


int find_receiver(RQueueItem *rqi)
{
    int i;

    if (rqi->routing_info == NULL)
	return -1;
    
    for(i=0; i < route_count; i++) {
	if (strcmp(rqi->routing_info, route_info[i].route_match)==0)
	    return route_info[i].receiver_id;
    }
    return -1;
}


int add_receiver(char *routing_str, int id)
{
    RouteInfo *new;
    
    if (route_count >= route_limit) {
	route_limit += 1024;
	new = realloc(route_info, route_limit * sizeof(RouteInfo));
	if (new == NULL) {
	    error(errno, "Failed to add a new receiver routing info");
	    return -1;
	}
	route_info = new;
    }
    route_info[route_count].route_match = strdup(routing_str);
    if (route_info[route_count].route_match == NULL) {
	error(errno, "Failed to allocate room for router");
	return -1;
    }
    route_info[route_count].receiver_id = id;

    route_count++;
    return 0;
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

    if (msg->source > -1)	/* if we have already routed message */
	return 0;			
    msg->source = bbt->id;

    if (msg->msg_type == R_MSG_TYPE_MO) {
	if (msg->msg_class == R_MSG_CLASS_SMS)
	    return 0;	      	/* no direct destination, leave it
				 * to load balancing functions */
	/* WAP
	* .. if a WAP is connectioneless, send to any wap box.. so
	* need to set the destination. But if the message is connectioned
	* session, we must route all to same wap box... that later */

	return 0;		/* TODO: Route! */

	
    }
    /* if we have gone this far, this must be a mobile terminated
     * (new) message from sms/wap box to SMSC/CSD Router
     *
     * This means that we must use some heurastics to find to which
     * one we will send this...
     */

    /* possible bottleneck? deal with this later */
    
    ret = pthread_mutex_lock(&bbox->mutex);	
    if (ret != 0)
	goto error;

    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL) {
	    if ((thr->type == BB_TTYPE_SMSC ||
		 thr->type == BB_TTYPE_CSDR)
		&&
		(thr->status == BB_STATUS_OK ||
		 thr->status == BB_STATUS_CREATED)) {

		/* Route all WAPs to single CSDR.
		 * TODO: multiple routing
		 */
		if (msg->msg_class == R_MSG_CLASS_WAP &&
		    thr->type == BB_TTYPE_CSDR) {

		    msg->destination = thr->id;
		    break;
		}


		if (thr->type == BB_TTYPE_SMSC)
		    ret = smsc_receiver(thr->smsc,
			   octstr_get_cstr(msg->msg->plain_sms.receiver));
		else
		    ret = 0;
		
		if (ret == 1) {
		    msg->destination = thr->id;
		    break;
		} else if (ret == 2)
		    backup = thr->id;
	    }
	}
    }
    ret = pthread_mutex_unlock(&bbox->mutex);
    if (ret != 0)
	goto error;

    if (msg->destination == -1) {
	if (backup >= 0)
	    msg->destination = backup;
	else {
	    error(0, "Cannot route receiver <%s>, message ignored",
		  octstr_get_cstr(msg->msg->plain_sms.receiver));
	    return -1;
	}
    }
    return 0;

error:
    error(ret, "mutex error! Failed to route etc.");
    return -1;
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
    
    while(1) {

	for(p = octstr_get_cstr(*number), start = t, len = 0; ; t++, p++, len++) {
	    if (*t == ',' || *t == ';' || *t == '\0') {
		if (start != official) {
		    Octstr *nstr;
		    nstr = octstr_create_limited(official, official_len);
		    if (nstr == NULL)
			goto error;
		    if (octstr_insert_data(nstr, official_len - len + 1,
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
	if (p != NULL) {
	    sr = normalize_number(p, &(msg->msg->plain_sms.sender));
	    rr = normalize_number(p, &(msg->msg->plain_sms.receiver));
	}
    }
    if (sr == 0)
	sr = normalize_number(bbox->global_prefix, &(msg->msg->plain_sms.sender));
    if (rr == 0)
	rr = normalize_number(bbox->global_prefix, &(msg->msg->plain_sms.receiver));

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
    int ret;
    ret = pthread_mutex_lock(&bbox->mutex);
    if (ret != 0)
	goto error;

    thr->heartbeat = time(NULL);
    
    ret = pthread_mutex_unlock(&bbox->mutex);
    if (ret != 0)
	goto error;

    return;	      
    
error:
    error(ret, "Failed to update heartbeat");
    return;
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
    int		wait;
    
    us = arg;
    us->status = BB_STATUS_OK;
    last_time = time(NULL);

    info(0, "smscenter thread [%d/%s]..", us->id, smsc_name(us->smsc));
    
    while(bbox->abort_program < 2) {
	wait = 1;
	if (us->status == BB_STATUS_KILLED) break;
	HEARTBEAT_UPDATE(our_time, last_time, us);

	/* check for any messages to us in reply-queue
	 */

	msg = rq_pull_msg(bbox->reply_queue, us->id);
	if (msg) {
	    ret = smsc_send_message(us->smsc, msg, bbox->request_queue);
	    continue;
	}
	/* check for any new messages from SMSC
	 */
	if (bbox->abort_program == 0 &&
	    rq_queue_len(bbox->request_queue, NULL) < bbox->max_queue) {

	    ret = smsc_get_message(us->smsc, &msg);
	    if (ret == -1) {
		error(0, "SMSC: [%d] failed permanently, killing thread", us->id);
		break;		/* kill us */
	    }
	    if (ret == 1) {
		normalize_numbers(msg, us->smsc);
		route_msg(us, msg);
	    
		rq_push_msg(bbox->request_queue, msg);
		debug(0, "Got message [%d] from %s", msg->id, smsc_name(us->smsc));
		wait = 0;
	    }
	}
	if (wait)
	    usleep(1000);
    }
    us->status = BB_STATUS_DEAD;
    smsc_close(us->smsc);
    us->smsc = NULL;
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
	msg = csdr_get_message(us->csdr);
	if (msg) {
	    route_msg(us, msg);
	    rq_push_msg(bbox->request_queue, msg);
	    continue;	/* is this necessary? */
	}

	usleep(1000);
    }
    us->status = BB_STATUS_DEAD;
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
    us->boxc = boxc_open(bbox->wap_fd);
    bbox->accept_pending--;

    us->status = BB_STATUS_OK;
    
    while(us->boxc != NULL && !bbox->abort_program) {
	if (us->status == BB_STATUS_KILLED) break;
	HEARTBEAT_UPDATE(our_time, last_time, us);

	/* check for any messages to us in request-queue,
	 * if any, put into socket and if accepted, add ACK
	 * about that to reply-queue, otherwise NACK
	 */
	msg = rq_pull_msg(bbox->request_queue, us->id);
	if (msg == NULL)
	    msg = rq_pull_msg_class(bbox->request_queue, R_MSG_CLASS_WAP);

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
    info(0, "WAPBOXC: Closing and dying...");
    boxc_close(us->boxc);
    us->boxc = NULL;
    us->status = BB_STATUS_DEAD;
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
    us->boxc = boxc_open(bbox->sms_fd);
    bbox->accept_pending--;

    us->status = BB_STATUS_OK;
    
    while(us->boxc != NULL && bbox->abort_program < 2) {
	
	if (us->status == BB_STATUS_KILLED) break;
	/* update heartbeat if too much from the last update
	 * die if forced to, closing the socket */

        our_time = time(NULL);
	if ((our_time) - (last_time) > bbox->heartbeat_freq) {
	    if (us->boxc->box_heartbeat < last_time) {
		warning(0, "SMSBOXC: Other end has stopped beating");
		break;
	    }
	    update_heartbeat(us);
	    last_time = our_time;
	}
	

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
    info(0, "SMSBOXC: Closing and dying...");
    boxc_close(us->boxc);
    us->boxc = NULL;
    us->status = BB_STATUS_DEAD;
    return NULL;
}



/*---------------------------------------------------------------------
 * BEARER BOX THREAD FUNCTIONS (receivers)
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

    id = find_bbt_id();
    index = find_bbt_index();
    nt->id = id;
    bbox->threads[index] = nt;

    bbox->id_max = id;
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
    }
    debug(0, "Created a new SMSC thread");
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
    }
    debug(0, "Created a new CSDR thread");
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
    }
    debug(0, "Created a new WAP BOX thread (id = %d)", nt->id);
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
    }
    debug(0, "Created a new SMS BOX thread (id = %d)", nt->id);
}



/*-----------------------------------------------------------
 * HTTP ADMINSTRATION
 */

static void *http_request_thread(void *arg)
{
    int client;
    char *path = NULL, *args = NULL, *client_ip = NULL;
    char answer[10*1024];
    
    client = httpserver_get_request(bbox->http_fd, &client_ip, &path, &args);
    bbox->accept_pending--;
    if (client == -1) {
	error(0, "HTTP: Failed to get request from client, killing thread");
	return NULL;
    }

    sprintf(answer, "HTTP adminstration not yet installed, you have our sympathy");
    info(0, "%s", answer);

    if (httpserver_answer(client, answer) == -1)
	error(0, "HTTP: Error responding to client. Too bad.");

    /* answer closes the socket */

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
    int ret;
    RQueueItem *ptr, *prev;
    
    ret = pthread_mutex_lock(&bbox->request_queue->mutex);
    if (ret != 0)
	goto error;

    ptr = bbox->request_queue->first;
    prev = NULL;
    while(ptr) {
	/*
	 * TODO:
	 * check message type and check if there is any eligble
	 * receiver. Nuke if there is no, and put a NACK into
	 * queue unless it was ACK/NACK message
	 */
	ptr = ptr->next;
    }
    ret = pthread_mutex_unlock(&bbox->request_queue->mutex);
    if (ret != 0)
	goto error;

    return;
    
error:
    error(ret, "Failed to check queues");
    return;
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
    
    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL) {
	    if (thr->status == BB_STATUS_DEAD) {
		del_bbt(thr);
		bbox->threads[i] = NULL;
		del++;
	    }
	    else
		num++;
	}
    }
    bbox->num_threads = num;
    debug(0, "check_threads: %d active threads, %d killed", num, del);
}


/*
 * check heartbeat of all threads - if someone has stopped updating,
 * kill it (and restart new...or something)
 */
static void check_heartbeats(void)
{
    BBThread *thr;
    int i;
    int ret;
    time_t now;
    ret = pthread_mutex_lock(&bbox->mutex);
    if (ret != 0)
	goto error;

    now = time(NULL);
    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL) {
	    if (now - thr->heartbeat > 2 * bbox->heartbeat_freq) {
		warning(0, "Thread %d (id %d) type %d has stopped beating!",
			i, thr->id, thr->type);

		if (thr->status != BB_STATUS_DEAD)
		    thr->status = BB_STATUS_KILLED;
	    }
	}
    }
    ret = pthread_mutex_unlock(&bbox->mutex);
    if (ret != 0)
	goto error;

    return;	      
    
error:
    error(ret, "Failed to check heartbeats");
    return;
}

/*
 * print the current and mean queue length of both queues
 * into target buffer, which must be large enough (around 150 chars)
 */
static void print_queues(char *buffer)
{
    int ret;
    int rq, rp;
    int totp, totq;
    time_t trp, trq, now;
    
    rq = rq_queue_len(bbox->request_queue, &totq);
    rp = rq_queue_len(bbox->reply_queue, &totp);
    trq = rq_oldest_message(bbox->request_queue);
    trp = rq_oldest_message(bbox->reply_queue);
    now = time(NULL);
    
    ret = pthread_mutex_lock(&bbox->mutex);
    if (ret != 0)
	goto error;

    sprintf(buffer,"Request queue length %d, oldest %ds old; mean %.1f, total %d messages\n"
	    "Reply queue length %d; oldest %ds old; mean %.1f, total %d messages",
	    rq, (int)(now-trq), bbox->mean_req_ql, totq, 
	    rp, (int)(now-trp), bbox->mean_rep_ql, totp);
	    
    ret = pthread_mutex_unlock(&bbox->mutex);
    if (ret != 0)
	goto error;

    return;	      

error:
    error(ret, "Failed to print queues");
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
    int i, id, ret;
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
    ret = pthread_mutex_lock(&bbox->mutex);
    if (ret != 0)
	goto error;

    bbox->mean_req_ql = req / id;
    bbox->mean_rep_ql = rep / id;

    ret = pthread_mutex_unlock(&bbox->mutex);
    if (ret != 0)
	goto error;

    c++;
    if (c >= 20) {
	if (c > 120 || req > 0 || rep > 0) {
	    char buf[1024];
	    print_queues(buf);
	    info(0, "\n%s", buf);
	    c = 0;
	}
    }
    return;	      
    
error:
    error(ret, "Failed to update mean queue lengths");
    return;
}

/*
 * print information about running receiver threads
 * Info is put into buffer which must be large enough
 */
static void print_threads(char *buffer)
{
    BBThread *thr;
    int i, ret, num;
    int smsbox, wapbox, smsc, csdr;

    smsbox = wapbox = smsc = csdr = 0;
    num = 0;
    
    ret = pthread_mutex_lock(&bbox->mutex);
    if (ret != 0)
	goto error;

    for(i=0; i < bbox->thread_limit; i++) {
	thr = bbox->threads[i];
	if (thr != NULL) {
	    if (thr->status == BB_STATUS_OK) {
		switch(thr->type) {
		case BB_TTYPE_SMSC: smsc++; break;
		case BB_TTYPE_CSDR: csdr++; break;
		case BB_TTYPE_SMS_BOX: smsbox++; break;
		case BB_TTYPE_WAP_BOX: wapbox++; break;
		}
	    }
	    if (thr->status != BB_STATUS_DEAD)
		num++;
	}
    }
    ret = pthread_mutex_unlock(&bbox->mutex);
    if (ret != 0)
	goto error;

    sprintf(buffer, "Total %d receiver threads, of which...\n"
	    "active ones: %d SMSC, %d CSDR, %d SMS BOX, %d WAP BOX",
	    num, smsc, csdr, smsbox,wapbox);

    return;	      
error:
    error(ret, "Failed to print threads");
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
    int c = 0;
    
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
	    last = now;

	    c++;
	    if (c == 60) {
		print_threads(buf);
		info(0, "Threads:\n%s", buf);
		c = 0;
	    }
	}

	if (bbox->accept_pending)
	    continue;
	
	FD_ZERO(&rf);
	FD_SET(bbox->http_fd, &rf);
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
    pthread_mutex_init(&bbox->mutex, NULL);
    
    bbox->thread_limit = 20;
    bbox->http_port = BB_DEFAULT_HTTP_PORT;
    bbox->wapbox_port = BB_DEFAULT_WAPBOX_PORT;
    bbox->smsbox_port = BB_DEFAULT_SMSBOX_PORT;
    bbox->heartbeat_freq = BB_DEFAULT_HEARTBEAT;
    bbox->max_queue = BB_DEFAULT_MAX_QUEUE;
    bbox->pid_file = NULL;
    bbox->global_prefix = NULL;
    
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

    if(bbox->http_fd < 0 || bbox->wap_fd < 0 || bbox->sms_fd < 0) {
	error(0, "Failed to open sockets");
	goto error;
    }
    if (logfile != NULL) {
	info(0, "Starting to log to file %s level %d", logfile, lvl);
	open_logfile(logfile, lvl);
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


int main(int argc, char **argv)
{
    int cf_index;
    Config *cfg;
        
    cf_index = get_and_set_debugs(argc, argv, NULL);

    warning(0, "Gateway bearer box version %s starting", VERSION);

    setup_signal_handlers();
    cfg = config_from_file(argv[cf_index], "bearerbox.conf");
    if (cfg == NULL)
	panic(0, "No configuration, aborting.");
    
    init_bb(cfg);
    open_all_receivers(cfg);
    write_pid_file();
    
    main_program();

    return 0;
}


