
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

/* Defines */
#define MAX8BITLENGTH	140
#define MAX7BITLENGTH	160

#define CONCAT_IEI	0
#define CONCAT_IEL	6

#define	CONN_TEMP	0x60
#define	CONN_CONT	0x61
#define	CONN_SECTEMP	0x62
#define	CONN_SECCONT	0x63
#define AUTH_NORMAL	0x70
#define AUTH_SECURE	0x71
#define BEARER_DATA	0x45
#define CALL_ISDN	0x73
#define SPEED_9660	"6B"
#define SPEED_14400	"6C"
#define ENDTAG		"01"

/* Global variables */

static URLTranslationList *translations = NULL;
static int sms_max_length = -1;		/* not initialized - never modify after 
                                         * smsbox_req_init! */
static char *sendsms_number_chars;
static char *global_sender = NULL;
static int (*sender) (Msg *msg) = NULL;
static Config 	*cfg = NULL;

static volatile sig_atomic_t req_threads = 0;

#define SENDSMS_DEFAULT_CHARS "0123456789 +-"


/*-------------------------------------------------------------------*
 * STATIC FUNCTIONS
 */

/* Rounds up the result of a division */
static int roundup_div(int a, int b)
{
	int t;
	
	t = a / b;
	if(t * b != a)
		t += 1;

	return t;
}


/* Perform the service requested by the user: translate the request into
 * a pattern, if it is an URL, fetch it, and return a string, which must
 * be free'ed by the caller
 */
static char *obey_request(URLTranslation *trans, Msg *msg)
{
	char *pattern, *ret;
	Octstr *url, *final_url, *reply_body, *type, *charset,
		*temp, *replytext;
	List *request_headers, *reply_headers;
	int status;

	gw_assert(msg != NULL);
	gw_assert(msg_type(msg) == smart_sms);

	pattern = urltrans_get_pattern(trans, msg);
	gw_assert(pattern != NULL);

	switch (urltrans_type(trans)) {
	case TRANSTYPE_TEXT:
		debug("sms", 0, "formatted text answer: <%s>", pattern);
		ret = pattern;
		alog("SMS request sender:%s request: '%s' fixed answer: '%s'",
		     octstr_get_cstr(msg->smart_sms.receiver),
		     octstr_get_cstr(msg->smart_sms.msgdata),
		     pattern);
		break;

	case TRANSTYPE_FILE:
		replytext = octstr_read_file(pattern);
		gw_free(pattern);
		ret = gw_strdup(octstr_get_cstr(replytext));
		octstr_destroy(replytext);
		alog("SMS request sender:%s request: '%s' file answer: '%s'",
		     octstr_get_cstr(msg->smart_sms.receiver),
		     octstr_get_cstr(msg->smart_sms.msgdata),
		     ret);
		break;

	case TRANSTYPE_URL:
		url = octstr_create(pattern);
		request_headers = list_create();
		status = http_get_real(url, request_headers, &final_url,
					&reply_headers, &reply_body);
		alog("SMS HTTP-request sender:%s request: '%s' url: '%s' reply: %d '%s'",
		     octstr_get_cstr(msg->smart_sms.receiver),
		     octstr_get_cstr(msg->smart_sms.msgdata),
		     pattern, status,
		     (status == 200) ? "<< successful >>"
		     : (reply_body != NULL) ? octstr_get_cstr(reply_body) : "");
		
		gw_free(pattern);		/* no longer needed */

		octstr_destroy(url);
		octstr_destroy(final_url);
		list_destroy(request_headers, NULL);
		if (status != HTTP_OK) {
		    list_destroy(reply_headers, octstr_destroy_item);
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
		list_destroy(reply_headers, octstr_destroy_item);
		octstr_destroy(reply_body);
	
		if (octstr_len(replytext) == 0)
			ret = gw_strdup("");
		else {
			octstr_strip_blanks(replytext);
			ret = gw_strdup(octstr_get_cstr(replytext));
		}
		octstr_destroy(replytext);
	
		break;

	default:
		error(0, "Unknown URL translation type %d", 
			urltrans_type(trans));
		alog("SMS request sender:%s request: '%s' FAILED unknown translation",
		     octstr_get_cstr(msg->smart_sms.receiver),
		     octstr_get_cstr(msg->smart_sms.msgdata));
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
    if (sender(msg) < 0)
	goto error;

    debug("smsbox_req", 0, "message sent\n");
    /* sender does the freeing (or uses msg as it sees fit) */

    return 0;
error:
    error(0, "Msg send failed");
    return -1;
}



/*
 * Take a Msg structure and send it as a MT SMS message.
 * Return -1 on failure, 0 if Ok.
 * Parameters: msg: message to send, maxmsgs: limit to the number of parts the
 *     message can be split into, h: header, hl: header length, f: footer,
 *     fl: footer length.
 */
static int do_split_send(Msg *msg, int maxmsgs, int maxdatalength, URLTranslation *trans,
			 char *h, int hl, char *f, int fl)
{
	Msg *split;

	char *p, *suf, *sc;
	int suflen = 0;
	int size, total_len, pos;

	int concat;
	int msgcount, msgseq = 1;
	static unsigned char msgref = 0;

	gw_assert(msg != NULL);
	gw_assert(maxmsgs > 1);
	gw_assert(hl >= 0);
	gw_assert(fl >= 0);

	if (trans != NULL)
	    concat = urltrans_concatenation(trans);
	else
	    concat = 0;
	/* The concatenation adds some information in the UDH so the maximum length
	 * of the data goes down */
	if(concat) {
		if(msg->smart_sms.flag_8bit) {
			maxdatalength -= CONCAT_IEL;
		} else {
			/* in 7bit mode it is easier to remove the length of the UDH and
			 * calculate it again */
			maxdatalength += roundup_div(octstr_len(msg->smart_sms.udhdata)*8, 7) + 1;
			maxdatalength -= roundup_div(
			    (CONCAT_IEL + octstr_len(msg->smart_sms.udhdata)) * 8, 7);
		}
	}
	
	if (trans != NULL) {
	        suf = urltrans_split_suffix(trans);
		if (suf != NULL) {
		        suflen = strlen(suf);
		}
		sc = urltrans_split_chars(trans);
	} else {
	    suf = NULL;
	    sc = NULL;
	}

	total_len = octstr_len(msg->smart_sms.msgdata) + octstr_len(msg->smart_sms.udhdata);

	/* number of messages that will be needed 
	 * The value is rounded up */
	msgcount = roundup_div(total_len, maxdatalength);

	/* Go through the full message and send it in parts. The maximum number
	 * of messages is respected even if the message has not been completely sent. */
	p = octstr_get_cstr(msg->smart_sms.msgdata);
	for(pos = 0; maxmsgs > 0 && pos < total_len; maxmsgs--)
	{
		if (total_len-pos < maxdatalength-fl-hl) { 	/* message ends */
			suflen = 0;
			suf = NULL;
			sc = NULL;	/* no split char on end of message! */
			size = total_len - pos;
		} else if (maxmsgs == 1) {			/* last part */
			suflen = 0;
			suf = NULL;
			sc = NULL;	/* no split char on end of message! */
			size = maxdatalength -hl -fl;
		} else {					/* other parts */
			size = maxdatalength - suflen -hl -fl;
		}

		/* Split chars are used to avoid cutting a word in the middle.
		 * The split will occur at the last occurence of the split char
		 * in the message part. */
		if (sc) {
			size = str_reverse_seek(p+pos, size-1, sc) + 1;

			/* Do not split if the resulting message is too small
			 * (if the last word is very long). */
			if (size < sms_max_length/2)
				size = maxdatalength - suflen -hl -fl;
		}

		/* Make a copy of the message then replace the data  with the 
		 * part that we are going to send in this SMS. This is easier
		 * than creating a new message with almost the same content. */
		split = msg_duplicate(msg);
		if(split==NULL)
			goto error;
	
		if (h != NULL) {	/* add header and message */
			octstr_replace(split->smart_sms.msgdata, h, hl);
			octstr_insert_data(split->smart_sms.msgdata, hl, p+pos, size);    
		} else			/* just the message */
			octstr_replace(split->smart_sms.msgdata, p+pos, size);
	
		if (suf != NULL)
			octstr_insert_data(split->smart_sms.msgdata, size+hl, suf, suflen);
		
		if (f != NULL)	/* add footer */
			octstr_insert_data(split->smart_sms.msgdata, size+hl+suflen, f, fl);

		/* for concatenated messages add the UDH Element */
		if(concat == 1)
		{
			/* Add the UDH with the concatenation information */
			octstr_append_char(split->smart_sms.udhdata, CONCAT_IEI); /* IEI */
			octstr_append_char(split->smart_sms.udhdata, 3); /* IEI Length = 3 octets */
			octstr_append_char(split->smart_sms.udhdata, msgref); /* ref */
			octstr_append_char(split->smart_sms.udhdata, msgcount); /* total nbr of msg */
			octstr_append_char(split->smart_sms.udhdata, msgseq); /* msg sequence */
			split->smart_sms.flag_udh = 1;
		}
		
		if (do_sending(split) < 0) {
			msg_destroy(msg);
			return -1;
		}
		pos += size;

		msgseq++; /* sequence number for the next message */
	}
	msg_destroy(msg); /* we must delete it as it is supposed to be deleted */

	/* Increment the message reference. It is an unsigned value so it will wrap. */
	msgref++;
	
	return 0;

error:
	msg_destroy(msg); /* we must delete it as it is supposed to be deleted */
	return -1;
}



/*
 * send message (or messages) according to data in *msg
 */
static int send_sms(URLTranslation *trans, Msg *msg, int max_msgs)
{
	int hl=0, fl=0;
	char *h, *f;
	int maxdatalength;

	if (trans != NULL) {
	    h = urltrans_header(trans);
	    f = urltrans_footer(trans);
	} else {
	    h = f = NULL;
	}
	if (h != NULL) hl = strlen(h); else hl = 0;
	if (f != NULL) fl = strlen(f); else fl = 0;

	/* maximum length of the data in the SMS */
	maxdatalength = sms_max_length;
	if(maxdatalength < 0) {
		/* If the maximum length of the SMS data hasn't been set in the 
		 * config file, set it to the maximum length depending on the 
		 * 7bit or 8bit settings. */ 
		maxdatalength = (msg->smart_sms.flag_8bit != 0) ? MAX8BITLENGTH : MAX7BITLENGTH;
	}
	if(maxdatalength == 0) {	/* Don't send a message is maxdatalength is 0 ! */
		return -1;
	}

	if(msg->smart_sms.flag_8bit) {		/* 8 bit */
		if(maxdatalength > MAX8BITLENGTH) {
			maxdatalength = MAX8BITLENGTH;
		}
		if(msg->smart_sms.flag_udh) {
    			maxdatalength -= octstr_len(msg->smart_sms.udhdata);
    		}
    	} else {				/* 7 bit */
		if(maxdatalength > MAX7BITLENGTH) {
			maxdatalength = MAX7BITLENGTH;
		}
		if(msg->smart_sms.flag_udh) {
			/* the length is in 7bit characters! +1 for the length of the UDH. */
    			maxdatalength -= roundup_div(octstr_len(msg->smart_sms.udhdata)*8, 7) + 1;
    		}
    	}
    	
	if (octstr_len(msg->smart_sms.msgdata) <= (maxdatalength - fl - hl)
	    || max_msgs == 1) { 

		if (h != NULL)	/* if header set */
			octstr_insert_data(msg->smart_sms.msgdata, 0, h, hl);
		
		/* truncate if the message is too long (this only happens if
	 	 *  max_msgs == 1) */

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
		return do_split_send(msg, max_msgs, maxdatalength, trans, h, hl, f, fl);
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

	gw_assert(msg != NULL);
	
	if (trans != NULL)
	    max_msgs = urltrans_max_messages(trans);
	else
	    max_msgs = 1;
	
	if(msg_type(msg) != smart_sms) {
		error(0, "Weird message type for send_message!");
		msg_destroy(msg);
		return -1;
    	}

	if (max_msgs == 0) {
		info(0, "No reply sent, denied.");
		msg_destroy(msg);
		return 0;
	}

	if((msg->smart_sms.flag_udh == 0) 
	   && (octstr_len(msg->smart_sms.msgdata)==0)) {
	        if (trans != NULL && urltrans_omit_empty(trans) != 0) {
			max_msgs = 0;
		} else { 
			octstr_replace(msg->smart_sms.msgdata, empty, 
				       strlen(empty));
		}
	}
	if (max_msgs > 0)
	    return send_sms(trans, msg, max_msgs);
}

/*
 * Check for matching username and password for requests.
 * Return an URLTranslation if successful NULL otherwise.
 */
static URLTranslation *authorise_user(List *list, char *client_ip) {
	URLTranslation *t = NULL;
	Octstr *val, *user = NULL;
	
	if ((user = http_cgi_variable(list, "username")) == NULL
	    && (user = http_cgi_variable(list, "user")) == NULL)
		t = urltrans_find_username(translations, "default");
	else 
		t = urltrans_find_username(translations, octstr_get_cstr(user));
    
	if (((val = http_cgi_variable(list, "password")) == NULL
	     && (val = http_cgi_variable(list, "pass")) == NULL)
	    || t == NULL ||
	    strcmp(octstr_get_cstr(val), urltrans_password(t)) != 0)
	{
		/* if the password is not correct, reset the translation. */
		t = NULL;
	}
	if (t) {
	    Octstr *ip = octstr_create(client_ip);

	    if (is_allowed_ip(urltrans_allow_ip(t),
			      urltrans_allow_ip(t), ip) == 0)
	    {
		warning(0, "Non-allowed connect tried by <%s> from <%s>, ignored",
			user ? octstr_get_cstr(user) : "default-user" ,
			client_ip);
		t = NULL;
	    }
	    octstr_destroy(ip);
	}
	return t;
}

/*----------------------------------------------------------------*
 * PUBLIC FUNCTIONS
 */

void smsbox_req_init(URLTranslationList *transls,
		    Config *config,
		    int sms_max,
		    char *global,
		    char *accept_str,
		    int (*send) (Msg *msg))
{
	translations = transls;
	cfg = config;
	sms_max_length = sms_max;
	sender = send;
	if (accept_str)
	    sendsms_number_chars = accept_str;
	else
	    sendsms_number_chars = SENDSMS_DEFAULT_CHARS;

	if (global != NULL)
		global_sender = gw_strdup(global);
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
    
    if (octstr_len(msg->smart_sms.sender) == 0 ||
	octstr_len(msg->smart_sms.receiver) == 0) 
    {

	error(0, "smsbox_req_thread: no sender/receiver, dump follows:");
	msg_dump(msg, 0);
		/* NACK should be returned here if we use such 
		   things... future implementation! */

	req_threads--;
	return;
    }

    if (octstr_compare(msg->smart_sms.sender, msg->smart_sms.receiver) == 0) {
	info(0, "NOTE: sender and receiver same number <%s>, ignoring!",
	     octstr_get_cstr(msg->smart_sms.sender));
	req_threads--;
	return;
    }

    trans = urltrans_find(translations, msg->smart_sms.msgdata,
			  msg->smart_sms.smsc_id);
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
error:
	error(0, "request failed");
	/* XXX this can be something different, according to urltranslation */
	reply = gw_strdup("Request failed");
	trans = NULL;	/* do not use any special translation */
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

}

/*****************************************************************************
 * Creates and sends an SMS message from an HTTP request
 * Args: list contains the CGI parameters
 */
char *smsbox_req_sendsms(List *list, char *client_ip)
{
	Msg *msg = NULL;
	URLTranslation *t = NULL;
	Octstr *user = NULL, *from = NULL, *to;
	Octstr *text = NULL, *udh = NULL, *smsc = NULL;
	int ret;

	/* check the username and password */
	t = authorise_user(list, client_ip);
	if (t == NULL) {
		return "Authorization failed";
	}

	udh = http_cgi_variable(list, "udh");
	text = http_cgi_variable(list, "text");
	smsc = http_cgi_variable(list, "smsc");

	if ((to = http_cgi_variable(list, "to")) == NULL ||
	    (text == NULL && udh == NULL))
	{
		error(0, "/cgi-bin/sendsms got wrong args");
		return "Wrong sendsms args.";
	}

	if (strspn(octstr_get_cstr(to), sendsms_number_chars)
	    < octstr_len(to)) {

	    info(0,"Illegal characters in 'to' string ('%s') vs '%s'",
		 octstr_get_cstr(to), sendsms_number_chars);
	    return "Garbage 'to' field, rejected.";
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

	/*
	 * XXX here we should validate and split the 'to' field
	 *   to allow multi-cast. Waiting for octstr_split...
	 */
	msg = msg_create(smart_sms);

	msg->smart_sms.receiver = octstr_duplicate(to);
	msg->smart_sms.sender = octstr_duplicate(from);
	msg->smart_sms.msgdata = text ? octstr_duplicate(text) : octstr_create("");
	msg->smart_sms.udhdata = udh ? octstr_duplicate(udh) : octstr_create("");

	/* new smsc-id argument - we should check this one, if able,
	   but that's advanced logics -- Kalle */

	if (urltrans_forced_smsc(t)) {
	    msg->smart_sms.smsc_id = octstr_create(urltrans_forced_smsc(t));
	    if (smsc)
		info(0, "send-sms request smsc id ignored, as smsc id forced to %s",
		     urltrans_forced_smsc(t));
	} else if (smsc) {
	    msg->smart_sms.smsc_id = octstr_duplicate(smsc);
	} else if (urltrans_default_smsc(t)) {
	    msg->smart_sms.smsc_id = octstr_create(urltrans_default_smsc(t));
	} else
	    msg->smart_sms.smsc_id = NULL;
	    

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

	alog("send-SMS request added - sender:%s:%s %s target:%s request: '%s'",
	     user ? octstr_get_cstr(user) : "default",
	     octstr_get_cstr(from), client_ip,
	     octstr_get_cstr(to),
	     text ? octstr_get_cstr(text) : "<< UDH >>");
	octstr_destroy(from);

	return "Sent.";
    
error:
	error(0, "sendsms_request: failed");
	octstr_destroy(from);
	return "Sending failed.";
}
