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
#include "wml_compiler.h"
#include "heartbeat.h"
#include "wap/wap.h"
#include "wap-appl.h"
#include "wap_push_ota.h"
#include "wap_push_ppg.h"
#include "msg.h"
#include "bb.h"


static Config *cfg = NULL;
static Octstr *bearerbox_host;
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
	bearerbox_host = octstr_create(s);
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
	    log_set_syslog(NULL, 0);
	    debug("bbox",0,"syslog parameter is none");
	} else {
	    log_set_syslog("wapbox", atoi(s));
	    debug("bbox",0,"syslog parameter is %d", atoi(s));
	}
    } else {
	log_set_syslog(NULL, 0);
	debug("bbox",0,"no syslog parameter");
    }
    
    
    if (logfile != NULL) {
	log_open(logfile, logfilelevel);
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
	if (program_status != shutting_down) {
	    error(0, "SIGINT received, let's die.");
	    program_status = shutting_down;
	    break;
	}
	break;
    
    case SIGHUP:
	warning(0, "SIGHUP received, catching and re-opening logs");
	log_reopen();
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
        msg->wdp_datagram.user_data =
	    octstr_duplicate(dgram->u.T_DUnitdata_Req.user_data);

	write_to_bearerbox(msg);
    }

    wap_event_destroy(dgram);
}

int main(int argc, char **argv) 
{
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
    info(0, "Kannel wapbox version %s starting up.", VERSION);
    
    wsp_session_init(&wtp_resp_dispatch_event,
                     &wtp_initiator_dispatch_event,
                     &wap_appl_dispatch,
                     &wap_push_ppg_dispatch_event);
    wsp_unit_init(&dispatch_datagram, &wap_appl_dispatch);
    wsp_push_client_init(&wsp_push_client_dispatch_event, 
                         &wtp_resp_dispatch_event);
    
    wtp_initiator_init(&dispatch_datagram, &wsp_session_dispatch_event);
    wtp_resp_init(&dispatch_datagram, &wsp_session_dispatch_event,
                  &wsp_push_client_dispatch_event);
    wap_appl_init();
    wap_push_ota_init(&wsp_session_dispatch_event, &wsp_unit_dispatch_event);
    wap_push_ppg_init(&wap_push_ota_dispatch_event);

    wml_init();
    
    if (bearerbox_host == NULL)
    	bearerbox_host = octstr_create(BB_DEFAULT_HOST);
    connect_to_bearerbox(bearerbox_host, bearerbox_port);

    wap_push_ota_bb_address_set(bearerbox_host);

    program_status = running;
    heartbeat_thread = heartbeat_start(write_to_bearerbox, heartbeat_freq, 
    	    	    	    	       wap_appl_get_load);

    while (program_status != shutting_down) {
	WAPEvent *dgram;

	msg = read_from_bearerbox();
	if (msg == NULL)
	    break;
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

    info(0, "Kannel wapbox terminating.");
    
    program_status = shutting_down;
    heartbeat_stop(heartbeat_thread);
    wtp_initiator_shutdown();
    wtp_resp_shutdown();
    wsp_push_client_shutdown();
    wsp_unit_shutdown();
    wsp_session_shutdown();
    wap_appl_shutdown();
    wap_push_ota_shutdown();
    wap_push_ppg_shutdown();
    wml_shutdown();
    close_connection_to_bearerbox();
    wsp_http_map_destroy();
    config_destroy(cfg);
    octstr_destroy(bearerbox_host);
    gwlib_shutdown();
    return 0;
}
