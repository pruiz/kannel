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

#include "gwlib.h"
#include "msg.h"
#include "wtp.h"
#include "bb.h"

#define TEST_HEARTBEAT_THREAD

static char *bearerbox_host = BB_DEFAULT_HOST;
static int bearerbox_port = BB_DEFAULT_WAPBOX_PORT;
static int heartbeat_freq = BB_DEFAULT_HEARTBEAT;
#ifdef TEST_HEARTBEAT_THREAD
static int test_heartbeat_thread = 0;
#endif
static char *logfile = NULL;
static int logfilelevel = 0;


static enum {
	initializing,
	running,
	aborting,
	aborting_with_prejudice,
} run_status = initializing;


static void read_config(char *filename) {
	Config *cfg;
	ConfigGroup *grp;
	char *s;

	cfg = config_create(filename);
	if (config_read(cfg) == -1)
		panic(0, "Couldn't read configuration from `%s'.", filename);
	config_dump(cfg);
	
	grp = config_first_group(cfg);
	while (grp != NULL) {
		if ((s = config_get(grp, "bearerbox-host")) != NULL)
			bearerbox_host = s;
		if ((s = config_get(grp, "bearerbox-port")) != NULL)
			bearerbox_port = atoi(s);
		if ((s = config_get(grp, "heartbeat-freq")) != NULL)
		        heartbeat_freq = atoi(s);
		if ((s = config_get(grp, "log-file")) != NULL)
		        logfile = s;
		if ((s = config_get(grp, "log-level")) != NULL)
		        logfilelevel = atoi(s);
#ifdef TEST_HEARTBEAT_THREAD
		if ((s = config_get(grp, "test-heartbeat-thread")) != NULL)
		        test_heartbeat_thread = atoi(s);
#endif
		if ((s = config_get(grp, "device-home")) != NULL)
			wsp_http_map_url_config_device_home(s);
		/* This would be really nice, if config_get would return
		 * all map-url lines in seperate calls; right now only
		 * the last map-url line in the configuration file survives.
		 */
		if ((s = config_get(grp, "map-url")) != NULL)
			wsp_http_map_url_config(s);
		grp = config_next_group(grp);
	}
	if (heartbeat_freq == -600)
	    panic(0, "Apparently someone is using SAMPLE configuration without "
		  "editing it first - well, hopefully he or she now reads it");

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

        debug("wap", 0, "WAPBOX: message received");
        msg_dump(msg);
	if (msg == NULL)
		return NULL;
	octstr_destroy(os);
	return msg;
}


static void msg_send(int s, Msg *msg) {
	Octstr *os;

	os = msg_pack(msg);
	if (os == NULL)
	   panic(0, "msg_pack failed");
	if (octstr_send(s, os) == -1)
	   error(0, "wapbox: octstr_send failed");
	octstr_destroy(os);
	if (msg->type != heartbeat) {
		debug("wap", 0, "WAPBOX: Sent message:");
		msg_dump(msg);
	} else {
		/* avoid overly large, growing memory leak
		 * As far as I can see msgs are not freed
		 * right now; heartbeat was static, now we
		 * need to free them.
		 */
#ifdef TEST_HEARTBEAT_THREAD
		if (test_heartbeat_thread)
			warning(0, "destroying heartbeat message");
#endif
		msg_destroy(msg);
	}
}


#define MAX_MSGS_IN_QUEUE 1024
static Msg *queue_tab[MAX_MSGS_IN_QUEUE];
static int queue_start = 0;
static int queue_len = 0;
static Mutex *queue_mutex;
static Mutex *queue_read_mutex;	/* remove_msg_from_queue() sleeps here */


void init_queue(void) {
	queue_mutex = mutex_create();
	queue_read_mutex = mutex_create();
	mutex_lock(queue_read_mutex);
}


void put_msg_in_queue(Msg *msg) {
	mutex_lock(queue_mutex);
	if (queue_len == MAX_MSGS_IN_QUEUE)
		error(0, "wapbox: message queue full, dropping message");
	else {
		queue_tab[(queue_start + queue_len) % MAX_MSGS_IN_QUEUE] = msg;
		if (0 == queue_len++) {
#ifdef TEST_HEARTBEAT_THREAD
			if (test_heartbeat_thread)
				warning(0, "first new msg, waking empty_queue_thread");
#endif
			mutex_unlock(queue_read_mutex);	/* wake reader */
		}
	}
	mutex_unlock(queue_mutex);
}


Msg *remove_msg_from_queue(void) {
	Msg *msg;
	
#ifdef TEST_HEARTBEAT_THREAD
	if (test_heartbeat_thread)
		warning(0, "remove_msg_from_queue sleeping");
#endif
	mutex_lock(queue_read_mutex);	/* sleeps if queue empty */
#ifdef TEST_HEARTBEAT_THREAD
	if (test_heartbeat_thread)
		warning(0, "remove_msg_from_queue awake");
#endif
	mutex_lock(queue_mutex);
	if (queue_len == 0)
		panic(0, "remove_msg_from_queue: no longer single consumer?");
	msg = queue_tab[queue_start];
	queue_start = (queue_start + 1) % MAX_MSGS_IN_QUEUE;
	if (--queue_len > 0)
		mutex_unlock(queue_read_mutex);
#ifdef TEST_HEARTBEAT_THREAD
	else
		if (test_heartbeat_thread)
			warning(0, "remove_msg_from_queue no more messages");
#endif
	mutex_unlock(queue_mutex);
	return msg;
}


static void *send_heartbeat_thread(void *arg) {
	while (run_status == running) {
		Msg *msg = msg_create(heartbeat);
		if (msg == NULL)
			panic(0, "cannot create heartbeat message");
		msg->heartbeat.load = 0;	/* XXX */
#ifdef TEST_HEARTBEAT_THREAD
		if (test_heartbeat_thread)
			warning(0, "send_heartbeat_thread new heartbeat");
#endif
		put_msg_in_queue(msg);
		sleep(heartbeat_freq);
	}
	return NULL;
}


static void *empty_queue_thread(void *arg) {
	Msg *msg;
	int socket;
	time_t stamp;
	
	socket = *(int *) arg;
	stamp = time(NULL);
	
	while (run_status == running) {
		msg = remove_msg_from_queue();
		if (msg == NULL)
			panic(0, "empty_queue_thread: msg==NULL");
		msg_send(socket, msg);
	}
	return NULL;
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
	WTPEvent *wtp_event = NULL;
        WTPMachine *wtp_machine = NULL;

	cf_index = get_and_set_debugs(argc, argv, NULL);
	
	if (argc > cf_index)
		read_config(argv[cf_index]);
	else
		read_config("kannel.wapconf");
		
	setup_signal_handlers();

	info(0, "------------------------------------------------------------");
	info(0, "WAP box version %s starting up.", VERSION);

	wtp_init();
        wtp_tid_cache_init();
	wsp_init();

	bbsocket = connect_to_bearer_box();
	init_queue();
	
	/* bof@bof.de 30.1.2000 - the other way round races. ugh. */

	run_status = running;

	(void) start_thread(1, send_heartbeat_thread, 0, 0);
	(void) start_thread(1, empty_queue_thread, &bbsocket, 0);
	
	while (run_status == running) {
		msg = msg_receive(bbsocket);
		if (msg == NULL)
			break;

                debug("wap", 0, "WAPBOX: message received");

		wtp_event = wtp_unpack_wdp_datagram(msg);
                debug("wap", 0, "WAPBOX: datagram unpacked");
                wtp_event_dump(wtp_event);
                if (wtp_event == NULL)
                   continue;
		wtp_machine = wtp_machine_find_or_create(msg, wtp_event);
                debug("wap", 0, "WAPBOX: machine created");
                wtp_machine_dump(wtp_machine);
                if (wtp_machine == NULL)
                   continue;
	        wtp_handle_event(wtp_machine, wtp_event);
	}
	
	info(0, "WAP box terminating.");
	return 0;
}
