
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

#include "gwlib/gwlib.h"
#include "html.h"
#include "cgi.h"
#include "msg.h"

#include "smsbox_req.h"
#include "urltrans.h"

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

static volatile sig_atomic_t req_threads = 0;


/*-------------------------------------------------------------------*
 * STATIC FUNCTIONS
 */

/* Perform the service requested by the user: translate the request into
 * a pattern, if it is an URL, fetch it, and return a string, which must
 * be free'ed by the caller
 */
static char *obey_request(URLTranslation *trans, Msg *sms)
{
	char *pattern, *ret;
	Octstr *os, *url, *final_url, *reply_body, *type, *charset,
		*temp, *replytext;
	List *request_headers, *reply_headers;
	int status;

	gw_assert(sms != NULL);
	gw_assert(msg_type(sms) == smart_sms);

	pattern = urltrans_get_pattern(trans, sms);
	gw_assert(pattern != NULL);

	switch (urltrans_type(trans)) {
	case TRANSTYPE_TEXT:
		debug("sms", 0, "formatted text answer: <%s>", pattern);
		ret = pattern;
		break;

	case TRANSTYPE_FILE:
		replytext = octstr_read_file(pattern);
		gw_free(pattern);
		ret = gw_strdup(octstr_get_cstr(replytext));
		octstr_destroy(replytext);
		break;

	case TRANSTYPE_URL:
		url = octstr_create(pattern);
		request_headers = list_create();
		status = http_get_real(url, request_headers, &final_url,
					&reply_headers, &reply_body);
		gw_free(pattern);		/* no longer needed */
		octstr_destroy(url);
		octstr_destroy(final_url);
		list_destroy(request_headers);
		if (status != HTTP_OK) {
			while ((os = list_extract_first(reply_headers)) != NULL)
				octstr_destroy(os);
			list_destroy(reply_headers);
			octstr_destroy(reply_body);
			goto error;
		}
		
		http_header_get_content_type(reply_headers, &type, &charset);
		if (octstr_str_compare(type, "text/html") == 0) {
			if (urltrans_prefix(trans) != NULL &&
			    urltrans_suffix(trans) != NULL) {
			    temp = html_strip_prefix_and_suffix(reply_body,
				       urltrans_prefix(trans), 
				       urltrans_suffix(trans));
			    octstr_destroy(reply_body);
			    reply_body = temp;
			}
			replytext = html_to_sms(reply_body);
		} else if (octstr_str_compare(type, "text/plain") == 0) {
			replytext = reply_body;
			reply_body = NULL;
		} else {
			replytext = octstr_create("Result could not be represented "
						  "as an SMS message.");
		}
	
		octstr_destroy(type);
		octstr_destroy(charset);
		while ((os = list_extract_first(reply_headers)) != NULL)
			octstr_destroy(os);
		list_destroy(reply_headers);
		octstr_destroy(reply_body);
	
		if (octstr_len(replytext) == 0)
			ret = gw_strdup("");
		else {
			octstr_strip_blank(replytext);
			ret = gw_strdup(octstr_get_cstr(replytext));
		}
		octstr_destroy(replytext);
	
		break;

	default:
		error(0, "Unknown URL translation type %d", 
			urltrans_type(trans));
		return NULL;
	}
	
	return ret;

error:
	return NULL;
}


/*
 * sends the buf, with msg-info - does NO splitting etc. just the sending
 * NOTE: the sender gw_frees the message!
 *
 * return -1 on failure, 0 if Ok.
 */
static int do_sending(Msg *msg)
{
    if (sms_max_length < 0) return -1;

    if (sender(msg) < 0)
	goto error;

    /* sender does the freeing (or uses msg as it sees fit) */

    return 0;
error:
    error(0, "Msg send failed");
    return -1;
}



/*
 * Take a Msg structure and send it as a MT SMS message.
 * Works only with plain sms messages, discards UDH
 *
 * Return -1 on failure, 0 if Ok.
 */
static int do_split_send(Msg *msg, int maxmsgs, URLTranslation *trans,
			 char *h, int hl, char *f, int fl)
{
    Msg *split;

    char *p, *suf, *sc;
    int slen = 0;
    int size, total_len, loc;

    gw_assert(trans != NULL);
    
    suf = urltrans_split_suffix(trans);
    if (suf != NULL) slen = strlen(suf);
    sc = urltrans_split_chars(trans);

    total_len = octstr_len(msg->smart_sms.msgdata);
    
    for(loc = 0, p = octstr_get_cstr(msg->smart_sms.msgdata);
	maxmsgs > 0 && loc < total_len;
	maxmsgs--)
    {
	if (total_len-loc < sms_max_length-fl-hl) { 	/* message ends */
	    slen = 0;
	    suf = sc = NULL;
	    size = total_len - loc;
	} else if (maxmsgs == 1) {			/* last part */
	    slen = 0;
	    suf = sc = NULL;
	    size = sms_max_length -hl -fl;
	} else						/* other parts */
	    size = sms_max_length - slen -hl -fl;

	/*
	 * if we use split chars, find the first from starting from
	 * the end of sms message and return partion _before_ that
	 */
	if (sc) {
	    size = str_reverse_seek(p+loc, size-1, sc) + 1;

	    /* do not accept a bit too small fractions... */
	    if (size < sms_max_length/2)
		size = sms_max_length - slen -hl -fl;
	}
	split = msg_duplicate(msg);
	
	if (h != NULL) {	/* add header and message */
	    octstr_replace(split->smart_sms.msgdata, h, hl);
	    octstr_insert_data(split->smart_sms.msgdata, hl, p+loc, size);    
	} else			/* just the message */
	    octstr_replace(split->smart_sms.msgdata, p+loc, size);
	
	if (suf != NULL)
	    octstr_insert_data(split->smart_sms.msgdata, size, suf, slen);
	
	if (f != NULL)	/* add footer */
	    octstr_insert_data(split->smart_sms.msgdata, size+hl, f, fl);

	if (do_sending(split) < 0) {
	    msg_destroy(msg);
	    return -1;
	}
	loc += size;
    }
    msg_destroy(msg);	/* we must delete at as it is supposed to be deleted */
    return 0;
}



/*
 * send UDH message (or messages) according to data in *msg
 */
static int send_udh_sms(URLTranslation *trans, Msg *msg, int max_msgs)
{
    /*
     * TODO XXX
     * maybe we should truncate the message herein
     *
     * this is NOT the right way to do it, but hopefully this is
     * enough for right now
     */
    
    octstr_truncate(msg->smart_sms.msgdata, sms_max_length);
    octstr_truncate(msg->smart_sms.udhdata, sms_max_length);
    
    /*
     * TODO XXX : UDH split send?
     */
    
    return do_sending(msg);
}


/*
 * send SMS without UDH, with all those fancy bits and parts
 */
static int send_plain_sms(URLTranslation *trans, Msg *msg, int max_msgs)
{    
    int hl, fl;
    char *h, *f;

    h = urltrans_header(trans);
    f = urltrans_footer(trans);

    if (h != NULL) hl = strlen(h); else hl = 0;
    if (f != NULL) fl = strlen(f); else fl = 0;

    if (octstr_len(msg->smart_sms.msgdata) <= (sms_max_length - fl - hl)
	|| max_msgs == 1) {

	if (h != NULL)	/* if header set */
	    octstr_insert_data(msg->smart_sms.msgdata, 0, h, hl);
	/*
	 * truncate if the message is too long one (this only happens if
	 *  max_msgs == 1)
	 */

	if (octstr_len(msg->smart_sms.msgdata)+fl > sms_max_length)
	    octstr_truncate(msg->smart_sms.msgdata, sms_max_length - fl);
	    
	if (f != NULL)	/* if footer set */
	    octstr_insert_data(msg->smart_sms.msgdata,
				   octstr_len(msg->smart_sms.msgdata), f, fl);

	return do_sending(msg);

    } else {
	/*
	 * we have a message that is longer than what fits in one
	 * SMS message and we are allowed to split it
	 */

	return do_split_send(msg, max_msgs, trans, h, hl, f, fl);
    }
}



/*
 * send the 'reply', according to settings in 'trans' and 'msg'
 * return -1 if failed utterly, 0 otherwise
 */
static int send_message(URLTranslation *trans, Msg *msg)
{
    int max_msgs;
    static char *empty = "<Empty reply from service provider>";

    max_msgs = urltrans_max_messages(trans);

    if(msg_type(msg) != smart_sms) {
	error(0, "Weird messagetype for send_message!");
	msg_destroy(msg);
	return -1;
    }

    if (max_msgs == 0) {
	info(0, "No reply sent, denied.");
	msg_destroy(msg);
	return 0;
    }

    if(msg->smart_sms.flag_udh)
	return send_udh_sms(trans, msg, 1);
    
    if (octstr_len(msg->smart_sms.msgdata)==0) {
	if (urltrans_omit_empty(trans) != 0) {
	    max_msgs = 0;
	} else { 
	    octstr_replace(msg->smart_sms.msgdata, empty, strlen(empty));
	}
    }
    return send_plain_sms(trans, msg, max_msgs);
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
	sender = send;
	if (global != NULL) {
		global_sender = gw_strdup(global);
	}
	return 0;
}


int smsbox_req_count(void)
{
	return (int)req_threads;
}


void smsbox_req_thread(void *arg) {
    Msg *msg;
    Octstr *tmp;
    URLTranslation *trans;
    char *reply = NULL, *p;
    
    msg = arg;
    req_threads++;	/* possible overflow */
    
    if ((octstr_len(msg->smart_sms.msgdata) == 0 &&
	octstr_len(msg->smart_sms.udhdata) == 0) ||
	octstr_len(msg->smart_sms.sender) == 0 ||
	octstr_len(msg->smart_sms.receiver) == 0) 
    {

	error(0, "smsbox_req_thread: EMPTY Msg, dump follows:");
	msg_dump(msg, 0);
		/* NACK should be returned here if we use such 
		   things... future implementation! */
	   
	return;
    }

    if (octstr_compare(msg->smart_sms.sender, msg->smart_sms.receiver) == 0) {
	info(0, "NOTE: sender and receiver same number <%s>, ignoring!",
	     octstr_get_cstr(msg->smart_sms.sender));
	return;
    }

    trans = urltrans_find(translations, msg->smart_sms.msgdata);
    if (trans == NULL) goto error;

    info(0, "Starting to service <%s> from <%s> to <%s>",
	 octstr_get_cstr(msg->smart_sms.msgdata),
	 octstr_get_cstr(msg->smart_sms.sender),
	 octstr_get_cstr(msg->smart_sms.receiver));

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
		reply = gw_strdup("Request failed");
		goto error;
    }

    octstr_replace(msg->smart_sms.msgdata, reply, strlen(reply));

    msg->smart_sms.flag_8bit = 0;
    msg->smart_sms.flag_udh  = 0;
    msg->smart_sms.time = time(NULL);	/* set current time */

	/* send_message frees the 'msg' */
    if(send_message(trans, msg) < 0)
		error(0, "request_thread: failed");
    
    gw_free(reply);
    req_threads--;
    return;

error:
    error(0, "Request_thread: failed");
    msg_destroy(msg);
    gw_free(reply);
    req_threads--;
}


char *smsbox_req_sendsms(List *list)
{
	Msg *msg = NULL;
	URLTranslation *t = NULL;
	Octstr *user = NULL, *val, *from = NULL, *to;
	Octstr *text = NULL, *udh = NULL;
	int ret;

	if ((user = http_cgi_variable(list, "username")) == NULL)
	    t = urltrans_find_username(translations, "default");
	else 
	    t = urltrans_find_username(translations, octstr_get_cstr(user));
    
	if (t == NULL || 
	    (val = http_cgi_variable(list, "password")) == NULL ||
	    strcmp(octstr_get_cstr(val), urltrans_password(t)) != 0)
	{
	    return "Authorization failed";
	}

	udh = http_cgi_variable(list, "udh");
	text = http_cgi_variable(list, "text");

	if ((to = http_cgi_variable(list, "to")) == NULL ||
	    (text == NULL && udh == NULL))
	{
		error(0, "/cgi-bin/sendsms got wrong args");
		return "Wrong sendsms args.";
	}

	if (urltrans_faked_sender(t) != NULL) {
	    from = octstr_create(urltrans_faked_sender(t));
	} else if ((from = http_cgi_variable(list, "from")) != NULL &&
		   octstr_len(from) > 0) 
	{
	    from = octstr_duplicate(from);
	} else if (global_sender != NULL) {
	    from = octstr_create(global_sender);
	} else {
	    return "Sender missing and no global set";
	}
	info(0, "/cgi-bin/sendsms <%s:%s> <%s> <%s>",
	     user ? octstr_get_cstr(user) : "default",
	     octstr_get_cstr(from), octstr_get_cstr(to),
	     text ? octstr_get_cstr(text) : "<< UDH >>");
  
	msg = msg_create(smart_sms);
	if (msg == NULL) goto error;

	msg->smart_sms.receiver = octstr_duplicate(to);
	msg->smart_sms.sender = from;  	/* duplication */
	msg->smart_sms.msgdata = text ? octstr_duplicate(text) : octstr_create("");
	msg->smart_sms.udhdata = udh ? octstr_duplicate(udh) : octstr_create("");

	if(udh==NULL) {
		msg->smart_sms.flag_8bit = 0;
		msg->smart_sms.flag_udh  = 0;
	} else {
		msg->smart_sms.flag_8bit = 1;
		msg->smart_sms.flag_udh  = 1;
		octstr_dump(msg->smart_sms.udhdata, 0);
	}

	msg->smart_sms.time = time(NULL);
   
	/* send_message frees the 'msg' */
	ret = send_message(t, msg);

    if (ret == -1)
	goto error;

    return "Sent.";
    
error:
    error(0, "sendsms_request: failed");
    octstr_destroy(from);
    return "Sending failed.";
}






