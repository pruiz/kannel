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

#include "gwlib/gwlib.h"
#include "msg.h"
#include "bearerbox.h"

/* global variables; included to other modules as needed */

List *incoming_sms;
List *outgoing_sms;

List *incoming_wdp;
List *outgoing_wdp;

Counter *incoming_sms_counter;
Counter *outgoing_sms_counter;
Counter *incoming_wdp_counter;
Counter *outgoing_wdp_counter;



/* this is not a list of items; instead it is used as
 * indicator to note how many threads we have.
 * ALL flow threads must exit before we may safely change
 * bb_status from BB_SHUTDOWN to BB_DEAD
 *
 * XXX: prehaps we could also have items in this list, as
 *     descriptors of each thread?
 */
List *flow_threads;

/* and still more abuse; we use this list to put us into
 * 'suspend' state - if there are any producers (only core adds/removes them)
 * receiver/sender systems just sit, blocked in list_consume
 */
List *suspended;

/* this one is like 'suspended', but only for receiving UDP/SMSC
 * (suspended state puts producers for both lists)
 */
List *isolated;

volatile sig_atomic_t bb_status;

/* own global variables */

static Mutex *status_mutex;
static time_t start_time;



/* to avoid copied code */

static void set_shutdown_status(void)
{
    if (bb_status == BB_SUSPENDED)
	list_remove_producer(suspended);
    if (bb_status == BB_SUSPENDED || bb_status == BB_ISOLATED)
	list_remove_producer(isolated);
    bb_status = BB_SHUTDOWN;
}


/*-------------------------------------------------------
 * signals
 */

static void signal_handler(int signum)
{
    /* We get a SIGINT for each thread running; this timeout makes sure we
       handle it only once (unless there's a huge load and handling them
       takes longer than two seconds). */

    static time_t first_kill = -1;
    
    if (signum == SIGINT || signum == SIGTERM) {

	mutex_lock(status_mutex);
        if (bb_status != BB_SHUTDOWN && bb_status != BB_DEAD) {
	    set_shutdown_status();

	    /* shutdown smsc/udp is called by the http admin thread */
	    
            first_kill = time(NULL);
	    
            warning(0, "Killing signal received, shutting down...");
	    mutex_unlock(status_mutex);
        }
        else if (bb_status == BB_SHUTDOWN) {
	    /*
             * we have to wait for a while as one SIGINT from keyboard
             * causes several signals - one for each thread?
             */
            if (time(NULL) - first_kill > 2) {
                warning(0, "New killing signal received, killing neverthless...");
                bb_status = BB_DEAD;
		first_kill = time(NULL);
            }
	    mutex_unlock(status_mutex);
        }
        else if (bb_status == BB_DEAD) {
            if (time(NULL) - first_kill > 1)
		panic(0, "cannot die by its own will");
	}
	else
	    mutex_unlock(status_mutex);
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
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGPIPE, &act, NULL);
}




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


static void wdp_router(void *arg)
{
    Msg *msg;

    list_add_producer(flow_threads);
    
    while(bb_status != BB_DEAD) {

	if ((msg = list_consume(outgoing_wdp)) == NULL)
	    break;

	gw_assert(msg_type(msg) == wdp_datagram);
	
	// if (msg->list == sms)
	// smsc_addwdp(msg);
	// else

	udp_addwdp(msg);
    }
    udp_die();
    // smsc_endwdp();

    list_remove_producer(flow_threads);
}

static int start_wap(Config *config)
{
    static int started = 0;
    if (started) return 0;

    
    wapbox_start(config);

    debug("bb", 0, "starting WDP router");
    if (gwthread_create(wdp_router, NULL) == -1)
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

#ifndef KANNEL_NO_SMS    
    if (smsp && *smsp && grp == NULL) {
	error(0, "No 'smsbox' group in configuration, but smsbox-port set");
	return -1;
    }
    if (grp != NULL && config_find_next_group(grp, "group", "smsbox"))
	warning(0, "multiple 'smsbox' groups in configuration");
#endif
    
#ifndef KANNEL_NO_WAP	
    grp = config_find_first_group(config, "group", "wapbox");
    if (wapp && *wapp && grp == NULL) {
	error(0, "No 'wapbox' group in configuration, but wapbox-port set");
	return -1;
    }
    if (grp != NULL && config_find_next_group(grp, "group", "wapbox"))
	warning(0, "multiple 'wapbox' groups in configuration");
#endif
    
    return 0;
}

/*
 * check our own variables
 */

static int check_args(int i, int argc, char **argv) {
    if (strcmp(argv[i], "-S")==0 || strcmp(argv[i], "--suspended")==0)
	bb_status = BB_SUSPENDED;
    else if (strcmp(argv[i], "-I")==0 || strcmp(argv[i], "--isolated")==0)
	bb_status = BB_ISOLATED;
    else
	return -1;

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
    info(0, "----------------------------------------");
    info(0, "Kannel bearerbox II version %s starting", VERSION);

    if (check_config(config) == -1)
	panic(0, "Cannot start with corrupted configuration");

    /* if all seems to be OK by the first glimpse, real start-up */
    
    outgoing_sms = list_create();
    incoming_sms = list_create();
    outgoing_wdp = list_create();
    incoming_wdp = list_create();

    outgoing_sms_counter = counter_create();
    incoming_sms_counter = counter_create();
    outgoing_wdp_counter = counter_create();
    incoming_wdp_counter = counter_create();
    
    status_mutex = mutex_create();

    setup_signal_handlers();

    
    /* http-admin is REQUIRED */
    httpadmin_start(config);

#ifndef KANNEL_NO_SMS    
    if (config_find_first_group(config, "group", "smsc"))
	start_smsc(config);
#endif
    
    grp = config_find_first_group(config, "group", "core");
    
#ifndef KANNEL_NO_WAP
    val = config_get(grp, "wdp-interface-name");
    if (val && *val != '\0')
	start_udp(config);

    if (config_find_first_group(config, "group", "wapbox"))
	start_wap(config);
#endif
    
    return 0;
}


static void empty_msg_lists(void)
{
    Msg *msg;

#ifndef KANNEL_NO_WAP

    if (list_len(incoming_wdp) > 0 || list_len(outgoing_wdp) > 0)
	warning(0, "Remaining WDP: %ld incoming, %ld outgoing",
	      list_len(incoming_wdp), list_len(outgoing_wdp));

    info(0, "Total WDP messages: received %ld, sent %ld",
	 counter_value(incoming_wdp_counter),
	 counter_value(outgoing_wdp_counter));
#endif
    
    while((msg = list_extract_first(incoming_wdp))!=NULL)
	msg_destroy(msg);
    while((msg = list_extract_first(outgoing_wdp))!=NULL)
	msg_destroy(msg);

    list_destroy(incoming_wdp);
    list_destroy(outgoing_wdp);

    counter_destroy(incoming_wdp_counter);
    counter_destroy(outgoing_wdp_counter);
    
    
#ifndef KANNEL_NO_SMS

    /* XXX we should record these so that they are not forever lost...
     */
    if (list_len(incoming_sms) > 0 || list_len(outgoing_sms) > 0)
	debug("bb", 0, "Remaining SMS: %ld incoming, %ld outgoing",
	      list_len(incoming_sms), list_len(outgoing_sms));

    info(0, "Total SMS messages: received %ld, sent %ld",
	 counter_value(incoming_sms_counter),
	 counter_value(outgoing_sms_counter));

#endif

    while((msg = list_extract_first(incoming_sms))!=NULL)
	msg_destroy(msg);
    while((msg = list_extract_first(outgoing_sms))!=NULL)
	msg_destroy(msg);
    
    list_destroy(incoming_sms);
    list_destroy(outgoing_sms);
    
    counter_destroy(incoming_sms_counter);
    counter_destroy(outgoing_sms_counter);
}


int main(int argc, char **argv)
{
    int cf_index;
    Config *cfg;

    bb_status = BB_RUNNING;
    
    gwlib_init();
    start_time = time(NULL);

    suspended = list_create();
    isolated = list_create();
    list_add_producer(suspended);
    list_add_producer(isolated);

    cf_index = get_and_set_debugs(argc, argv, check_args);

    cfg = config_from_file(argv[cf_index], "kannel.conf");
    if (cfg == NULL)
        panic(0, "No configuration, aborting.");

    flow_threads = list_create();
    
    starter(cfg);


    sleep(1);	/* give time to threads to register themselves */

    info(0, "MAIN: Start-up done, entering mainloop");
    if (bb_status == BB_SUSPENDED)
	info(0, "Gateway is now SUSPENDED by startup arguments");
    else if (bb_status == BB_ISOLATED) {
	info(0, "Gateway is now ISOLATED by startup arguments");
	list_remove_producer(suspended);
    } else {
	list_remove_producer(suspended);	
	list_remove_producer(isolated);
    }

    /* wait until flow threads exit */

    while(list_consume(flow_threads)!=NULL)
	;

    info(0, "All flow threads have died, killing core");
    bb_status = BB_DEAD;

    smsc_die();
    
    gwthread_join_all();
    
    empty_msg_lists();
    
    list_destroy(flow_threads);
    list_destroy(suspended);
    list_destroy(isolated);
    mutex_destroy(status_mutex);

    config_destroy(cfg);
    gwlib_shutdown();

    return 0;
}



/*----------------------------------------------------------------
 * public functions used via HTTP adminstration interface/module
 */

int bb_shutdown(void)
{
    static int called = 0;
    
    mutex_lock(status_mutex);
    
    if (called) {
	mutex_unlock(status_mutex);
	return -1;
    }
    debug("bb", 0, "Shutting down Kannel...");

    called = 1;
    set_shutdown_status();
    mutex_unlock(status_mutex);

#ifndef KANNEL_NO_SMS
    debug("bb", 0, "shutting down smsc");
    smsc_shutdown();
#endif
#ifndef KANNEL_NO_WAP
    debug("bb", 0, "shutting down upd");
    udp_shutdown();
#endif
    
    return 0;
}

int bb_isolate(void)
{
    mutex_lock(status_mutex);
    if (bb_status != BB_RUNNING && bb_status != BB_SUSPENDED) {
	mutex_unlock(status_mutex);
	return -1;
    }
    if (bb_status == BB_RUNNING)
	list_add_producer(isolated);
    else
	list_remove_producer(suspended);

    bb_status = BB_ISOLATED;
    mutex_unlock(status_mutex);
    return 0;
}

int bb_suspend(void)
{
    mutex_lock(status_mutex);
    if (bb_status != BB_RUNNING && bb_status != BB_ISOLATED) {
	mutex_unlock(status_mutex);
	return -1;
    }
    if (bb_status != BB_ISOLATED)
	list_add_producer(isolated);

    bb_status = BB_SUSPENDED;
    list_add_producer(suspended);
    mutex_unlock(status_mutex);
    return 0;
}

int bb_resume(void)
{
    mutex_lock(status_mutex);
    if (bb_status != BB_SUSPENDED && bb_status != BB_ISOLATED) {
	mutex_unlock(status_mutex);
	return -1;
    }
    if (bb_status == BB_SUSPENDED)
	list_remove_producer(suspended);
	
    bb_status = BB_RUNNING;
    list_remove_producer(isolated);
    mutex_unlock(status_mutex);
    return 0;
}

int bb_restart(void)
{
    return -1;
}



#define append_status(r, s, f) { s = f(); octstr_append(r, s); \
                                 octstr_destroy(s); }


Octstr *bb_print_status(void)
{
    char *s;
    char buf[512];
    Octstr *ret, *str;
    time_t t;

    t = time(NULL) - start_time;
    
    switch(bb_status) {
    case BB_RUNNING:
	s = "running";
	break;
    case BB_ISOLATED:
	s = "isolated";
	break;
    case BB_SUSPENDED:
	s = "suspended";
	break;
    default:
	s = "going down";
    }
    sprintf(buf, "Kannel version %s %s (up %ldd %ldh %ldm %lds), %d threads<br><br>"
	    "Received %ld SMS and %ld WDP messages, Sent %ld SMS and %ld WDP messages<br><br>",
	    VERSION, s, t/3600/24, t/3600%24, t/60%60, t%60,
	    list_producer_count(flow_threads),
	    counter_value(incoming_sms_counter),
	    counter_value(incoming_wdp_counter),
	    counter_value(outgoing_sms_counter),
	    counter_value(outgoing_wdp_counter));

    ret = octstr_create(buf);

    append_status(ret, str, boxc_status);
    append_status(ret, str, smsc_status);
    
    return ret;
}

