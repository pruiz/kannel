/*
 * bb_udpc.c : bearerbox UDP sender/receiver module
 *
 * handles start/restart/shutdown/suspend/die operations of the UDP
 * WDP interface
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
#include <fcntl.h>
#include <assert.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "bearerbox.h"

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern List *incoming_wdp;

extern Counter *incoming_wdp_counter;
extern Counter *outgoing_wdp_counter;

extern List *flow_threads;
extern List *suspended;
extern List *isolated;

/* our own thingies */

static volatile sig_atomic_t udp_running;
static List *udpc_list;


typedef struct _udpc {
    int fd;
    Octstr *addr;
    List *outgoing_list;
    long receiver;
} Udpc;


/*
 * IP numbers which are allowed or denied us of the bearerbox via UDP.
 */
static Octstr *allow_ip;
static Octstr *deny_ip;


/* forward declarations */

static void udpc_destroy(Udpc *udpc);

/*-------------------------------------------------
 *  receiver thingies
 */

static void udp_receiver(void *arg)
{
    Octstr *datagram, *cliaddr;
    int ret;
    Msg *msg;
    Udpc *conn = arg;
    Octstr *ip;

    list_add_producer(incoming_wdp);
    list_add_producer(flow_threads);
    gwthread_wakeup(MAIN_THREAD_ID);
    
    /* remove messages from socket until it is closed */
    while (bb_status != BB_DEAD && bb_status != BB_SHUTDOWN) {

	list_consume(isolated);	/* block here if suspended/isolated */

	if (read_available(conn->fd, 100000) < 1)
	    continue;

	ret = udp_recvfrom(conn->fd, &datagram, &cliaddr);
	if (ret == -1) {
	    if (errno == EAGAIN)
		/* No datagram available, don't block. */
		continue;

	    error(errno, "Failed to receive an UDP");
	    /*
	     * just continue, or is there ANY error that would result
	     * in situation where it would be better to break; or even
	     * die off?     - Kalle 28.2
	     */
	    continue;
	}

	/* discard the message if the client is not allowed */
    	ip = udp_get_ip(cliaddr);
	if (!is_allowed_ip(allow_ip, deny_ip, ip)) {
    	    warning(0, "UDP: Discarding packet from %s, IP is denied.",
		       octstr_get_cstr(ip));
    	    octstr_destroy(datagram);
	} else {
	    debug("bb.udp", 0, "datagram received");
	    msg = msg_create(wdp_datagram);
    
	    msg->wdp_datagram.source_address = udp_get_ip(cliaddr);
	    msg->wdp_datagram.source_port    = udp_get_port(cliaddr);
	    msg->wdp_datagram.destination_address = udp_get_ip(conn->addr);
	    msg->wdp_datagram.destination_port    = udp_get_port(conn->addr);
	    msg->wdp_datagram.user_data = datagram;
    
	    list_produce(incoming_wdp, msg);
	    counter_increase(incoming_wdp_counter);
	}

	octstr_destroy(cliaddr);
	octstr_destroy(ip);
    }    
    list_remove_producer(incoming_wdp);
    list_remove_producer(flow_threads);
}


/*---------------------------------------------
 * sender thingies
 */

static int send_udp(int fd, Msg *msg)
{
    Octstr *cliaddr;
    int ret;

    cliaddr = udp_create_address(msg->wdp_datagram.destination_address,
				 msg->wdp_datagram.destination_port);
    ret = udp_sendto(fd, msg->wdp_datagram.user_data, cliaddr);
    if (ret == -1)
	error(0, "WDP/UDP: could not send UDP datagram");
    octstr_destroy(cliaddr);
    return ret;
}


static void udp_sender(void *arg)
{
    Msg *msg;
    Udpc *conn = arg;

    list_add_producer(flow_threads);
    while(bb_status != BB_DEAD) {

	list_consume(suspended);	/* block here if suspended */

	if ((msg = list_consume(conn->outgoing_list)) == NULL)
	    break;

	debug("bb.udp", 0, "udp: sending message");
	
        if (send_udp(conn->fd, msg) == -1)
	    /* ok, we failed... tough
	     * XXX log the message or something like that... but this
	     * is not as fatal as it is with SMS-messages...
	     */ {
	    msg_destroy(msg);
	    continue;
	}
	counter_increase(outgoing_wdp_counter);
	msg_destroy(msg);
    }
    gwthread_join(conn->receiver);

    udpc_destroy(conn);
    list_remove_producer(flow_threads);
}

/*---------------------------------------------------------------
 * accept/create thingies
 */


static Udpc *udpc_create(int port, char *interface_name)
{
    Udpc *udpc;
    Octstr *os;
    int fl;
    
    udpc = gw_malloc(sizeof(Udpc));
    udpc->fd = udp_bind(port, interface_name);

    os = octstr_create(interface_name);
    udpc->addr = udp_create_address(os, port);
    octstr_destroy(os);
    if (udpc->addr == NULL) {
	error(0, "updc_create: could not resolve interface <%s>",
	      interface_name);
	close(udpc->fd);
	gw_free(udpc);
	return NULL;
    }

    fl = fcntl(udpc->fd, F_GETFL);
    fcntl(udpc->fd, F_SETFL, fl | O_NONBLOCK);

    os = udp_get_ip(udpc->addr);
    debug("bb.udp", 0, "udpc_create: Bound to UDP <%s:%d>",
	  octstr_get_cstr(os), udp_get_port(udpc->addr));

    octstr_destroy(os);
    
    udpc->outgoing_list = list_create();

    return udpc;
}    


static void udpc_destroy(Udpc *udpc)
{
    if (udpc == NULL)
	return;

    if (udpc->fd >= 0)
	close(udpc->fd);
    octstr_destroy(udpc->addr);
    gw_assert(list_len(udpc->outgoing_list) == 0);
    list_destroy(udpc->outgoing_list, NULL);

    gw_free(udpc);
}    


static int add_service(int port, char *interface_name)
{
    Udpc *udpc;
    
    if ((udpc = udpc_create(port, interface_name)) == NULL)
	goto error;
    list_add_producer(udpc->outgoing_list);

    udpc->receiver = gwthread_create(udp_receiver, udpc);
    if (udpc->receiver == -1)
	goto error;

    if (gwthread_create(udp_sender, udpc) == -1)
	goto error;

    list_append(udpc_list, udpc);
    return 0;
    
error:    
    error(0, "Failed to start UDP receiver/sender thread");
    udpc_destroy(udpc);
    return -1;
}



/*-------------------------------------------------------------
 * public functions
 *
 */

int udp_start(Cfg *cfg)
{
    Octstr *interface_name;
    CfgGroup *grp;
    
    if (udp_running) return -1;

    
    debug("bb.udp", 0, "starting UDP sender/receiver module");

    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    interface_name = cfg_get(grp, octstr_imm("wdp-interface-name"));
    if (interface_name == NULL) {
	error(0, "Missing wdp-interface-name variable, cannot start UDP");
	return -1;
    }
    
    allow_ip = cfg_get(grp, octstr_imm("udp-allow-ip"));
    deny_ip = cfg_get(grp, octstr_imm("udp-deny-ip"));
    
    udpc_list = list_create();	/* have a list of running systems */

    add_service(9200, octstr_get_cstr(interface_name));	   /* wsp 	*/
    add_service(9201, octstr_get_cstr(interface_name));	   /* wsp/wtp	*/
    add_service(9202, octstr_get_cstr(interface_name));    /* wsp/wtls	*/
    add_service(9203, octstr_get_cstr(interface_name));    /* wsp/wtp/wtls */
    /* add_service(9204, octstr_get_cstr(interface_name));  * vcard	*/
    /* add_service(9205, octstr_get_cstr(interface_name));  * vcal	*/
    /* add_service(9206, octstr_get_cstr(interface_name));  * vcard/wtls */
    /* add_service(9207, octstr_get_cstr(interface_name));  * vcal/wtls	*/
    
    octstr_destroy(interface_name);
    
    list_add_producer(incoming_wdp);
    udp_running = 1;
    return 0;
}


/*
 * this function receives an WDP message and adds it to
 * corresponding outgoing_list.
 */
int udp_addwdp(Msg *msg)
{
    int i;
    Udpc *udpc;
    
    if (!udp_running) return -1;
    assert(msg != NULL);
    assert(msg_type(msg) == wdp_datagram);
    
    list_lock(udpc_list);
    /* select in which list to add this */
    for (i=0; i < list_len(udpc_list); i++) {
		udpc = list_get(udpc_list, i);

		if (msg->wdp_datagram.source_port == udp_get_port(udpc->addr))
		{
	    	list_produce(udpc->outgoing_list, msg);
	    	list_unlock(udpc_list);
	    	return 0;
		}
    }
    list_unlock(udpc_list);
    return -1;
}

int udp_shutdown(void)
{
    if (!udp_running) return -1;

    debug("bb.thread", 0, "udp_shutdown: Starting avalanche");
    list_remove_producer(incoming_wdp);
    return 0;
}


int udp_die(void)
{
    Udpc *udpc;

    if (!udp_running) return -1;
    
    /*
     * remove producers from all outgoing lists.
     */
    debug("bb.udp", 0, "udp_die: removing producers from udp-lists");

    while((udpc = list_consume(udpc_list)) != NULL) {
	list_remove_producer(udpc->outgoing_list);
    }
    list_destroy(udpc_list, NULL);
    udp_running = 0;
    
    octstr_destroy(allow_ip);
    octstr_destroy(deny_ip);
    allow_ip = NULL;
    deny_ip = NULL;
    
    return 0;
}


int udp_outgoing_queue(void)
{
    int i, q = 0;
    Udpc *udpc;

    if (!udp_running || udpc_list == NULL)
	return 0;

    list_lock(udpc_list);
    for (i=0; i < list_len(udpc_list); i++) {
	udpc = list_get(udpc_list, i);
	q += list_len(udpc->outgoing_list);
    }
    list_unlock(udpc_list);
    return q;
}
