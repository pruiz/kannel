
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "http.h"
#include "html.h"
#include "cgi.h"
#include "msg.h"

#include "smsbox_req.h"
#include "urltrans.h"
#include "wapitlib.h"
#include "octstr.h"

/*
 * this module handles the request handling - that is, finding
 * the correct urltranslation, fetching the result and then
 * splitting it into several messages if needed to
 *
 */

/* Global variables */

static URLTranslationList *translations = NULL;
static int sms_max_length = -1;		/* not initialized */
static char *global_sender = NULL;
static int (*sender) (Msg *msg) = NULL;

static sig_atomic_t req_threads = 0;


/*-------------------------------------------------------------------*
 * STATIC FUNCTIONS
 */

/* Perform the service requested by the user: translate the request into
 * a pattern, if it is an URL, fetch it, and return a string, which must
 * be free'ed by the caller
 */
static char *obey_request(URLTranslation *trans, Msg *sms)
{
	char *pattern;
	char *data, *tmpdata;
	size_t size;
	char *type = NULL;
	char replytext[1024*10+1];       /* ! absolute limit ! */


	pattern = urltrans_get_pattern(trans, sms);
	if (pattern == NULL) {
		error(0, "Oops, urltrans_get_pattern failed.");
		return NULL;
	}

	if (urltrans_type(trans) == TRANSTYPE_TEXT) {

		debug(0, "formatted text answer: <%s>", pattern);
		return pattern;

	} else if (urltrans_type(trans) == TRANSTYPE_FILE) {

		int fd;
		size_t len;
	
		fd = open(pattern, O_RDONLY);
		if (fd == -1) {
		    error(errno, "Couldn't open file <%s>", pattern);
		    return NULL;
		}

		replytext[0] = '\0';
		len = read(fd, replytext, 1024*10);
		close(fd);
		replytext[len-1] = '\0';	/* remove trainling '\n' */

		return strdup(replytext);
	}

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

	if(strcmp(type, "text/html") == 0) {

		if (urltrans_prefix(trans) != NULL &&
		    urltrans_suffix(trans) != NULL) {

		    tmpdata = html_strip_prefix_and_suffix(data,
			       urltrans_prefix(trans), urltrans_suffix(trans));
		    free(data);	
		    data = tmpdata;
		}
		html_to_sms(replytext, sizeof(replytext), data);

	} else if(strcmp(type, "text/plain") == 0) {

		strncpy(replytext, data, sizeof(replytext) - 1);

	} else {

		strcpy(replytext,
		       "Result could not be represented as an SMS message.");

	}

	free(data);
	free(type);

	if (strlen(replytext)==0)
		return strdup("");

	return strdup(replytext);

error:
    return NULL;
}


/*
 * sends the buf, with msg-info - does NO splitting etc. just the sending
 *
 * NOTE: the sender frees the message!
 *
 * return -1 on failure, 0 if Ok.
 */
static int do_sending(Msg *msg)
{
    debug(0, "Sending msg");

    if (sms_max_length < 0) return -1;
    
    if (sender(msg) < 0)
	goto error;

    /* sender does the freeing (or uses msg as it sees fit) */

    return 0;
error:
    error(0, "Failed");
    return -1;
}


/*
 * do the split procedure and send several sms messages
 *
 * return -1 on failure, 0 if Ok.
 */
static int do_split_send(Msg *msg, int maxmsgs, URLTranslation *trans)
{
    Msg *split;
    
    char *p, *suf, *sc;
    int slen = 0;
    int size, total_len, loc;

    suf = urltrans_split_suffix(trans);
    sc = urltrans_split_chars(trans);
    if (suf != NULL)
	slen = strlen(suf);

    if(msg->smart_sms.flag_udh) {
	warning(0, "Cannot send too long UDH!");
	return 0;
    }
    total_len = octstr_len(msg->smart_sms.msgdata);
    
    for(loc = 0, p = octstr_get_cstr(msg->smart_sms.msgdata);
	maxmsgs > 0 && loc < total_len;
	maxmsgs--) {

	if (maxmsgs == 1 || total_len-loc < sms_max_length) {
	    slen = 0;
	    suf = NULL;
	    sc = NULL;
	}
	size = sms_max_length - slen;	/* leave room to split-suffix */
	/*
	 * if we use split chars, find the first from starting from
	 * the end of sms message and return partion _before_ that
	 */
	if (sc)
	    size = str_reverse_seek(p+loc, size-1, sc) + 1;

	/* do not accept a bit too small fractions... */
	if (size < sms_max_length/2)
	    size = sms_max_length - slen;

	if ((split = msg_duplicate(msg))==NULL)
	    goto error;

	octstr_replace(split->smart_sms.msgdata, p+loc, size);
	if (suf != NULL)
	    octstr_insert_data(split->smart_sms.msgdata, size, suf, slen);
	
	if (do_sending(split) < 0)
	    return -1;

	loc += size;
    }
    msg_destroy(msg);	/* we must delete at as it is supposed to be deleted */
    return 0;
error:
    error(0, "Memory allocation failed!");
    msg_destroy(msg);
    return -1;
    
}

/*
 * send the 'reply', according to settings in 'trans' and 'msg'
 *
 * return -1 if failed utterly, 0 otherwise
 */
static int send_message(URLTranslation *trans, Msg *msg)
{
    int max_msgs;
    static char *empty = "<Empty reply from service provider>";
    
    max_msgs = urltrans_max_messages(trans);
	
    if(msg_type(msg) != smart_sms) {
	info(0, "msgtype failed");
	goto error;
    }    
    if (octstr_len(msg->smart_sms.msgdata)==0) {
	if (urltrans_omit_empty(trans) != 0) {
	    max_msgs = 0;
	} else { 
	    if (octstr_replace(msg->smart_sms.msgdata, empty, strlen(empty)) == -1) {
		info(0, "replace failed");
		goto error;
	    }
	}
    }
    if (max_msgs == 0)
	info(0, "No reply sent, denied.");
	
    else if (octstr_len(msg->smart_sms.msgdata) <= sms_max_length) {
	if (do_sending(msg) < 0)
	    goto error;

    } else if (octstr_len(msg->smart_sms.msgdata) > sms_max_length &&
	       max_msgs == 1) {

	/* truncate reply */
	    
	octstr_truncate(msg->smart_sms.msgdata, sms_max_length);	
	if (do_sending(msg) < 0)
	    goto error;

    } else {
	/*
	 * we have a message that is longer than what fits in one
	 * SMS message and we are allowed to split it
	 */
	if (do_split_send(msg, max_msgs, trans) < 0)
	    goto error;
    }
    return 0;

error:
    error(0, "send message failed");
    return -1;
}


/*----------------------------------------------------------------*
 * PUBLIC FUNCTIONS
 */

int smsbox_req_init(URLTranslationList *transls,
		    int sms_max,
		    char *global,
		    int (*send) (Msg *msg))
{
	translations = transls;
	sms_max_length = sms_max;
	if (global != NULL) {
		global_sender = strdup(global);
		if (global_sender == NULL)
		    return -1;
	}
	sender = send;
	return 0;
}


int smsbox_req_count(void)
{
	return (int)req_threads;
}


void *smsbox_req_thread(void *arg) {
    unsigned long id;
    Msg *msg;
    Octstr *tmp;
    URLTranslation *trans;
    char *reply = NULL, *p;
    
    msg = arg;
    id = (unsigned long) pthread_self();

    req_threads++;	/* possible overflow */
    
	if (octstr_len(msg->smart_sms.msgdata) == 0 ||
		octstr_len(msg->smart_sms.sender) == 0 ||
		octstr_len(msg->smart_sms.receiver) == 0) 
	{

		error(0, "smsbox_req_thread: EMPTY Msg, dump follows:");
		msg_dump(msg);
		/* NACK should be returned here if we use such 
		   things... future implementation! */
	   
		return NULL;
	}

	if (octstr_compare(msg->smart_sms.sender, msg->smart_sms.receiver) == 0) {
		info(0, "NOTE: sender and receiver same number <%s>, ignoring!",
		     octstr_get_cstr(msg->smart_sms.sender));
		return NULL;
	}

	trans = urltrans_find(translations, msg->smart_sms.msgdata);
	if (trans == NULL) goto error;

	/*
	 * now, we change the sender (receiver now 'cause we swap them later)
	 * if faked-sender or similar set. Note that we ignore if the replacement
	 * fails.
	 */
	tmp = octstr_duplicate(msg->smart_sms.sender);
	if (tmp == NULL) goto error;
	
	p = urltrans_faked_sender(trans);
	if (p != NULL)
		octstr_replace(msg->smart_sms.sender, p, strlen(p));
	else if (global_sender != NULL)
		octstr_replace(msg->smart_sms.sender, global_sender, strlen(global_sender));
	else {
		Octstr *t = msg->smart_sms.sender;
		msg->smart_sms.sender = msg->smart_sms.receiver;
		msg->smart_sms.receiver = t;
	}
	octstr_destroy(msg->smart_sms.receiver);
	msg->smart_sms.receiver = tmp;

	/* TODO: check if the sender is approved to use this service */

	reply = obey_request(trans, msg);
	if (reply == NULL) {
		error(0, "request failed");
		reply = strdup("Request failed");
		goto error;
	}

	 if (octstr_replace(msg->smart_sms.msgdata, reply, strlen(reply)) == -1)
		goto error;

	msg->smart_sms.time = time(NULL);	/* set current time */

	/* send_message frees the 'msg' */
	if(send_message(trans, msg) < 0)
		error(0, "request_thread: failed");
    
	free(reply);
	req_threads--;
	return NULL;
error:
	error(0, "request_thread: failed");
	msg_destroy(msg);
	free(reply);
	req_threads--;
	return NULL;
}


char *smsbox_req_sendsms(CGIArg *list)
{
	Msg *msg = NULL;
	URLTranslation *t = NULL;
	char *val, *from, *to, *text;
	char *udh = NULL;
	int ret;
    
	if (cgiarg_get(list, "username", &val) == -1)
		t = urltrans_find_username(translations, "default");
	else 
		t = urltrans_find_username(translations, val);
    
	if (t == NULL || 
		cgiarg_get(list, "password", &val) == -1 ||
		strcmp(val, urltrans_password(t)) != 0) {
		return "Authorization failed";
	}

	cgiarg_get(list, "udh", &udh);

	if (cgiarg_get(list, "to", &to) == -1 ||
		cgiarg_get(list, "text", &text) == -1)
	{
		error(0, "/cgi-bin/sendsms got wrong args");
		return "Wrong sendsms args.";
	}

	if (urltrans_faked_sender(t) != NULL) {
		from = urltrans_faked_sender(t);
	} else if (cgiarg_get(list, "from", &from) == 0 &&
	       *from != '\0') 
	{
		/* Do nothing. */
	} else if (global_sender != NULL) {
		from = global_sender;
	} else {
		return "Sender missing and no global set";
	}
    
	info(0, "/cgi-bin/sendsms <%s> <%s> <%s>", from, to, text);
  
	msg = msg_create(smart_sms);
	if (msg == NULL) goto error;

	msg->smart_sms.receiver = octstr_create(to);
	msg->smart_sms.sender = octstr_create(from);
	msg->smart_sms.msgdata = octstr_create(text);
	msg->smart_sms.udhdata = octstr_create("");

	if(udh==NULL) {
		msg->smart_sms.flag_8bit = 0;
		msg->smart_sms.flag_udh  = 0;
	} else {
		msg->smart_sms.flag_8bit = 1;
		msg->smart_sms.flag_udh  = 1;
	}

	msg->smart_sms.time = time(NULL);
   
	/* send_message frees the 'msg' */
	ret = send_message(t, msg);

    if (ret == -1)
	goto error;

    return "Sent.";
    
error:
    error(0, "sendsms_request: failed");
    return "Sending failed.";
}

