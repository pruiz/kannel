/*
 * SMS BOX
 *
 * (WAP/SMS) Gateway
 *
 * Kalle Marjola 1999 for Wapit ltd.
 *
 */

/*
 * this is a SMS Service BOX
 *
 * it's main function is to receive SMS Messages from
 * (gateway) bearerbox and then fulfill requests in those
 * messages
 *
 * It may also send SMS Messages on its own, sending them
 * to bearerbox and that way into SMS Centers
 *
 * 
 * FUNCTION:
 *
 * 1. main loop opens a TCP/IP socket into the bearerbox, doing
 *    necessary handshake
 *
 * 2. for each SMS Message received, a new thread is created to
 *    handle the request
 *
 * 3. replies to requests and HTTP-initiated messages are sent
 *    (back) to the bearerbox. A global mutex is used for locking
 *    purposes
 *
 * THREAD FUNCTION:
 *
 * this program can also be used as a separate thread in bearerbox
 * When used this way, request thread is directly created by the
 * main program in bearerbox and repolies directly added to the
 * bearerbox reply queue. TODO: This functionality is added later.
 *
 * CONFIGURATION:
 *
 * - Information required for connecting the bearerbox is stored into
 *   a seperate configuration file.
 * - Service handling information is received from the bearerbox during
 *   handshake procedure (currently: from same configuration as rest)
 *
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "gwlib/gwlib.h"

#include "cgi.h"
#include "msg.h"
#include "bb.h"

#include "smsbox_req.h"


/* global variables */

static Config 	*cfg;
static int 	bb_port;
static int	sendsms_port = 0;
static char 	*bb_host;
static char	*pid_file;
static int	sms_len = 160;
static char	*global_sender;
static int	heartbeat_freq;

static int 	socket_fd;
static int 	http_fd;
static char 	*http_allow_ip = NULL;
static char 	*http_deny_ip = NULL;

/* thread handling */

static Mutex	 	*socket_mutex;
static sig_atomic_t 	http_accept_pending = 0;
static sig_atomic_t 	abort_program = 0;


/*
 * function to do the actual sending; called from smsbox_req via
 * pointer we give during initialization
 *
 * MUST DO: free (or otherwise get rid of) pmsg, and
 * return 0 if OK, -1 if failed
 */
static int socket_sender(Msg *pmsg)
{
    Octstr *pack;

    pack = msg_pack(pmsg);
    if (pack == NULL)
	goto error;

    mutex_lock(socket_mutex);
    if (octstr_send(socket_fd, pack) < 0) {
	mutex_unlock(socket_mutex);
	goto error;
    }
    mutex_unlock(socket_mutex);

#if 0
    debug("sms", 0, "write <%.*s> [%ld]",
	  (int) octstr_len(pmsg->smart_sms.msgdata),
	  octstr_get_cstr(pmsg->smart_sms.msgdata),
	  octstr_len(pmsg->smart_sms.udhdata));
#endif

    octstr_destroy(pack);
    msg_destroy(pmsg);

    return 0;

error:
    msg_destroy(pmsg);
    octstr_destroy(pack);
    return -1;
}

/*
 * start a new thread for each request
 */
static void new_request(Octstr *pack)
{
    Msg *msg;

    gw_assert(pack != NULL);
    msg = msg_unpack(pack);
    if (msg == NULL)
	error(0, "Failed to unpack data!");
    else if (msg_type(msg) != smart_sms)
	warning(0, "Received other message than smart_sms, ignoring!");
    else
	(void)start_thread(1, smsbox_req_thread, msg, 0);
}



/*-----------------------------------------------------------
 * HTTP ADMINSTRATION
 */


static void *http_request_thread(void *arg)
{
    int client, ret;
    char *path = NULL, *args = NULL, *client_ip = NULL;
    char *answer;
    CGIArg *arglist;
    
    client = httpserver_get_request(http_fd, &client_ip, &path, &args);
    http_accept_pending = 0;
    if (client == -1) {
	error(0, "Failed to get request from client, killing thread");
	return NULL;
    }
    ret = 0;
    if (http_allow_ip != NULL)
	ret = check_ip(http_allow_ip, client_ip, NULL);
    if (ret < 1 && http_deny_ip != NULL)
	if (check_ip(http_deny_ip, client_ip, NULL) == 1) {
	    warning(0, "Non-allowed connect tried from <%s>, ignored",
		    client_ip);
	    goto done;
	}

    /* print client information */

    info(0, "smsbox: Get HTTP request < %s > from < %s >", path, client_ip);
    
    if (strcmp(path, "/cgi-bin/sendsms") == 0) {

	arglist = cgiarg_decode_to_list(args);
	answer = smsbox_req_sendsms(arglist);

	cgiarg_destroy_list(arglist);
    } else
	answer = "unknown request";
    info(0, "%s", answer);

    if (httpserver_answer(client, answer) == -1)
	error(0, "Error responding to client. Too bad.");

done:    
    /* answer closes the socket */
    gw_free(path);
    gw_free(args);
    gw_free(client_ip);
    return NULL;
}


static void http_start_thread(void)
{
    (void)start_thread(1, http_request_thread, NULL, 0);
}



/*------------------------------------------------------------*/


static void write_pid_file(void) {
    FILE *f;
        
    if (pid_file != NULL) {
	f = fopen(pid_file, "w");
	fprintf(f, "%d\n", (int)getpid());
	fclose(f);
    }
}


static void signal_handler(int signum) {
    if (signum == SIGINT) {
	if (abort_program == 0) {
	    error(0, "SIGINT received, aborting program...");
	    abort_program = 1;
	}
    } else if (signum == SIGHUP) {
        warning(0, "SIGHUP received, catching and re-opening logs");
        reopen_log_files();
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



static void init_smsbox(Config *cfg)
{
    ConfigGroup *grp;
    char *logfile = NULL;
    char *p;
    int lvl = 0;

    bb_port = BB_DEFAULT_SMSBOX_PORT;
    bb_host = BB_DEFAULT_HOST;
    heartbeat_freq = BB_DEFAULT_HEARTBEAT;

    grp = config_first_group(cfg);
    while(grp != NULL) {
	if ((p = config_get(grp, "bearerbox-port")) != NULL)
	    bb_port = atoi(p);
	if ((p = config_get(grp, "bearerbox-host")) != NULL)
	    bb_host = p;
	if ((p = config_get(grp, "sendsms-port")) != NULL)
	    sendsms_port = atoi(p);
	if ((p = config_get(grp, "sms-length")) != NULL)
	    sms_len = atoi(p);
        if ((p = config_get(grp, "http-allowed-hosts")) != NULL)
            http_allow_ip = p;
        if ((p = config_get(grp, "http-denied-hosts")) != NULL)
            http_deny_ip = p;
	if ((p = config_get(grp, "heartbeat-freq")) != NULL)
	    heartbeat_freq = atoi(p);
	if ((p = config_get(grp, "pid-file")) != NULL)
	    pid_file = p;
	if ((p = config_get(grp, "global-sender")) != NULL)
	    global_sender = p;
	if ((p = config_get(grp, "log-file")) != NULL)
	    logfile = p;
	if ((p = config_get(grp, "log-level")) != NULL)
	    lvl = atoi(p);
	grp = config_next_group(grp);
    }

    if (heartbeat_freq == -600)
	panic(0, "Apparently someone is using SAMPLE configuration without "
		"editing it first - well, hopefully he or she now reads it");

    if (http_allow_ip != NULL && http_deny_ip == NULL)
	warning(0, "Allow IP-string set without any IPs denied!");

    if (global_sender != NULL)
	info(0, "Service global sender set as '%s'", global_sender);
    
    if (logfile != NULL) {
	info(0, "Starting to log to file %s level %d", logfile, lvl);
	open_logfile(logfile, lvl);
    }
    if (sendsms_port > 0) {
	http_fd = httpserver_setup(sendsms_port);
	if (http_fd < 0)
	    error(0, "Failed to open HTTP socket, ignoring it");
	else
	    info(0, "Set up send sms service at port %d", sendsms_port);
    } else
	http_fd = -1;
    
    return;
}

/*
 * send the heartbeat packet
 */
static int send_heartbeat(void)
{
#if 0
    /* XXX this is not thread safe, if two threads happen to call 
       send_hearbeat at the same time. Should never happen, though, and 
       anyway only causes a minor memory leak. --liw */
    static Msg *msg = NULL;
    Octstr *pack;
    
    if (msg == NULL)
	if ((msg = msg_create(heartbeat)) == NULL)
	    return -1;

    msg->heartbeat.load = smsbox_req_count();
    if ((pack = msg_pack(msg)) == NULL)
	return -1;

#if 0
    debug("sms", 0, "sending heartbeat load %d", smsbox_req_count()); 
#endif
    if (octstr_send(socket_fd, pack))
	return -1;
    octstr_destroy(pack);
    return 0;
#else
    Msg *msg = NULL;
    Octstr *pack;
    int ret;
    
    msg = msg_create(heartbeat);
    msg->heartbeat.load = smsbox_req_count();
#if 0
    debug("sms", 0, "sending heartbeat load %ld", msg->heartbeat.load); 
#endif
    pack = msg_pack(msg);
    ret = octstr_send(socket_fd, pack);
    octstr_destroy(pack);
    msg_destroy(msg);
    return ret;
#endif
}


static void main_loop(void)
{
    time_t start, t;
    int ret, secs;
    int total = 0;
    fd_set rf;
    struct timeval to;

    if (http_fd < 0)
	http_accept_pending = -1;
    else
	http_accept_pending = 0;
    
    start = t = time(NULL);
    mutex_lock(socket_mutex);
    while(!abort_program) {

	if (time(NULL)-t > heartbeat_freq) {
	    if (send_heartbeat() == -1)
		goto error;
	    t = time(NULL);
	}
	FD_ZERO(&rf);
	FD_SET(socket_fd, &rf);
	if (http_accept_pending == 0)
	    FD_SET(http_fd, &rf);
	to.tv_sec = 0;
	to.tv_usec = 0;

	ret = select(FD_SETSIZE, &rf, NULL, NULL, &to);

	if (ret < 0) {
	    if(errno==EINTR) continue;
	    if(errno==EAGAIN) continue;
	    error(errno, "Select failed");
	    goto error;
	} if (ret > 0 && http_accept_pending == 0 && FD_ISSET(http_fd, &rf)) {

	    http_accept_pending = 1;
	    http_start_thread();
	    continue;
	}
	else if (ret > 0 && FD_ISSET(socket_fd, &rf)) {

	    Octstr *pack;

	    ret = octstr_recv(socket_fd, &pack);
	    if (ret == 0) {
		info(0, "Connection closed by the Bearerbox");
		break;
	    }
	    else if (ret == -1) {
		info(0, "Connection to Bearerbox failed, reconnecting");
	    reconnect:
		socket_fd = tcpip_connect_to_server(bb_host, bb_port);
		if (socket_fd > -1)
		    continue;
		sleep(10);
		goto reconnect;
	    }
	    mutex_unlock(socket_mutex);

	    if (total == 0)
		start = time(NULL);
	    total++;
	    new_request(pack);
	    octstr_destroy(pack);
	    
	    mutex_lock(socket_mutex);
	    continue;
	}
	mutex_unlock(socket_mutex);
	    
	usleep(1000);

	mutex_lock(socket_mutex);
    }
    secs = time(NULL) - start;
    info(0, "Received (and handled?) %d requests in %d seconds (%.2f per second)",
	 total, secs, (float)total/secs);
    return;

error:
    panic(0, "Mutex error, exiting");
}



int main(int argc, char **argv)
{
    int cf_index;
    URLTranslationList *translations;

    gw_init_mem();
    cf_index = get_and_set_debugs(argc, argv, NULL);


    socket_mutex = mutex_create();

    setup_signal_handlers();
    cfg = config_from_file(argv[cf_index], "kannel.smsconf");
    if (cfg == NULL)
	panic(0, "No configuration, aborting.");

    init_smsbox(cfg);

    debug("sms", 0, "----------------------------------------------");
    debug("sms", 0, "Gateway SMS BOX version %s starting", VERSION);
    write_pid_file();

    translations = urltrans_create();
    if (translations == NULL)
	panic(errno, "urltrans_create failed");
    if (urltrans_add_cfg(translations, cfg) == -1)
	panic(errno, "urltrans_add_cfg failed");

    /*
     * initialize smsbox-request module
     */
    smsbox_req_init(translations, sms_len, global_sender, socket_sender);
    
    while(!abort_program) {
	socket_fd = tcpip_connect_to_server(bb_host, bb_port);
	if (socket_fd > -1)
	    break;
	sleep(10);
    }
    info(0, "Connected to Bearer Box at %s port %d", bb_host, bb_port);

    main_loop();

    mutex_destroy(socket_mutex);
    urltrans_destroy(translations);
    config_destroy(cfg);
    gw_check_leaks();
    return 0;
}
