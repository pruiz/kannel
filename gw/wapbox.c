/*
 * wapbox.h - main program for WAP box
 *
 * This module contains the main program for the WAP box of the WAP gateway.
 * See the architecture documentation for details.
 *
 * Lars Wirzenius <liw@wapit.com> for WapIT Ltd.
 */
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#ifdef HAVE_GETLOADAVG
#include <sys/loadavg.h>
#endif

#include "gwlib/gwlib.h"
#include "wapbox.h"
#include "msg.h"
#include "wtp.h"
#include "wtp_timer.h"
#include "bb.h"

static Config *cfg = NULL;
static char *bearerbox_host = BB_DEFAULT_HOST;
static int bearerbox_port = BB_DEFAULT_WAPBOX_PORT;
static int heartbeat_freq = BB_DEFAULT_HEARTBEAT;
static int timer_freq = WB_DEFAULT_TIMER_TICK;
static char *logfile = NULL;
static int logfilelevel = 0;


static enum {
	initializing,
	running,
	aborting,
	aborting_with_prejudice
} run_status = initializing;


/* NOTE: the following variable belongs to a hack, and will go away
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

static void read_config(char *filename) {
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
	if ((grp = config_find_first_group(cfg, "group", "core")) == NULL)
	    panic(0, "No 'core' group in configuration");

	if ((s = config_get(grp, "wapbox-port")) == NULL)
	    panic(0, "No 'wapbox-port' in core group");

	bearerbox_port = atoi(s);

	/*
	 * get the remaining values from the wapbox group
	 */
	if ((grp = config_find_first_group(cfg, "group", "wapbox")) == NULL)
	    panic(0, "No 'wapbox' group in configuration");

	if ((s = config_get(grp, "bearerbox-host")) != NULL)
	    bearerbox_host = s;
	if ((s = config_get(grp, "heartbeat-freq")) != NULL)
		heartbeat_freq = atoi(s);
	
	if ((s = config_get(grp, "timer-freq")) != NULL)
	    timer_freq = atoi(s);
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
	if (logfile != NULL) {
		open_logfile(logfile, logfilelevel);
	        info(0, "Starting to log to file %s level %d", logfile, logfilelevel);
	}
	wsp_http_map_url_config_info();	/* debugging aid */
}


static int connect_to_bearer_box(void) {
	int s;
	
	s = tcpip_connect_to_server(bearerbox_host, bearerbox_port);
	if (s == -1)
		panic(0, "Couldn't connect to bearer box %s:%d.",
			bearerbox_host, bearerbox_port);
	return s;
}


static Msg *msg_receive(int s) {
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
	return msg;
}


static void msg_send(int s, Msg *msg) {
	Octstr *os;
/*
 * Ifdefs define code for timer testing. This code will disappear
 */
#if 0
        int ret;
#endif
	os = msg_pack(msg);
	if (os == NULL)
	   panic(0, "msg_pack failed");
#if 0
        if (msg->type == wdp_datagram){ 
	   ret = octstr_get_char(msg->wdp_datagram.user_data, 0);
           ret = ret>>3&15;
           debug("wap", 0, "selecting dropped message %d", ret);
           if (ret != 2)
              if (octstr_send(s, os) == -1)
	         error(0, "wapbox: octstr_send failed");
	   octstr_destroy(os);
        } else {
#endif
        if (octstr_send(s, os) == -1)
	   error(0, "wapbox: octstr_send failed");
	octstr_destroy(os);
        /* Yeah, we now allways free the memory allocated to msg.*/
        msg_destroy(msg);
#if 0
        }
#endif
}



/*
 * This is the queue of messages that are being sent to the bearerbox.
 */
static List *queue = NULL;


void init_queue(void) {
        gw_assert(queue == NULL);
	queue = list_create();
}


static void destroy_queue(void) {
	while (list_len(queue) > 0)
		msg_destroy(list_extract_first(queue));
	list_destroy(queue);
}


void put_msg_in_queue(Msg *msg) {
	list_produce(queue, msg);
}


Msg *remove_msg_from_queue(void) {
	return list_consume(queue);
}


static void send_heartbeat_thread(void *arg) {
	list_add_producer(queue);
	while (run_status == running) {
	
		#ifdef HAVE_GETLOADAVG
		double loadavg[3]={0};
		#endif
	
		Msg *msg = msg_create(heartbeat);
		
		#ifdef HAVE_GETLOADAVG
		if(getloadavg(loadavg,3)==-1){
			info(0,"getloadavg failed!\n");
		}else{
			info(0,"1 min load average %f\n",
				loadavg[LOADAVG_1MIN]);
		}
		msg->heartbeat.load = loadavg[LOADAVG_1MIN];
		#else
		msg->heartbeat.load = 0;	/* XXX */
		#endif

		put_msg_in_queue(msg);
		sleep(heartbeat_freq);
	}
	list_remove_producer(queue);
}

#if 0
static void timer_thread(void *arg) {
	while (run_status == running) {
		wtp_timer_check();
		sleep(timer_freq);
	}
}
#endif

static void empty_queue_thread(void *arg) {
	Msg *msg;
	int socket;
	
	socket = *(int *) arg;
	while (run_status == running) {
		msg = remove_msg_from_queue();
		if (msg != NULL)
			msg_send(socket, msg);
	}
}


static void signal_handler(int signum) {
	/* Implement a simple timer for ignoring all but the first of each
	   set of signals. Sigint is sent to all threads, when given from
	   keyboard. This timer makes sure only the first thread to receive
	   it actually does anything. Otherwise the other ones will
	   be in aborting state when they receive the signal. */
	static time_t previous_sigint = 0;

	switch (signum) {
	case SIGINT:
		switch (run_status) {
		case aborting_with_prejudice:
			break;
		case aborting:
			if (time(NULL) - previous_sigint > 2) {
				error(0, "New SIGINT received, let's die harder");
				run_status = aborting_with_prejudice;
			} else {
				;
				/* Oh man, I can't f*cking believe this. 
				 * Another thread, another handler. How can
				 * the same signal happen to the same guy 
				 * twice? 
				 */
			}
			break;
		default:
			error(0, "SIGINT received, let's die.");
			time(&previous_sigint);
			run_status = aborting;
			break;
		}
		break;

	case SIGHUP:
		warning(0, "SIGHUP received, catching and re-opening logs");
		reopen_log_files();
		break;
	}
}


static void setup_signal_handlers(void) {
	struct sigaction act;
	
	act.sa_handler = signal_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);
	sigaction(SIGPIPE, &act, NULL);
}


int main(int argc, char **argv) {
	int bbsocket;
	int cf_index;
	Msg *msg;
	WAPEvent *wtp_event = NULL;

	gwlib_init();
	cf_index = get_and_set_debugs(argc, argv, NULL);
	
	if (argc > cf_index)
		read_config(argv[cf_index]);
	else
		read_config("kannel.conf");
		
	setup_signal_handlers();

	info(0, "------------------------------------------------------------");
	info(0, "WAP box version %s starting up.", VERSION);

	wtp_init();
        wtp_tid_cache_init();
        wtp_timer_init();
	wsp_init();

	bbsocket = connect_to_bearer_box();
	init_queue();
	
	/* bof@bof.de 30.1.2000 - the other way round races. ugh. */

	run_status = running;
	list_add_producer(queue);

	gwthread_create(send_heartbeat_thread, NULL);
	gwthread_create(empty_queue_thread, &bbsocket);
#if 0
	gwthread_create(timer_thread, 0);
#endif
	wap_appl_init();
	for (; run_status == running; msg_destroy(msg)) {
		msg = msg_receive(bbsocket);
		if (msg == NULL)
			break;
                if (msg->wdp_datagram.destination_port == CONNECTIONLESS_PORT) {
			error(0, "WAPBOX: connectionless mode PDU ignored");
			continue;
                }
		wtp_event = wtp_unpack_wdp_datagram(msg);
                if (wtp_event == NULL)
                   continue;
		wtp_dispatch_event(wtp_event);
	}
	info(0, "WAP box terminating.");

	run_status = aborting;
	list_remove_producer(queue);
	gwthread_join_all(send_heartbeat_thread);
	gwthread_join_all(empty_queue_thread);
	wap_appl_shutdown();
	wsp_shutdown();
	destroy_queue();
	wtp_shutdown();
	wtp_timer_shutdown();
	wtp_tid_cache_shutdown();
	config_destroy(cfg);
	gwlib_shutdown();
	return 0;
}
