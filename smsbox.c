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
 * (gateway) Bearer Box and then fulfill requests in those
 * messages
 *
 * It may also send SMS Messages on its own, sending them
 * to Bearer box and that way into SMS Centers
 *
 * 
 * FUNCTION:
 *
 * 1. main loop opens a TCP/IP socket into the bearer box, doing
 *    necessary handshake
 *
 * 2. for each SMS Message received, an ACK is sent back to bearer
 *    box and then a new thread is created to handle the request
 *
 * 3. replies to requests and HTTP-initiated messages are added
 *    to reply queue, which is then emptied by the main loop onto
 *    the bearer box
 *
 * THREAD FUNCTION:
 *
 * this program can also be used as a separate thread in Bearer Box
 * When used this way, different main porgram is simply used and messages
 * are transfered via queue
 *
 * CONFIGURATION:
 *
 * - Information required for connecting the bearer box is stored into
 *   a seperate configuration file.
 * - Service handling information is received from the bearer box during
 *   handshake procedure
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

#include "wapitlib.h"
#include "config.h"
#include "urltrans.h"
#include "http.h"
#include "html.h"
#include "cgi.h"
#include "sms_msg.h"
#include "bb.h"


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

/* thread handling */

static pthread_mutex_t 	socket_mutex;
static sig_atomic_t 	http_accept_pending = 0;
static sig_atomic_t 	abort_program = 0;
static sig_atomic_t	req_threads = 0;

/* Current list of URL translations. */

static URLTranslationList *translations = NULL;



/* Perform the service requested by the user: translate the request into
 * a pattern, if it is an URL, fetch it, and return a string, which must
 * be free'ed by the caller
 */
static char *obey_request(URLTranslation *trans, SMSMessage *sms)
{
    char *pattern;
    char *data, *tmpdata;
    size_t size;
    int type;
    char replytext[1024*10+1];       /* ! absolute limit ! */


    pattern = urltrans_get_pattern(trans, sms);
    if (pattern == NULL) {
	error(0, "Oops, urltrans_get_pattern failed.");
	return NULL;
    }
    if (urltrans_type(trans) == TRANSTYPE_TEXT) {
	debug(0, "formatted text answer: <%s>", pattern);
	return pattern;
    }
    if (urltrans_type(trans) == TRANSTYPE_FILE)
	return strdup("File reading not yet implemented");

    /* URL */

    debug(0, "formatted url: <%s>", pattern);

    if (http_get(pattern, &type, &data, &size) == -1) {
	free(pattern);
	goto error;
    }
    free(pattern);		/* no longer needed */
	
    /* Make sure the data is NUL terminated. */
    tmpdata = realloc(data, size + 1);
    if (tmpdata == NULL) {
	error(errno, "Out of memory allocating HTTP response.");
	free(data);
	goto error;
    }
    data = tmpdata;
    data[size] = '\0';

/*
 * http_get is buggy at the moment, and doesn't set type correctly.
 * work around this. XXX fix this
 */
    type = HTTP_TYPE_HTML;

    switch (type) {
    case HTTP_TYPE_HTML:
	if (urltrans_prefix(trans) != NULL &&
	    urltrans_suffix(trans) != NULL) {
	    
	    tmpdata = html_strip_prefix_and_suffix(data,
		       urltrans_prefix(trans), urltrans_suffix(trans));
	    free(data);	
	    data = tmpdata;
	}
	html_to_sms(replytext, sizeof(replytext), data);
	break;
    case HTTP_TYPE_TEXT:
	strncpy(replytext, data, sizeof(replytext) - 1);
	break;
    default:
	strcpy(replytext,
	       "Result could not be represented as an SMS message.");
	break;
    }
    free(data);

    if (strlen(replytext)==0)
	return strdup("");
    return strdup(replytext);

error:
    return NULL;
}


/*
 * sends the buf, with msg-info - does NO splitting etc. just the sending
 * Message is truncated by sms mag length
 *
 * return -1 on failure, 0 if Ok.
 */
static int do_sending(SMSMessage *msg, char *str)
{
    char buf[1024];
    int ret;
    
    /* note the switching of sender and receiver */
    
    sprintf(buf, "%d %s %s %.*s\n", msg->id, msg->receiver, msg->sender,
	    sms_len, str);

    ret = pthread_mutex_lock(&socket_mutex);
    if (ret != 0) return -1;	

    write_to_socket(socket_fd, buf);
	
    ret = pthread_mutex_unlock(&socket_mutex);
    if (ret != 0) return -1;

    debug(0, "write < %.*s > from <%s> to <%s>", sms_len, str,
	  msg->receiver, msg->sender);

    return 0;
}


/*
 * do the split procedure and send several sms messages
 *
 * return -1 on failure, 0 if Ok.
 */
static int do_split_send(SMSMessage *msg, char *str,
			 int maxmsgs, URLTranslation *trans)
{
    char *p;
    char *suf, *sc;
    char buf[1024];
    int slen = 0;
    int size;

    suf = urltrans_split_suffix(trans);
    sc = urltrans_split_chars(trans);
    if (suf != NULL)
	slen = strlen(suf);

    for(p = str; maxmsgs > 1; maxmsgs--) {
	size = sms_len - slen;	/* leave room to split-suffix */

	if (sc)
	    size = str_reverse_seek(p, size, sc);

	/* do not accept a bit too small fractions... */
	if (size < sms_len/2)
	    size = sms_len - slen;

	sprintf(buf, "%.*s%s", size, p, suf ? suf : "");
	if (do_sending(msg, buf) < 0)
	    return -1;

	p += size;
    }
    if (do_sending(msg, p) < 0)
	return -1;

    return 0;
}

/*
 * send the 'reply', according to settings in 'trans' and 'msg'
 *
 * return -1 if failed utterly, 0 otherwise
 */
static int send_message(URLTranslation *trans, SMSMessage *msg, char *reply)
{
    char *rstr = reply;
    int max_msgs;
    int len;
    
    max_msgs = urltrans_max_messages(trans);
    
    if (strlen(reply)==0) {
	if (urltrans_omit_empty(trans) != 0) {
	    max_msgs = 0;
	}
	else
	    rstr = "<Empty reply from service provider>";
    }
    len = strlen(rstr);

    if (max_msgs == 0)
	info(0, "No reply sent, denied.");
    else if (len <= sms_len) {
	if (do_sending(msg, rstr) < 0)
	    goto error;
    } else if (len > sms_len && max_msgs == 1) {
	/* truncated reply */
	if (do_sending(msg, rstr) < 0)
	    goto error;
    } else {
	/*
	 * we have a message that is longer than what fits in one
	 * SMS message and we are allowed to split it
	 */
	if (do_split_send(msg, rstr, max_msgs, trans) < 0)
	    goto error;
    }
    return 0;

error:
    error(0, "send message failed");
    return -1;
}

/*
 * handle one MO request
 */
static void *request_thread(void *arg) {
    unsigned long id;
    SMSMessage *msg;
    URLTranslation *trans;
    char *reply = NULL, *p;
    
    msg = arg;
    id = (unsigned long) pthread_self();

    req_threads++;
    
    if (octstr_len(msg->text) == 0 ||
	strlen(msg->sender) == 0 ||
	strlen(msg->receiver) == 0) {
	error(0, "EMPTY: Text is <%s>, sender is <%s>, receiver is <%s>",
	      octstr_get_cstr(msg->text), msg->sender, msg->receiver);

	/* NACK should be returned here if we use such things... future
	   implementation! */
	   
	return NULL;
    }
    if (strcmp(msg->sender, msg->receiver) == 0) {
	info(0, "NOTE: sender and receiver same number <%s>, ignoring!",
	     msg->sender);
	return NULL;
    }
    trans = urltrans_find(translations, msg);
    if (trans == NULL)
	goto error;

    p = urltrans_faked_sender(trans);
    if (p != NULL) {
	free(msg->receiver);		/* that is us, we swap these at send */
	msg->receiver = strdup(p);
    }
    else if (global_sender != NULL) {
	free(msg->receiver);		/* that is us, we swap these at send */
	msg->receiver = strdup(global_sender);
    }
    if (msg->receiver == NULL)
	goto error;
    
    /* TODO: check if the sender is approved to use this service */

    info(0, "starting to service request <%s> from <%s> to <%s>",
	octstr_get_cstr(msg->text), msg->sender, msg->receiver);
    
    reply = obey_request(trans, msg);
    if (reply == NULL) {
	error(0, "request failed");
	reply = strdup("Request failed");
    }
    if (reply == NULL || send_message(trans, msg, reply) < 0)
	goto error;

    smsmessage_destruct(msg);
    free(reply);
    req_threads--;
    return NULL;
error:
    error(errno, "request_thread: failed");
    smsmessage_destruct(msg);
    free(reply);
    req_threads--;
    return NULL;
        
}


static void new_request(char *buf)
{
    SMSMessage *msg;
    char *sender, *receiver, *text;
    char *p;
    int id;

    id = atoi(buf);
    p = strchr(buf, ' ');
    if (p == NULL)
	sender = receiver = text = "";
    else {
	*p++ = '\0';
	sender = p;
	p = strchr(sender, ' ');
	if (p == NULL)
	    receiver = text = "";
	else {
	    *p++ = '\0';
	    receiver = p;
	    p = strchr(receiver, ' ');
	    if (p == NULL)
		text = "";
	    else {
		*p++ = '\0';
		text = p;
	    }
	}
    }
    msg = smsmessage_construct(sender, receiver, octstr_create(text));
    if (msg != NULL) {
	msg->id = id;
	(void)start_thread(1, request_thread, msg, 0);
    }
}



/*-----------------------------------------------------------
 * HTTP ADMINSTRATION
 */

static char *sendsms_request(CGIArg *list)
{
    SMSMessage *msg;
    URLTranslation *t;
    char *val, *from, *to, *text;
    int ret;
    
    if (cgiarg_get(list, "username", &val) == -1)
	return "Authorization failed";	

    t = urltrans_find_username(translations, val);
    if (t == NULL || cgiarg_get(list, "password", &val) == -1 ||
	strcmp(val, urltrans_password(t)) != 0)

	return "Authorization failed";	

    if (cgiarg_get(list, "to", &to) == -1 ||
	cgiarg_get(list, "text", &text) == -1) {

	error(0, "/cgi-bin/sendsms got wrong args");
	return "Wrong sendsms args.";
    }
    if (urltrans_faked_sender(t) != NULL)
	from = urltrans_faked_sender(t);
    else if (cgiarg_get(list, "from", &from) == 0)
	;
    else if (global_sender != NULL)
	from = global_sender;
    else
	return "Sender missing and no global set";
    
    info(0, "/cgi-bin/sendsms <%s> <%s> <%s>", from, to, text);
    
    msg = smsmessage_construct(to, from, octstr_create(""));
    if (msg == NULL)
	goto error;
    ret = send_message(t, msg, text);

    if (ret == -1)
	goto error;

    smsmessage_destruct(msg);
    return "Sent.";
    
error:
    error(errno, "sendsms_request: failed");
    smsmessage_destruct(msg);
    req_threads--;
    return "Sending failed.";
}


static void *http_request_thread(void *arg)
{
    int client;
    char *path, *args;
    char *answer;
    CGIArg *arglist;
    
    client = httpserver_get_request(http_fd, &path, &args);
    http_accept_pending = 0;
    if (client == -1) {
	error(0, "Failed to get request from client, killing thread");
	return NULL;
    }
    /* print client information */

    info(0, "Get HTTP request < %s >", path);
    
    if (strcmp(path, "/cgi-bin/sendsms") == 0) {
	
	arglist = cgiarg_decode_to_list(args);
	answer = sendsms_request(arglist);
	
	cgiarg_destroy_list(arglist);
    } else
	answer = "unknown request";
    info(0, "%s", answer);

    if (httpserver_answer(client, answer) == -1)
	error(0, "Error responding to client. Too bad.");

    /* answer closes the socket */
    
    return NULL;
}


static void http_start_thread()
{
    (void)start_thread(1, http_request_thread, NULL, 0);
}



/*------------------------------------------------------------*/


static void write_pid_file(void) {
    FILE *f;
        
    if (pid_file != NULL) {
	f = fopen(pid_file, "w");
	fprintf(f, "%d\n", getpid());
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



void main_loop()
{
    char linebuf[1024+1], buf[32];
    time_t start, t;
    int ret, secs;
    int total = 0;
    fd_set rf;
    struct timeval to;

    if (http_fd == -1)
	http_accept_pending = -1;
    else
	http_accept_pending = 0;
    
    start = t = time(NULL);
    while(!abort_program) {

	if (time(NULL)-t > heartbeat_freq) {
	    sprintf(buf, "H%d\n", req_threads);
	    if (write_to_socket(socket_fd, buf)<0)
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

	if (ret < 0)
	    goto error;
	if (ret > 0 && http_accept_pending == 0 && FD_ISSET(http_fd, &rf)) {

	    http_accept_pending = 1;
	    http_start_thread();
	    continue;
	}
	else if (ret > 0 && FD_ISSET(socket_fd, &rf)) {
	    ret = read_line(socket_fd, linebuf, 1024);
	    if (ret < 1) {
		error(0, "read line failed!");
		break;
	    }
	    debug(0, "Read < %s > (load: %d)", linebuf, req_threads);

	    /* ignore ack/nack, TODO: do not ignore
	     */
	    if (*linebuf == 'A' || *linebuf == 'N')
		continue;

/*	    if (write_to_socket(socket_fd, "A\n")<0)
 *		goto error;
 */
	    if (req_threads % 10 == 9) {
		sprintf(buf, "H%d\n", req_threads);
		if (write_to_socket(socket_fd, buf)<0)
		    goto error;
		t = time(NULL);
	    }
	    ret = pthread_mutex_unlock(&socket_mutex);
	    if (ret != 0) goto error;

	    if (total == 0)
		t = time(NULL);
	    total++;
	    new_request(linebuf);

	    ret = pthread_mutex_lock(&socket_mutex);
	    if (ret != 0) goto error;
	    continue;
	}
	ret = pthread_mutex_unlock(&socket_mutex);
	if (ret != 0) goto error;
	    
	usleep(1000);

	ret = pthread_mutex_lock(&socket_mutex);
	if (ret != 0) goto error;
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
    
    cf_index = get_and_set_debugs(argc, argv, NULL);

    warning(0, "Gateway SMS BOX version %s starting", VERSION);

    pthread_mutex_init(&socket_mutex, NULL);

    setup_signal_handlers();
    cfg = config_from_file(argv[cf_index], "smsbox.conf");
    if (cfg == NULL)
	panic(0, "No configuration, aborting.");

    init_smsbox(cfg);
    write_pid_file();

    translations = urltrans_create();
    if (translations == NULL)
	panic(errno, "urltrans_create failed");
    if (urltrans_add_cfg(translations, cfg) == -1)
	panic(errno, "urltrans_add_cfg failed");

    while(!abort_program) {
	socket_fd = tcpip_connect_to_server(bb_host, bb_port);
	if (socket_fd > -1)
	    break;
	sleep(10);
    }
    info(0, "Connected to Bearer Box at %s port %d", bb_host, bb_port);

    main_loop();
    return 0;
}

