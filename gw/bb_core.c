/*
 * bearerbox.c
 * 
 * this is the core module of the bearerbox. It starts everything and
 * listens to HTTP requests and traps signals.
 * All started modules are responsible for the rest.
 *
 * Kalle Marjola <rpr@wapit.com> 2000 for project Kannel
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "new_bb.h"

/* global variables; included to other modules as needed */

List *incoming_sms;
List *outgoing_sms;

List *incoming_wdp;
List *outgoing_wdp;

volatile sig_atomic_t bb_status;

/* own global variables */

static time_t start_time;

/*--------------------------------------------------------
 * functions to start/init sub-parts of the bearerbox
 *
 * these functions are NOT thread safe but they have no need to be,
 * as there is only one core bearerbox thread
 */



static int start_smsc(Config *config)
{
    static int started = 0;
    if (started) return 0;

    smsc_start(config);

    smsbox_start(config);

    started = 1;
    return 0;
}


static void *wdp_router(void *arg)
{
    Msg *msg;

    while(bb_status != bb_dead) {

	if ((msg = list_consume(outgoing_wdp)) == NULL)
	    break;

	assert(msg_type(msg) == wdp_datagram);
	
	// if (msg->list == sms)
	// smsc_addwdp(msg);
	// else

	udp_addwdp(msg);
    }
    udp_die();
    // smsc_endwdp();

    return NULL;
}

static int start_wap(Config *config)
{
    static int started = 0;
    if (started) return 0;

    
    wapbox_start(config);

    debug("bb", 0, "starting WDP router");
    if ((int)(start_thread(0, wdp_router, NULL, 0)) == -1)
	panic(0, "Failed to start a new thread for WDP routing");

    started = 1;
    return 0;
}


static int start_udp(Config *config)
{
    static int started = 0;
    if (started) return 0;

    udp_start(config);

    start_wap(config);
    started = 1;
    return 0;
}


/*
 * check that there is basic thingies in configuration
 */
static int check_config(Config *config)
{
    ConfigGroup *grp;
    char *smsp, *wapp;

    if ((grp = config_find_first_group(config, "group", "core")) == NULL) {
	error(0, "No 'core' group in configuration");
	return -1;
    }
    smsp = config_get(grp, "smsbox-port");
    wapp = config_get(grp, "wapbox-port");
    
    if (config_find_next_group(grp, "group", "core"))
	warning(0, "multiple 'core' groups in configuration");

    grp = config_find_first_group(config, "group", "smsbox");
    if (smsp && *smsp && grp == NULL) {
	error(0, "No 'smsbox' group in configuration, but smsbox-port set");
	return -1;
    }
    if (grp != NULL && config_find_next_group(grp, "group", "smsbox"))
	warning(0, "multiple 'smsbox' groups in configuration");
	
    grp = config_find_first_group(config, "group", "wapbox");
    if (wapp && *wapp && grp == NULL) {
	error(0, "No 'wapbox' group in configuration, but wapbox-port set");
	return -1;
    }
    if (grp != NULL && config_find_next_group(grp, "group", "wapbox"))
	warning(0, "multiple 'wapbox' groups in configuration");

    return 0;
}


static int starter(Config *config)
{
    ConfigGroup *grp;
    char *val, *log;
    
	
    grp = config_find_first_group(config, "group", "core");

    if ((log = config_get(grp, "log-file")) != NULL) {
	val = config_get(grp, "log-level");
	open_logfile(log, val ? atoi(val) : 0);
    }
    if (check_config(config) == -1)
	panic(0, "Cannot start with corrupted configuration");

    outgoing_sms = list_create();
    incoming_sms = list_create();
    outgoing_wdp = list_create();
    incoming_wdp = list_create();
    
    if (config_find_first_group(config, "group", "smsc"))
	start_smsc(config);

    grp = config_find_first_group(config, "group", "core");
    
    val = config_get(grp, "wdp-interface-name");
    if (val && *val)
	start_udp(config);

    if (config_find_first_group(config, "group", "wapbox"))
	start_wap(config);

    return 0;
}


int main(int argc, char **argv)
{
    int cf_index;
    Config *cfg;
        
    gwlib_init();
    start_time = time(NULL);

    cf_index = get_and_set_debugs(argc, argv, NULL);

    // setup_signal_handlers();
    cfg = config_from_file(argv[cf_index], "new_kannel.conf");
    if (cfg == NULL)
        panic(0, "No configuration, aborting.");

    starter(cfg);
    info(0, "----------------------------------------");
    info(0, "Bearerbox version %s starting", VERSION);

    debug("bb", 0, "Start-up done, entering mainloop");
    // main_program();

    while(1) sleep(60);
    
    config_destroy(cfg);
    gw_check_leaks();
    gwlib_shutdown();

    return 0;
}

