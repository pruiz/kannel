
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
    else if (urltrans_type(trans) == TRANSTYPE_FILE) {
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
static int do_sending(Msg *msg, char *str)
{
    Msg *pmsg;

    if (sms_max_length < 0) return -1;
    
    pmsg = msg_create(plain_sms);
    if (pmsg == NULL)
	goto error;

    /* note the switching of sender and receiver */

    pmsg->plain_sms.receiver = octstr_duplicate(msg->plain_sms.receiver);
    pmsg->plain_sms.sender = octstr_duplicate(msg->plain_sms.sender);
    pmsg->plain_sms.text = octstr_create_limited(str, sms_max_length);

    if (sender(pmsg) < 0)
	goto error;

    free(pmsg);

    return 0;
error:
    free(pmsg);
    error(0, "Memory allocation failed");
    return -1;
}


/*
 * do the split procedure and send several sms messages
 *
 * return -1 on failure, 0 if Ok.
 */
static int do_split_send(Msg *msg, char *str, int maxmsgs,
			 URLTranslation *trans)
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

    for(p = str; maxmsgs > 1 && strlen(p) > sms_max_length; maxmsgs--) {
	size = sms_max_length - slen;	/* leave room to split-suffix */

	/*
	 * if we use split chars, find the first from starting from
	 * the end of sms message and return partion _before_ that
	 */

	if (sc)
	    size = str_reverse_seek(p, size, sc) + 1;

	/* do not accept a bit too small fractions... */
	if (size < sms_max_length/2)
	    size = sms_max_length - slen;

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
static int send_message(URLTranslation *trans, Msg *msg, char *reply)
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
    else if (len <= sms_max_length) {
	if (do_sending(msg, rstr) < 0)
	    goto error;
    } else if (len > sms_max_length && max_msgs == 1) {
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
    
    if (octstr_len(msg->plain_sms.text) == 0 ||
	octstr_len(msg->plain_sms.sender) == 0 ||
	octstr_len(msg->plain_sms.receiver) == 0) {
	error(0, "EMPTY: Text is <%s>, sender is <%s>, receiver is <%s>",
	      octstr_get_cstr(msg->plain_sms.text),
	      octstr_get_cstr(msg->plain_sms.sender),
	      octstr_get_cstr(msg->plain_sms.receiver));

	/* NACK should be returned here if we use such things... future
	   implementation! */
	   
	return NULL;
    }
    if (octstr_compare(msg->plain_sms.sender, msg->plain_sms.receiver) == 0) {
	info(0, "NOTE: sender and receiver same number <%s>, ignoring!",
	     octstr_get_cstr(msg->plain_sms.sender));
	return NULL;
    }
    trans = urltrans_find(translations, msg->plain_sms.text);
    if (trans == NULL)
	goto error;

    /*
     * now, we change the sender (receiver now 'cause we swap them later)
     * if faked-sender or similar set. Note that we ignore if the replacement
     * fails.
     */
    tmp = octstr_duplicate(msg->plain_sms.sender);
    if (tmp == NULL)
	goto error;
	
    p = urltrans_faked_sender(trans);
    if (p != NULL)
	octstr_replace(msg->plain_sms.sender, p, strlen(p));
    else if (global_sender != NULL)
	octstr_replace(msg->plain_sms.sender, global_sender, strlen(global_sender));
    else {
	Octstr *t = msg->plain_sms.sender;
	msg->plain_sms.sender = msg->plain_sms.receiver;
	msg->plain_sms.receiver = t;
    }
    octstr_destroy(msg->plain_sms.receiver);
    msg->plain_sms.receiver = tmp;

    /* TODO: check if the sender is approved to use this service */

    info(0, "starting to service request <%s> from <%s> to <%s>",
	      octstr_get_cstr(msg->plain_sms.text),
	      octstr_get_cstr(msg->plain_sms.sender),
	      octstr_get_cstr(msg->plain_sms.receiver));

    msg->plain_sms.time = time(NULL);	/* set current time */
    reply = obey_request(trans, msg);
    if (reply == NULL) {
	error(0, "request failed");
	reply = strdup("Request failed");
    }
    if (reply == NULL || send_message(trans, msg, reply) < 0)
	goto error;

    msg_destroy(msg);
    free(reply);
    req_threads--;
    return NULL;
error:
    error(errno, "request_thread: failed");
    msg_destroy(msg);
    free(reply);
    req_threads--;
    return NULL;
        
}


char *smsbox_req_sendsms(CGIArg *list)
{
    Msg *msg;
    URLTranslation *t;
    char *val, *from, *to, *text;
    char *udh = NULL;
    int ret;
    
	if (cgiarg_get(list, "username", &val) == -1)
		return "Authorization failed";	

	t = urltrans_find_username(translations, val);
	if (t == NULL || 
		cgiarg_get(list, "password", &val) == -1 ||
		strcmp(val, urltrans_password(t)) != 0)
	{
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
		   *from != '\0') {
	    ;
	} else if (global_sender != NULL) {
		from = global_sender;
	} else {
		return "Sender missing and no global set";
	}
    
	info(0, "/cgi-bin/sendsms <%s> <%s> <%s>", from, to, text);

	if(udh==NULL) {
  
		msg = msg_create(plain_sms);
		if (msg == NULL) goto error;

		msg->plain_sms.receiver = octstr_create(to);
		msg->plain_sms.sender = octstr_create(from);
		msg->plain_sms.text = octstr_create("");
		msg->plain_sms.time = time(NULL);
    
		ret = send_message(t, msg, text);

	} else {
  
		msg = msg_create(smart_sms);
		if (msg == NULL) goto error;

		msg->smart_sms.receiver = octstr_create(to);
		msg->smart_sms.sender = octstr_create(from);
		msg->smart_sms.msgdata = octstr_create("");
		msg->smart_sms.udhdata = octstr_create("");
		msg->smart_sms.flag_8bit = 1;
		msg->smart_sms.flag_udh  = 1;
		msg->smart_sms.time = time(NULL);
    
		ret = send_message(t, msg, text);

	}

    if (ret == -1)
	goto error;

    msg_destroy(msg);
    return "Sent.";
    
error:
    error(errno, "sendsms_request: failed");
    msg_destroy(msg);
    req_threads--;
    return "Sending failed.";
}

