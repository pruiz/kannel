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

#define BUSYLOOP_MILLISECONDS 10000

static char *bearerbox_host = BB_DEFAULT_HOST;
static int bearerbox_port = BB_DEFAULT_WAPBOX_PORT;
static int heartbeat_freq = BB_DEFAULT_HEARTBEAT;
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
#if 0
	config_dump(cfg);
#endif
	
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
		grp = config_next_group(grp);
	}
	if (logfile != NULL) {
		open_logfile(logfile, logfilelevel);
	        info(0, "Starting to log to file %s level %d", logfile, logfilelevel);
	}
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

	os = msg_pack(msg);
	if (os == NULL)
		panic(0, "msg_pack failed");
	if (octstr_send(s, os) == -1)
		error(0, "wapbox: octstr_send failed");
	octstr_destroy(os);
}


#define MAX_MSGS_IN_QUEUE 1024
static Msg *queue_tab[MAX_MSGS_IN_QUEUE];
static int queue_start = 0;
static int queue_len = 0;
static Mutex *queue_mutex;


void init_queue(void) {
	queue_mutex = mutex_create();
}


void put_msg_in_queue(Msg *msg) {
	mutex_lock(queue_mutex);
	if (queue_len == MAX_MSGS_IN_QUEUE)
		error(0, "wapbox: message queue full, dropping message");
	else {
		queue_tab[(queue_start + queue_len) % MAX_MSGS_IN_QUEUE] = msg;
		++queue_len;
	}
	mutex_unlock(queue_mutex);
}


Msg *remove_msg_from_queue(void) {
	Msg *msg;
	
	mutex_lock(queue_mutex);
	if (queue_len == 0)
		msg = NULL;
	else {
		msg = queue_tab[queue_start];
		queue_start = (queue_start + 1) % MAX_MSGS_IN_QUEUE;
		--queue_len;
#if 0
		debug(0, "wapbox: removed msg %p in queue", (void *) msg);
#endif
	}
	mutex_unlock(queue_mutex);
	return msg;
}


static int send_heartbeat(int socket, int load)
{
    static Msg *msg = NULL;
    
    if (msg == NULL)
        if ((msg = msg_create(heartbeat)) == NULL)
            return -1;

    msg->heartbeat.load = load;
    msg_send(socket, msg);

    return 0;
}


static void *empty_queue_thread(void *arg) {
	Msg *msg;
	int socket;
	time_t stamp;
	
	socket = *(int *) arg;
	stamp = time(NULL);
	
	while (run_status == running) {
		msg = remove_msg_from_queue();
		if (msg != NULL)
			msg_send(socket, msg);
		else {
			if (time(NULL) - stamp > heartbeat_freq) {
				/* send heartbeat */
				send_heartbeat(socket, 0);
				stamp = time(NULL);
			}
			usleep(BUSYLOOP_MILLISECONDS);
		}
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
		read_config("wapbox.wapconf");
		
	setup_signal_handlers();

	info(0, "------------------------------------------------------------");
	info(0, "WAP box starting up.");

	bbsocket = connect_to_bearer_box();
	init_queue();
	
	(void) start_thread(1, empty_queue_thread, &bbsocket, 0);
	
	run_status = running;
	while (run_status == running) {
		msg = msg_receive(bbsocket);
		if (msg == NULL)
			break;
#ifdef debug
                debug(0, "WAPBOX: message received");
#endif
		wtp_event = wtp_unpack_wdp_datagram(msg);
#ifdef debug
                debug(0, "WAPBOX: datagram unpacked");
#endif
                if (wtp_event == NULL)
                   continue;
		wtp_machine = wtp_machine_find_or_create(msg, wtp_event);
                if (wtp_machine == NULL)
                   continue;
	        wtp_handle_event(wtp_machine, wtp_event);
	}
	
	info(0, "WAP box terminating.");
	return 0;
}





