/*
 * bb_boxc.c : bearerbox box connection module
 *
 * handles start/restart/stop/suspend/die operations of the sms and
 * wapbox connections
 *
 * Kalle Marjola <rpr@wapit.com> 2000 for project Kannel
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

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern List *incoming_sms;
extern List *outgoing_sms;
extern List *incoming_wdp;
extern List *outgoing_wdp;

extern List *flow_threads;
extern List *suspended;

/* our own thingies */

static volatile sig_atomic_t smsbox_running;
static volatile sig_atomic_t wapbox_running;
static List	*wapbox_list = NULL;
static List	*smsbox_list = NULL;

static int	smsbox_port;
static int	wapbox_port;

static char *box_allow_ip;
static char *box_deny_ip;


static long	boxid = 0;

typedef struct _boxc {
    int   	fd;
    int		is_wap;
    long      	id;
    int		load;
    time_t	connect_time;
    Octstr    	*client_ip;
    List      	*incoming;
    List      	*retry;   	/* If sending fails */
    List       	*outgoing;
    volatile sig_atomic_t alive;
} Boxc;


/*-------------------------------------------------
 *  receiver thingies
 */

static void boxc_receiver(void *arg)
{
    Boxc *conn = arg;
    Octstr *pack;
    Msg *msg;
    int ret;

    /* remove messages from socket until it is closed */
    while(bb_status != BB_DEAD && conn->alive) {

	if (read_available(conn->fd, 100000) < 1)
	    continue;

	list_consume(suspended);	/* block here if suspended */
	
	ret = octstr_recv(conn->fd, &pack);

	if (ret < 1) {
	    info(0, "Client <%s> closed connection",
		  octstr_get_cstr(conn->client_ip));
	    conn->alive = 0;
	    break;
	}
	if ((msg = msg_unpack(pack))==NULL) {
	    debug("bb.boxc", 0, "Received garbage from <%s>, ignored",
		  octstr_get_cstr(conn->client_ip));
	    octstr_destroy(pack);
	    continue;
	}
	octstr_destroy(pack);

	if ((!conn->is_wap && msg_type(msg) == smart_sms)
	                  ||
	    (conn->is_wap && msg_type(msg) == wdp_datagram))
	{
	    debug("bb.boxc", 0, "boxc_receiver: message from client received");
	    list_produce(conn->outgoing, msg);
	} else {
	    if (msg_type(msg) == heartbeat) {
		if (msg->heartbeat.load > 0)
		    debug("bb.boxc", 0, "boxc_receiver: heartbeat with load value %ld received",
			  msg->heartbeat.load);
		conn->load = msg->heartbeat.load;
	    }
	    else
		warning(0, "boxc_receiver: unknown msg received from <%s>, ignored",
		  octstr_get_cstr(conn->client_ip));
	    msg_destroy(msg);
	}
    }    
}


/*---------------------------------------------
 * sender thingies
 */

static int send_msg(int fd, Msg *msg)
{
    Octstr *pack;

    pack = msg_pack(msg);

    if (octstr_send(fd, pack) == -1) {
	octstr_destroy(pack);
	return -1;
    }
    octstr_destroy(pack);
    return 0;
}


static void boxc_sender(void *arg)
{
    Msg *msg;
    Boxc *conn = arg;

    list_add_producer(flow_threads);

    while(bb_status != BB_DEAD && conn->alive) {

	list_consume(suspended);	/* block here if suspended */

	if ((msg = list_consume(conn->incoming)) == NULL)
	    break;

	gw_assert((!conn->is_wap && msg_type(msg) == smart_sms)
		                 ||
		  (conn->is_wap && msg_type(msg) == wdp_datagram));

        if (send_msg(conn->fd, msg) == -1) {
	    /* if we fail to send, return msg to the list it came from
	     * before dying off */
	    debug("bb.boxc", 0, "send failed, let's assume that connection had died");
	    list_produce(conn->retry, msg);
	    break;
	}
	msg_destroy(msg);
	debug("bb.boxc", 0, "boxc_sender: sent message");
    }
    /* XXX the client should close the line, instead */
    conn->alive = 0;
    
    list_remove_producer(flow_threads);
}

/*---------------------------------------------------------------
 * accept/create/kill thingies
 */


static Boxc *boxc_create(int fd, Octstr *ip)
{
    Boxc *boxc;
    
    boxc = gw_malloc(sizeof(Boxc));
    boxc->is_wap = 0;
    boxc->load = 0;
    boxc->fd = fd;
    boxc->id = boxid++;		/* XXX  MUTEX! fix later... */
    boxc->client_ip = ip;
    boxc->alive = 1;
    boxc->connect_time = time(NULL);
    return boxc;
}    

static void boxc_destroy(Boxc *boxc)
{
    if (boxc == NULL)
	return;
    
    /* do nothing to the lists, as they are only references */

    if (boxc->fd >= 0)
	close(boxc->fd);
    octstr_destroy(boxc->client_ip);
    gw_free(boxc);
}    



static Boxc *accept_boxc(int fd)
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

    if (is_allowed_ip(box_allow_ip, box_deny_ip, ip)==0) {
	info(0, "Box connection tried from denied host <%s>, disconnected",
	     octstr_get_cstr(ip));
	octstr_destroy(ip);
	return NULL;
    }
    newconn = boxc_create(newfd, ip);
    
    info(0, "Client connected from <%s>", octstr_get_cstr(ip));

    /* XXX TODO: do the hand-shake, baby, yeah-yeah! */

    return newconn;
}



static void run_smsbox(void *arg)
{
    int fd;
    Boxc *newconn;
    long sender;
    
    list_add_producer(flow_threads);
    fd = (int)arg;
    newconn = accept_boxc(fd);

    newconn->incoming = incoming_sms;
    newconn->retry = incoming_sms;
    newconn->outgoing = outgoing_sms;
    
    list_append(smsbox_list, newconn);

    sender = gwthread_create(boxc_sender, newconn);
    if (sender == -1) {
	error(errno, "Failed to start a new thread, disconnecting client <%s>",
	      octstr_get_cstr(newconn->client_ip));
	goto cleanup;
    }
    list_add_producer(newconn->outgoing);
    boxc_receiver(newconn);
    list_remove_producer(newconn->outgoing);

    gwthread_join(sender);

cleanup:    
    list_delete_equal(smsbox_list, newconn);
    boxc_destroy(newconn);

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
    newconn = accept_boxc(fd);
    newconn->is_wap = 1;
    
    /*
     * create a new incoming list for just that box,
     * and add it to list of list pointers, so we can start
     * to route messages to it.
     */

    debug("bb", 0, "setting up systems for new wapbox");
    
    newlist = list_create();
    list_add_producer(newlist);  /* this is released by the sender/receiver if it exits */
    
    newconn->incoming = newlist;
    newconn->retry = incoming_wdp;
    newconn->outgoing = outgoing_wdp;

    sender = gwthread_create(boxc_sender, newconn);
    if (sender == -1) {
	error(errno, "Failed to start a new thread, disconnecting client <%s>",
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
    list_destroy(newlist);
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
    Boxc *conn;
    int i;
    
    ap = list_search(route_info, msg, cmp_route);
    if (ap == NULL) {
	debug("bb.boxc", 0, "Did not find previous routing info for WDP, generating new");
route:

	ap = gw_malloc(sizeof(AddrPar));
	ap->address = octstr_duplicate(msg->wdp_datagram.source_address);
	ap->port = msg->wdp_datagram.source_port;

	if (list_len(wapbox_list) == 0)
	    return NULL;

	/* XXX this SHOULD according to load levels! */
	
	list_lock(wapbox_list);
	i = gw_rand() % list_len(wapbox_list);

	conn = list_get(wapbox_list, i);
	list_unlock(wapbox_list);

	if (conn == NULL) {
	    warning(0, "wapbox_list empty!");
	    return NULL;
	}
	ap->wapboxid = conn->id;
	list_produce(route_info, ap);
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
    list_destroy(route_info);

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






static void wait_for_connections(int fd, void (*function) (void *arg), List *waited)
{
    fd_set rf;
    struct timeval tv;
    int ret;
    
    while(bb_status != BB_DEAD) {

	/* XXX: if we are being shutdowned, as long as there is
	 * messages in incoming list allow new connections, but when
	 * list is empty, exit
	 */
	if (bb_status == BB_SHUTDOWN) {
	    ret = list_wait_until_nonempty(waited);
	    if (ret == -1) break;
	}

	FD_ZERO(&rf);
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	
	if (bb_status != BB_SUSPENDED)
	    FD_SET(fd, &rf);

	ret = select(FD_SETSIZE, &rf, NULL, NULL, &tv);
	if (ret > 0) {
	    gwthread_create(function, (void *)fd);
	    sleep(1);
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
    port = (int)arg;
    
    fd = make_server_socket(port);

    if (fd < 0) {
	panic(0, "Could not open smsbox port %d", port);
    }

    /*
     * infinitely wait for new connections;
     * to shut down the system, SIGTERM is send and then
     * select drops with error, so we can check the status
     */

    wait_for_connections(fd, run_smsbox, incoming_sms);

    /* continue avalanche */
    list_remove_producer(outgoing_sms);

    /* all connections do the same, so that all must remove() before it
     * is completely over
     */

    /* XXX KLUDGE fix when list_wait_until_empty() exists */
    while(list_wait_until_nonempty(smsbox_list)!= -1)
	sleep(1);

    list_destroy(smsbox_list);
    smsbox_list = NULL;
    
    list_remove_producer(flow_threads);
}


static void wapboxc_run(void *arg)
{
    int fd, port;

    list_add_producer(flow_threads);
    port = (int)arg;
    
    fd = make_server_socket(port);

    if (fd < 0) {
	panic(0, "Could not open wapbox port %d", port);
    }

    wait_for_connections(fd, run_wapbox, incoming_wdp);

    /* continue avalanche */

    list_remove_producer(outgoing_wdp);


    /* wait for all connections to die and then remove list
     */
    
    /* XXX KLUDGE fix when list_wait_until_empty() exists */

    
    while(list_wait_until_nonempty(wapbox_list)== 1)
	sleep(1);

    /* wait for wdp_to_wapboxes to exit */
    while(list_consume(wapbox_list)!=NULL)
	;
    
    list_destroy(wapbox_list);
    wapbox_list = NULL;
    
    list_remove_producer(flow_threads);
}



/*-------------------------------------------------------------
 * public functions
 *
 * SMSBOX
 */

int smsbox_start(Config *config)
{
    char *p;
    
    if (smsbox_running) return -1;

    debug("bb", 0, "starting smsbox connection module");

    if ((p = config_get(config_find_first_group(config, "group", "core"),
			"smsbox-port")) == NULL) {
	error(0, "Missing smsbox-port variable, cannot start smsboxes");
	return -1;
    }
    smsbox_port = atoi(p);
    
    smsbox_list = list_create();	/* have a list of connections */
    list_add_producer(outgoing_sms);

    smsbox_running = 1;
    
    if (gwthread_create(smsboxc_run, (void *)smsbox_port) == -1)
	panic(0, "Failed to start a new thread for smsbox connections");

    return 0;
}


int smsbox_restart(Config *config)
{
    if (!smsbox_running) return -1;
    
    /* send new config to clients */

    return 0;
}



/* WAPBOX */

int wapbox_start(Config *config)
{
    ConfigGroup *grp;
    char *p;

    if (wapbox_running) return -1;

    debug("bb", 0, "starting wapbox connection module");
    
    grp = config_find_first_group(config, "group", "core");
    
    if ((p = config_get(grp, "wapbox-port")) == NULL) {
	error(0, "Missing wapbox-port variable, cannot start WAP");
	return -1;
    }
    wapbox_port = atoi(p);
    box_allow_ip = config_get(grp, "box-allow-ip");
    box_deny_ip = config_get(grp, "box-deny-ip");
    if (box_allow_ip != NULL && box_deny_ip == NULL)
	info(0, "Box connection allowed IPs defined without any denied...");
    
    wapbox_list = list_create();	/* have a list of connections */
    list_add_producer(outgoing_wdp);

    if (gwthread_create(wdp_to_wapboxes, NULL) == -1)
 	panic(0, "Failed to start a new thread for wapbox routing");
 
    if (gwthread_create(wapboxc_run, (void *)wapbox_port) == -1)
	panic(0, "Failed to start a new thread for wapbox connections");

    wapbox_running = 1;
    return 0;
}


Octstr *boxc_status(void)
{
    char tmp[1024], buf[256];
    int i;
    time_t orig, t;
    Boxc *bi;

    orig = time(NULL);
    /*
     * XXX: this will cause segmentation fault if this is called
     *    between 'destroy_list and setting list to NULL calls.
     *    Ok, this has to be fixed, but now I am too tired.
     */
    
    sprintf(tmp, "Box connections:<BR>\n");

    if (wapbox_list) {
	list_lock(wapbox_list);
	for(i=0; i < list_len(wapbox_list); i++) {
	    bi = list_get(wapbox_list, i);
	    t = orig - bi->connect_time;
	    sprintf(buf, "&nbsp;&nbsp;&nbsp;&nbsp;wapbox %s "
		    "(on-line %ldd %ldh %ldm %lds)<br>\n",
		    octstr_get_cstr(bi->client_ip),
		    t/3600/24, t/3600%24, t/60%60, t%60);
	    strcat(tmp, buf);
	}
	list_unlock(wapbox_list);
    }
    if (smsbox_list) {
	list_lock(smsbox_list);
	for(i=0; i < list_len(smsbox_list); i++) {
	    bi = list_get(smsbox_list, i);
	    t = orig - bi->connect_time;
	    sprintf(buf, "&nbsp;&nbsp;&nbsp;&nbsp;smsbox %s "
		    "(on-line %ldd %ldh %ldm %lds)<br>\n",
		    octstr_get_cstr(bi->client_ip),
		    t/3600/24, t/3600%24, t/60%60, t%60);
	    strcat(tmp, buf);
	}
	list_unlock(smsbox_list);
    }
    return octstr_create(tmp);
}

