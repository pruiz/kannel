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


static Octstr *bearerbox_host;
static long bearerbox_port = BB_DEFAULT_WAPBOX_PORT;
static long heartbeat_freq = BB_DEFAULT_HEARTBEAT;
static long heartbeat_thread;


static void read_config(Octstr *filename) 
{
    CfgGroup *grp;
    Octstr *s;
    long i;
    Cfg *cfg;
    Octstr *logfile;
    long logfilelevel;
    Octstr *http_proxy_host;
    long http_proxy_port;
    List *http_proxy_exceptions;
    Octstr *http_proxy_username;
    Octstr *http_proxy_password;
    long map_url_max;

    cfg = cfg_create(filename);
    if (cfg_read(cfg) == -1)
	panic(0, "Couldn't read configuration from `%s'.", 
	      octstr_get_cstr(filename));
    cfg_dump(cfg);
    
    /*
     * Extract info from the core group.
     */

    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    if (grp == NULL)
    	panic(0, "No 'core' group in configuration.");
    
    if (cfg_get_integer(&bearerbox_port,grp,octstr_imm("wapbox-port")) == -1)
	panic(0, "No 'wapbox-port' in core group");
    
    http_proxy_host = cfg_get(grp, octstr_imm("http-proxy-host"));
    http_proxy_port = -1;
    cfg_get_integer(&http_proxy_port, grp, octstr_imm("http-proxy-port"));
    http_proxy_username = cfg_get(grp, octstr_imm("http-proxy-username"));
    http_proxy_password = cfg_get(grp, octstr_imm("http-proxy-password"));
    http_proxy_exceptions = 
    	cfg_get_list(grp, octstr_imm("http-proxy-exceptions"));
    if (http_proxy_host != NULL && http_proxy_port > 0) {
	http_use_proxy(http_proxy_host, http_proxy_port, 
	    	       http_proxy_exceptions, http_proxy_username, 
		       http_proxy_password);
    }
    octstr_destroy(http_proxy_host);
    octstr_destroy(http_proxy_username);
    octstr_destroy(http_proxy_password);
    list_destroy(http_proxy_exceptions, octstr_destroy_item);

    
    /*
     * And the rest of the info comes from the wapbox group.
     */

    grp = cfg_get_single_group(cfg, octstr_imm("wapbox"));
    if (grp == NULL)
	panic(0, "No 'wapbox' group in configuration.");
    
    bearerbox_host = cfg_get(grp, octstr_imm("bearerbox-host"));
    
    logfile = cfg_get(grp, octstr_imm("log-file"));
    if (cfg_get_integer(&logfilelevel, grp, octstr_imm("log-level")) == -1)
    	logfilelevel = 0;
    if (logfile != NULL) {
	log_open(octstr_get_cstr(logfile), logfilelevel);
	info(0, "Starting to log to file %s level %ld", 
	     octstr_get_cstr(logfile), logfilelevel);
    }
    octstr_destroy(logfile);

    if ((s = cfg_get(grp, octstr_imm("syslog-level"))) != NULL) {
    	long level;
	
	if (octstr_compare(s, octstr_imm("none")) == 0) {
	    log_set_syslog(NULL, 0);
	    debug("wap", 0, "syslog parameter is none");
	} else if (octstr_parse_long(&level, s, 0, 0) == -1) {
	    log_set_syslog("wapbox", level);
	    debug("wap", 0, "syslog parameter is %ld", level);
	}
	octstr_destroy(s);
    } else {
	log_set_syslog(NULL, 0);
	debug("wap", 0, "no syslog parameter");
    }
    
    /* configure URL mappings */
    map_url_max = -1;
    cfg_get_integer(&map_url_max, grp, octstr_imm("map-url-max"));
	
    if ((s = cfg_get(grp, octstr_imm("device-home"))) != NULL) {
	wsp_http_map_url_config_device_home(octstr_get_cstr(s));
	octstr_destroy(s);
    }
    if ((s = cfg_get(grp, octstr_imm("map-url"))) != NULL) {
	wsp_http_map_url_config(octstr_get_cstr(s));
	octstr_destroy(s);
    }
    debug("wap", 0, "map_url_max = %ld", map_url_max);

    for (i = 0; i <= map_url_max; i++) {
	Octstr *name;
	
	name = octstr_format("map-url-%d", i);
	if ((s = cfg_get(grp, name)) != NULL)
	    wsp_http_map_url_config(octstr_get_cstr(s));
	octstr_destroy(name);
    }
    wsp_http_map_url_config_info();	/* debugging aid */
    
    cfg_destroy(cfg);
}


static void signal_handler(int signum) 
{
    /* On some implementations (i.e. linuxthreads), signals are delivered
     * to all threads.  We only want to handle each signal once for the
     * entire box, and we let the gwthread wrapper take care of choosing
     * one. */
    if (!gwthread_shouldhandlesignal(signum))
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
	WAPAddrTuple *tuple;
        msg = msg_create(wdp_datagram);
	tuple = dgram->u.T_DUnitdata_Req.addr_tuple;
        msg->wdp_datagram.source_address =
            octstr_duplicate(tuple->local->address);
        msg->wdp_datagram.source_port =
            dgram->u.T_DUnitdata_Req.addr_tuple->local->port;
        msg->wdp_datagram.destination_address =
            octstr_duplicate(tuple->remote->address);
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
    Octstr *filename;
    
    gwlib_init();
    cf_index = get_and_set_debugs(argc, argv, NULL);
    
    if (argc > cf_index)
	filename = octstr_create(argv[cf_index]);
    else
	filename = octstr_create("kannel.conf");
    read_config(filename);
    octstr_destroy(filename);
    
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
    octstr_destroy(bearerbox_host);
    gwlib_shutdown();
    return 0;
}
