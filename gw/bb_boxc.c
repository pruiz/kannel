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
#include <unistd.h>
#include <signal.h>
#include <assert.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "new_bb.h"

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern List *incoming_sms;
extern List *outgoing_sms;
extern List *incoming_wdp;
extern List *outgoing_wdp;

/* our own thingies */

static volatile sig_atomic_t smsbox_running;
static volatile sig_atomic_t wapbox_running;
static List	*wapbox_list;
static List	*smsbox_list;

static int	smsbox_port;
static int	wapbox_port;


typedef struct _boxc {
    int   	fd;
    int		is_wap;
    Octstr    	*client_ip;
    List      	*incoming;
    List      	*retry;   	/* If sending fails */
    List       	*outgoing;
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
    
    debug("bb", 0, "box connection receiver");

    
    /* remove messages from socket until it is closed */
    while(bb_status != bb_dead) {
	ret = octstr_recv(conn->fd, &pack);

	if (ret < 1)
	    break;

	if ((msg = msg_unpack(pack))==NULL) {
	    debug("bb", 0, "Received garbage from <%s>, ignored",
		  octstr_get_cstr(conn->client_ip));
	    octstr_destroy(pack);
	    continue;
	}
	octstr_destroy(pack);

	if ((!conn->is_wap && msg_type(msg) == smart_sms)
	                  ||
	    (conn->is_wap && msg_type(msg) == wdp_datagram))
	{
	    debug("bb", 0, "boxc: %s message received", conn->is_wap ? "wap" : "sms");
	    list_produce(conn->outgoing, msg);
	} else {
	    if (msg_type(msg) == heartbeat)
		debug("bb", 0, "boxc: heartbeat with load value %ld received",
		      msg->heartbeat.load);
	    else
		warning(0, "boxc: unknown msg received from <%s>, ignored",
		  octstr_get_cstr(conn->client_ip));
	    msg_destroy(msg);
	}
    }    
    debug("bb", 0, "receiver done.");
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


static void *boxc_sender(void *arg)
{
    Msg *msg;
    Boxc *conn = arg;
    
    debug("bb", 0, "box connection sender");

    while(bb_status != bb_dead) {

	if (bb_status == bb_suspended)
	    ; // wait_for_status_change(&bb_status, bb_suspended);
	
	if ((msg = list_consume(conn->incoming)) == NULL)
	    break;

	debug("bb", 0, "boxc_sender: sending message");
	
        if (send_msg(conn->fd, msg) == -1) {
	    /* if we fail to send, return msg to the list it came from
	     * before dying off */
	    list_produce(conn->retry, msg);
	    break;
	}
	msg_destroy(msg);
    }
    return NULL;
}

/*---------------------------------------------------------------
 * accept/create thingies
 */


static Boxc *boxc_create(int fd, char *ip)
{
    Boxc *boxc;
    
    boxc = gw_malloc(sizeof(Boxc));
    boxc->is_wap = 0;
    boxc->fd = fd;
    boxc->client_ip = octstr_create(ip);
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

    int newfd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    char accept_ip[NI_MAXHOST];

    client_addr_len = sizeof(client_addr);

    newfd = accept(fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (newfd < 0)
	return NULL;

    /* what the heck does this do?
     * XXX
     */
    
    memset(accept_ip, 0, sizeof(accept_ip));
    getnameinfo((struct sockaddr *)&client_addr, client_addr_len,
		accept_ip, sizeof(accept_ip), 
		NULL, 0, NI_NUMERICHOST);

    /*
     * XXX here we should check the IP if it is an acceptable one
     */
    
    newconn = boxc_create(newfd, accept_ip);
    
    info(0, "Client connected from <%s>", octstr_get_cstr(newconn->client_ip));

    /* XXX TODO: do the hand-shake, baby, yeah-yeah! */

    return newconn;
}



static void *run_smsbox(void *arg)
{
    int fd;
    Boxc *newconn;
    pthread_t sender;
    
    fd = (int)arg;
    newconn = accept_boxc(fd);

    newconn->incoming = incoming_sms;
    newconn->retry = incoming_sms;
    newconn->outgoing = outgoing_sms;
    
    list_append(smsbox_list, newconn);

    if ((int)(sender = start_thread(0, boxc_sender, newconn, 0)) == -1) {
	error(errno, "Failed to start a new thread, disconnecting client <%s>",
	      octstr_get_cstr(newconn->client_ip));
	goto cleanup;
    }
    list_add_producer(newconn->outgoing);
    boxc_receiver(newconn);
    list_remove_producer(newconn->outgoing);

    if (pthread_join(sender, NULL) != 0)
	error(0, "Join failed in wapbox");

cleanup:    
    list_delete_equal(smsbox_list, newconn);
    boxc_destroy(newconn);

    return NULL;
}



static void *run_wapbox(void *arg)
{
    int fd;
    Boxc *newconn;
    List *newlist;
    pthread_t sender;

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

    list_append(wapbox_list, newconn);
    
    if ((int)(sender = start_thread(0, boxc_sender, newconn, 0)) == -1) {
	error(errno, "Failed to start a new thread, disconnecting client <%s>",
	      octstr_get_cstr(newconn->client_ip));
	goto cleanup;
    }
    list_add_producer(newconn->outgoing);
    boxc_receiver(newconn);
    list_remove_producer(newconn->outgoing);

    /* XXX remove from routing info! */
    
    list_remove_producer(newlist);

    if (pthread_join(sender, NULL) != 0)
	error(0, "Join failed in wapbox");

cleanup:    
    list_delete_equal(wapbox_list, newconn);
    list_destroy(newlist);
    boxc_destroy(newconn);

    return NULL;
}


/*------------------------------------------------*/


/*
 * this thread listens to incoming_wdp list
 * and then routs messages to proper wapbox
 */
static void *wdp_to_wapboxes(void *arg)
{
    List *route_info;
    Boxc *conn;
    Msg *msg;

    debug("bb", 0, "starting incoming WDP router module");

    route_info = list_create();

    
    while(bb_status != bb_dead) {

	if (bb_status == bb_suspended)
	    ; // wait_for_status_change(&bb_status, suspended);

	if ((msg = list_consume(incoming_wdp)) == NULL)
	    break;

	assert(msg_type(msg) == wdp_datagram);

	/*
	 * this is not the way to route these, but fix this later
	 */
	list_wait_until_nonempty(wapbox_list);
	conn = list_get(wapbox_list, 0);
	list_produce(conn->incoming, msg);
    }
    debug("bb", 0, "wdp to wapboxes router: exiting");
    list_destroy(route_info);
    return NULL;
}






static void wait_for_connections(int fd, void *(*function) (void *arg))
{
    fd_set rf;
    int ret;

    /*
     * XXX: future fixing: in a case of shutdown, we should
     *    allow new connections to empty the list... or do
     *    other things to empty it
     */

    debug("bb", 0, "Wait for connections, fd = %d", fd);
    
    while(bb_status != bb_dead && bb_status != bb_shutdown) {
	
	FD_ZERO(&rf);

	if (bb_status == bb_running || bb_status == bb_shutdown)
	    FD_SET(fd, &rf);

	ret = select(FD_SETSIZE, &rf, NULL, NULL, NULL);
	if (ret > 0) {
	    start_thread(1, function, (void *)fd, 0);
	    sleep(1);
	} else if (ret < 0) {
	    if(errno==EINTR) continue;
	    if(errno==EAGAIN) continue;
	    error(errno, "wait_for_connections failed");
	}
    }
}



static void *smsboxc_run(void *arg)
{
    int fd;
    int port;

    port = (int)arg;
    
    fd = make_server_socket(port);

    /*
     * infinitely wait for new connections;
     * to shut down the system, SIGTERM is send and then
     * select drops with error, so we can check the status
     */

    wait_for_connections(fd, run_smsbox);

    /* continue avalanche */
    list_remove_producer(outgoing_sms);

    /* all connections do the same, so that all must remove() before it
     * is completely over
     */

    return NULL;
}


static void *wapboxc_run(void *arg)
{
    int fd, port;
    port = (int)arg;

    debug("bb", 0, "main wapbox listener");

    
    fd = make_server_socket(port);

    wait_for_connections(fd, run_wapbox);

    /* continue avalanche */

    list_remove_producer(outgoing_wdp);

    return NULL;
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
    
    if ((int)(start_thread(0, smsboxc_run, (void *)smsbox_port, 0)) == -1)
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
    char *p;

    if (wapbox_running) return -1;

    debug("bb", 0, "starting wapbox connection module");
    
    if ((p = config_get(config_find_first_group(config, "group", "core"),
			"wapbox-port")) == NULL) {
	error(0, "Missing wapbox-port variable, cannot start WAP");
	return -1;
    }
    wapbox_port = atoi(p);

    wapbox_list = list_create();	/* have a list of connections */
    list_add_producer(outgoing_wdp);

    if (start_thread(0, wdp_to_wapboxes, NULL, 0) == -1)
 	panic(0, "Failed to start a new thread for wapbox routing");
 
    if (start_thread(0, wapboxc_run, (void *)wapbox_port, 0) == -1)
	panic(0, "Failed to start a new thread for wapbox connections");

    wapbox_running = 1;
    return 0;
}


