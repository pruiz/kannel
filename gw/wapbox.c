/*
 * wapbox.c - main program for wapbox
 *
 * This module contains the main program for the WAP box of the WAP gateway.
 * See the architecture documentation for details.
 */

#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "shared.h"
#include "heartbeat.h"
#include "wap/wap.h"
#include "wap-appl.h"
#include "msg.h"
#include "bb.h"


static Config *cfg = NULL;
static char *bearerbox_host = BB_DEFAULT_HOST;
static int bearerbox_port = BB_DEFAULT_WAPBOX_PORT;
static int heartbeat_freq = BB_DEFAULT_HEARTBEAT;
static long heartbeat_thread;
static char *logfile = NULL;
static int logfilelevel = 0;
static char *http_proxy_host = NULL;
static int http_proxy_port = -1;
static List *http_proxy_exceptions = NULL;
static Octstr *http_proxy_username = NULL;
static Octstr *http_proxy_password = NULL;


static enum {
    initializing,
    running,
    aborting,
    aborting_with_prejudice
} run_status = initializing;


/*
 * NOTE: the following variable belongs to a kludge, and will go away
 * when the configuration parsing is reworked. Right now config_get()
 * returns the last appearance of a given configuration variable, only.
 * We want to be able to configure several URL mappings at once.
 * To do so, you write a line "map-url-max = somenumber" in the config
 * file, and then write "map-url-0 = ...", "map-url-1 = ...", etc.
 * The mappings will be added in numerical sequence, which is a feature
 * to keep when reworking the configuration parsing, because the mapping
 * operation is order-sensitive
 */
static int map_url_max = 9;

static void read_config(char *filename) 
{
    ConfigGroup *grp;
    char *s;
    int i;
    
    cfg = config_create(filename);
    if (config_read(cfg) == -1)
	panic(0, "Couldn't read configuration from `%s'.", filename);
    config_dump(cfg);
    
    /*
     * first we take the port number in bearerbox from the main
     * core group in configuration file
     */
    if (config_sanity_check(cfg)==-1)
	panic(0, "Cannot start with malformed configuration");
    
    grp = config_find_first_group(cfg, "group", "core");
    
    if ((s = config_get(grp, "wapbox-port")) == NULL)
	panic(0, "No 'wapbox-port' in core group");
    bearerbox_port = atoi(s);
    
    if ((s = config_get(grp, "http-proxy-host")) != NULL)
	http_proxy_host = s;
    if ((s = config_get(grp, "http-proxy-port")) != NULL)
	http_proxy_port = atoi(s);
    if ((s = config_get(grp, "http-proxy-username")) != NULL)
	http_proxy_username = octstr_create(s);
    if ((s = config_get(grp, "http-proxy-password")) != NULL)
	http_proxy_password = octstr_create(s);
    if ((s = config_get(grp, "http-proxy-exceptions")) != NULL) {
	Octstr *os;
	
	os = octstr_create(s);
	http_proxy_exceptions = octstr_split_words(os);
	octstr_destroy(os);
    }
    
    /*
     * get the remaining values from the wapbox group
     */
    if ((grp = config_find_first_group(cfg, "group", "wapbox")) == NULL)
	panic(0, "No 'wapbox' group in configuration");
    
    if ((s = config_get(grp, "bearerbox-host")) != NULL)
	bearerbox_host = s;
    if ((s = config_get(grp, "heartbeat-freq")) != NULL)
	heartbeat_freq = atoi(s);
    
    if ((s = config_get(grp, "log-file")) != NULL)
	logfile = s;
    if ((s = config_get(grp, "log-level")) != NULL)
	logfilelevel = atoi(s);

    /* configure URL mappings */
    if ((s = config_get(grp, "map-url-max")) != NULL)
	map_url_max = atoi(s);
    if ((s = config_get(grp, "device-home")) != NULL)
	wsp_http_map_url_config_device_home(s);
    if ((s = config_get(grp, "map-url")) != NULL)
	wsp_http_map_url_config(s);
    for (i=0; i<=map_url_max; i++) {
	char buf[32];
	sprintf(buf, "map-url-%d", i);
	if ((s = config_get(grp, buf)) != NULL)
	    wsp_http_map_url_config(s);
    }

    /* Get syslog parameters */
    if ((s = config_get(grp, "syslog-level")) != NULL) {
	if (strcmp(s,"none") == 0) {
	    set_syslog(NULL, 0);
	    debug("bbox",0,"syslog parameter is none");
	} else {
	    set_syslog("wapbox", atoi(s));
	    debug("bbox",0,"syslog parameter is %d", atoi(s));
	}
    } else {
	set_syslog(NULL, 0);
	debug("bbox",0,"no syslog parameter");
    }
    
    
    if (logfile != NULL) {
	open_logfile(logfile, logfilelevel);
	info(0, "Starting to log to file %s level %d", logfile, logfilelevel);
    }
    wsp_http_map_url_config_info();	/* debugging aid */
    
    if (http_proxy_host != NULL && http_proxy_port > 0) {
	Octstr *os;
    
	os = octstr_create(http_proxy_host);
	http_use_proxy(os, http_proxy_port, http_proxy_exceptions,
	    	       http_proxy_username, http_proxy_password);
	octstr_destroy(os);
    }
}


static int connect_to_bearer_box(void) 
{
    int s;
    
    s = tcpip_connect_to_server(bearerbox_host, bearerbox_port);
    if (s == -1)
	panic(0, "Couldn't connect to bearer box %s:%d.",
	      bearerbox_host, bearerbox_port);
    debug("bbox", 0, "Bearerbox connected");
    return s;
}


static Msg *msg_receive(int s) 
{
    Octstr *os;
    Msg *msg;
    
    while (run_status == running && !read_available(s, 1000*1000))
	continue;
    if (run_status != running)
	return NULL;
    if (octstr_recv(s, &os) < 1)
	return NULL;
    msg = msg_unpack(os);
    
    if (msg == NULL)
	return NULL;
    octstr_destroy(os);
#if 0
    debug("wap", 0, "Received message from bearer box:");
    msg_dump(msg, 0);
#endif
    return msg;
}


static void msg_send(int s, Msg *msg) 
{
    Octstr *os;

#if 0
    if (msg->type != heartbeat) {
	debug("wap", 0, "Sending message to bearer box:");
	msg_dump(msg, 0);
    }
#endif

    os = msg_pack(msg);
    if (os == NULL)
	panic(0, "msg_pack failed");
    if (octstr_send(s, os) == -1)
	error(0, "wapbox: octstr_send failed");
    octstr_destroy(os);
}



/*
 * This is the queue of messages that are being sent to the bearerbox.
 */
static List *queue = NULL;

static void empty_queue_thread(void *arg) 
{
    Msg *msg;
    int socket;
    
    socket = *(int *) arg;
    while (run_status == running) {
	msg = list_consume(queue);
	if (msg != NULL)
	    msg_send(socket, msg);
        msg_destroy(msg);
    }
}

static void put_msg_on_queue(Msg *msg)
{
    list_produce(queue, msg);
}


static void signal_handler(int signum) 
{
    /* 
     * Signals are normally delivered to all threads.  We only want
     * to handle each signal once for the entire box, so we ignore
     * all except the one sent to the main thread. 
     */
    if (gwthread_self() != MAIN_THREAD_ID)
	return;
    
    switch (signum) {
    case SIGINT:
	switch (run_status) {
	case aborting_with_prejudice:
	    break;
	case aborting:
	    error(0, "New SIGINT received, let's die harder");
	    run_status = aborting_with_prejudice;
	    break;
	default:
	    error(0, "SIGINT received, let's die.");
	    run_status = aborting;
	    break;
	}
	break;
    
    case SIGHUP:
	warning(0, "SIGHUP received, catching and re-opening logs");
	reopen_log_files();
	break;
    
    /* 
     * It would be more proper to use SIGUSR1 for this, but on some
     * platforms that's reserved by the pthread support. 
     */
    case SIGQUIT:
	warning(0, "SIGQUIT received, reporting memory usage.");
	gw_check_leaks();
	break;
    }
}


static void setup_signal_handlers(void) 
{
    struct sigaction act;
    
    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGPIPE, &act, NULL);
}


static void dispatch_datagram(WAPEvent *dgram)
{
    Msg *msg;

    gw_assert(dgram != NULL);
    if (dgram->type != T_DUnitdata_Req) {
        warning(0, "dispatch_datagram received event of unexpected type.");
        wap_event_dump(dgram);
    } else {
        msg = msg_create(wdp_datagram);
        msg->wdp_datagram.source_address =
            octstr_duplicate(dgram->u.T_DUnitdata_Req.addr_tuple->local->address);
        msg->wdp_datagram.source_port =
            dgram->u.T_DUnitdata_Req.addr_tuple->local->port;
        msg->wdp_datagram.destination_address =
            octstr_duplicate(dgram->u.T_DUnitdata_Req.addr_tuple->remote->address);
        msg->wdp_datagram.destination_port =
            dgram->u.T_DUnitdata_Req.addr_tuple->remote->port;
        msg->wdp_datagram.user_data = dgram->u.T_DUnitdata_Req.user_data;
	dgram->u.T_DUnitdata_Req.user_data = NULL;

	put_msg_on_queue(msg);
    }

    wap_event_destroy(dgram);
}

int main(int argc, char **argv) 
{
    int bbsocket;
    int cf_index;
    Msg *msg;
    
    gwlib_init();
    cf_index = get_and_set_debugs(argc, argv, NULL);
    
    if (argc > cf_index)
	read_config(argv[cf_index]);
    else
	read_config("kannel.conf");
    
    report_versions("wapbox");

    setup_signal_handlers();
    
    info(0, "------------------------------------------------------------");
    info(0, "WAP box version %s starting up.", VERSION);
    
    wsp_session_init(&wtp_resp_dispatch_event,
                     &wtp_initiator_dispatch_event,
                     &wap_appl_dispatch);
    wsp_unit_init(&dispatch_datagram, &wap_appl_dispatch);
    
    wtp_initiator_init(&dispatch_datagram, &wsp_session_dispatch_event);
    wtp_resp_init(&dispatch_datagram, &wsp_session_dispatch_event);
    wap_appl_init();
    
    bbsocket = connect_to_bearer_box();
    queue = list_create();
    
    run_status = running;
    list_add_producer(queue);
    heartbeat_thread =
        heartbeat_start(put_msg_on_queue, heartbeat_freq, wap_appl_get_load);
    gwthread_create(empty_queue_thread, &bbsocket);

    while (run_status == running && (msg = msg_receive(bbsocket)) != NULL) {
	WAPEvent *dgram;

	dgram = wap_event_create(T_DUnitdata_Ind);
	dgram->u.T_DUnitdata_Ind.addr_tuple = wap_addr_tuple_create(
		msg->wdp_datagram.source_address,
		msg->wdp_datagram.source_port,
		msg->wdp_datagram.destination_address,
		msg->wdp_datagram.destination_port);
	dgram->u.T_DUnitdata_Ind.user_data = msg->wdp_datagram.user_data;
	msg->wdp_datagram.user_data = NULL;
	msg_destroy(msg);

	wap_dispatch_datagram(dgram);
    }

    info(0, "WAP box terminating.");
    
    run_status = aborting;
    list_remove_producer(queue);
    heartbeat_stop(heartbeat_thread);
    gwthread_join_every(empty_queue_thread);
    wtp_initiator_shutdown();
    wtp_resp_shutdown();
    wsp_unit_shutdown();
    wsp_session_shutdown();
    list_destroy(queue, msg_destroy_item);
    wap_appl_shutdown();
    wsp_http_map_destroy();
    config_destroy(cfg);
    gwlib_shutdown();
    return 0;
}
