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
#include <syslog.h>
#ifdef HAVE_GETLOADAVG
#include <sys/loadavg.h>
#endif

#include "gwlib/gwlib.h"
#include "wapbox.h"
#include "msg.h"
#include "wtp.h"
#include "wsp.h"
#include "wtp_timer.h"
#include "bb.h"
#include "wsp-strings.h"

static Config *cfg = NULL;
static char *bearerbox_host = BB_DEFAULT_HOST;
static int bearerbox_port = BB_DEFAULT_WAPBOX_PORT;
static int heartbeat_freq = BB_DEFAULT_HEARTBEAT;
static int timer_freq = WB_DEFAULT_TIMER_TICK;
static char *logfile = NULL;
static int logfilelevel = 0;
extern char dosyslog;
extern int sysloglevel;


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
	/* Get syslog parameters */
	if ((s = config_get(grp, "syslog-level")) != NULL){
	    if(!strcmp(s,"none")){
		dosyslog = 0;
		debug("bbox",0,"syslog parameter is none");
	    }else{
		openlog("wapbox",LOG_PID,LOG_DAEMON);
		dosyslog = 1;
		sysloglevel = atoi(s); 
		debug("bbox",0,"syslog parameter is %d",sysloglevel);
	    }

	}else{
	    dosyslog = 0;
	    debug("bbox",0,"no syslog parameter");
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
#if 0
	debug("wap", 0, "Received message from bearer box:");
	msg_dump(msg, 0);
#endif
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

#if 0
	if (msg->type != heartbeat) {
		debug("wap", 0, "Sending message to bearer box:");
		msg_dump(msg, 0);
	}
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

	/* It would be more proper to use SIGUSR1 for this, but on some
	 * platforms that's reserved by the pthread support. */
	case SIGQUIT:
		warning(0, "SIGQUIT received, reporting memory usage.");
		if (gwthread_self() == 0)
			gw_check_leaks();
		break;
	}

}


static void setup_signal_handlers(void) {
	struct sigaction act;
	
	act.sa_handler = signal_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGQUIT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);
	sigaction(SIGPIPE, &act, NULL);
}


WAPAddr *wap_addr_create(Octstr *address, long port) {
	WAPAddr *addr;
	
	addr = gw_malloc(sizeof(*addr));
	addr->address = octstr_duplicate(address);
	addr->port = port;
	return addr;
}


void wap_addr_destroy(WAPAddr *addr) {
	if (addr != NULL) {
		octstr_destroy(addr->address);
		gw_free(addr);
	}
}


int wap_addr_same(WAPAddr *a, WAPAddr *b) {
	return a->port == b->port && 
		octstr_compare(a->address, b->address) == 0;
}


WAPAddrTuple *wap_addr_tuple_create(Octstr *cli_addr, long cli_port,
Octstr *srv_addr, long srv_port) {
	WAPAddrTuple *tuple;
	
	tuple = gw_malloc(sizeof(*tuple));
	tuple->client = wap_addr_create(cli_addr, cli_port);
	tuple->server = wap_addr_create(srv_addr, srv_port);
	return tuple;
}


void wap_addr_tuple_destroy(WAPAddrTuple *tuple) {
	if (tuple != NULL) {
		wap_addr_destroy(tuple->client);
		wap_addr_destroy(tuple->server);
		gw_free(tuple);
	}
}


int wap_addr_tuple_same(WAPAddrTuple *a, WAPAddrTuple *b) {
	return wap_addr_same(a->client, b->client) &&
		wap_addr_same(a->server, b->server);
}


WAPAddrTuple *wap_addr_tuple_duplicate(WAPAddrTuple *tuple) {
	if (tuple == NULL)
		return NULL;

	return wap_addr_tuple_create(tuple->client->address,
				     tuple->client->port,
				     tuple->server->address,
				     tuple->server->port);
}


void wap_addr_tuple_dump(WAPAddrTuple *tuple) {
	debug("wap", 0, "WAPAddrTuple %p = <%s:%ld> - <%s:%ld>", 
		(void *) tuple,
		octstr_get_cstr(tuple->client->address),
		tuple->client->port,
		octstr_get_cstr(tuple->server->address),
		tuple->server->port);
}


int main(int argc, char **argv) {
	int bbsocket;
	int cf_index;
	Msg *msg;
	WAPEvent *event = NULL;
        List *events = NULL;

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
	wsp_strings_init();
	wsp_session_init();
	wsp_unit_init();

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
			event = wsp_unit_unpack_wdp_datagram(msg);
			if (event != NULL)
				wsp_unit_dispatch_event(event);
                } else {
                  events = wtp_unpack_wdp_datagram(msg);
		  while (list_len(events) > 0){
			event = list_extract_first(events);;
	                if (event != NULL)
				wtp_dispatch_event(event);
                  }
                  list_destroy(events);
		}
	}
	info(0, "WAP box terminating.");

	run_status = aborting;
	list_remove_producer(queue);
	gwthread_join_every(send_heartbeat_thread);
	gwthread_join_every(empty_queue_thread);
	wap_appl_shutdown();
	wsp_unit_shutdown();
	wsp_session_shutdown();
	destroy_queue();
	wtp_shutdown();
	wtp_timer_shutdown();
	wtp_tid_cache_shutdown();
	wsp_strings_shutdown();
	config_destroy(cfg);
	gwlib_shutdown();
	return 0;
}
