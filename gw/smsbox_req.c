
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

#include "gwlib.h"
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
		replytext[len-1] = '\0';	/* remove trailing '\n' */

		return gw_strdup(replytext);
	}

	/* URL */

	debug(0, "formatted url: <%s>", pattern);

	if (http_get(pattern, &type, &data, &size) == -1) {
		gw_free(pattern);
		goto error;
	}
	gw_free(pattern);		/* no longer needed */
	
	/* Make sure the data is NUL terminated. */
	tmpdata = gw_realloc(data, size + 1);
	data = tmpdata;
	data[size] = '\0';

	if(strcmp(type, "text/html") == 0) {

		if (urltrans_prefix(trans) != NULL &&
		    urltrans_suffix(trans) != NULL) {

		    tmpdata = html_strip_prefix_and_suffix(data,
			       urltrans_prefix(trans), urltrans_suffix(trans));
		    gw_free(data);	
		    data = tmpdata;
		}
		html_to_sms(replytext, sizeof(replytext), data);

	} else if(strcmp(type, "text/plain") == 0) {

		strncpy(replytext, data, sizeof(replytext) - 1);

	} else {

		strcpy(replytext,
		       "Result could not be represented as an SMS message.");

	}

	gw_free(data);
	gw_free(type);

	if (strlen(replytext)==0)
		return gw_strdup("");

	return gw_strdup(replytext);

error:
    return NULL;
}


/*
 * sends the buf, with msg-info - does NO splitting etc. just the sending
 *
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
    error(0, "Failed");
    return -1;
}


/*
 * Take a Msg structure and send it as a MT SMS message.
 *
 * Return -1 on failure, 0 if Ok.
 */
static int do_split_send(Msg *msg, int maxmsgs, URLTranslation *trans)
{
    Msg *split;

    char *p, *suf, *sc;
    int slen = 0;
    int size, total_len, loc;
    char *h, *f;
    int fl, hl;

    h = urltrans_header(trans);
    f = urltrans_footer(trans);
    if (h != NULL) hl = strlen(h); else hl = 0;
    if (f != NULL) fl = strlen(f); else fl = 0;

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

	if (maxmsgs == 1 || total_len-loc < sms_max_length-fl-hl) {
	    slen = 0;
	    suf = NULL;
	    sc = NULL;
	}
	size = sms_max_length - slen -hl -fl;	/* leave room to special parts */
	/*
	 * if we use split chars, find the first from starting from
	 * the end of sms message and return partion _before_ that
	 */
	if (sc)
	    size = str_reverse_seek(p+loc, size-1, sc) + 1;

	/* do not accept a bit too small fractions... */
	if (size < sms_max_length/2)
	    size = sms_max_length - slen -hl -fl;

	if ((split = msg_duplicate(msg))==NULL)
	    goto error;

	
	if (h != NULL) {	/* add header and message */
	    octstr_replace(split->smart_sms.msgdata, h, hl);
	    octstr_insert_data(split->smart_sms.msgdata, hl, p+loc, size);    
	} else			/* just the message */
	    octstr_replace(split->smart_sms.msgdata, p+loc, size);
	
	if (suf != NULL)
	    octstr_insert_data(split->smart_sms.msgdata, size, suf, slen);
	
	if (f != NULL)	/* add footer */
	    octstr_insert_data(split->smart_sms.msgdata, size+hl, f, fl);

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


#if 0

	Msg *tmpmsg;

	int data_to_send = 0;
	int data_sent = 0;

	/* The size of a single message (in bytes.) 
	   7bit and 8bit sizes differ. */
	int hard_msg_size = 0;

	/* The size of the payload that can be carried by a specific
	   message depends on whether we are using headers, footers
	   and split suffixes etc. */
/*	int soft_payload_size; */

	int pos_inside_message = 0;
	
	int this_message_length = 0;
	int this_udh_length = 0;

	int orig_msg_length = 0;
	int orig_udh_length = 0;

	/* The hard limit of maximum messages to send. 
	   Set in the configuration file. */
	int max_messages_to_send = maxmsgs;
	int this_message_number = 0;
	int is_last_message = 0;

	char *h, *f, *suf, *sc;
	int flen, hlen, slen;

	char *ptr;

	char tmpdata[256];
	char tmpudh[256];

	/* Get the configuration values. */
	h = urltrans_header(trans);
	f = urltrans_footer(trans);
	suf = urltrans_split_suffix(trans);
	sc = urltrans_split_chars(trans);

	/* Verify the configuration values. */
	hlen = ( h == NULL ? 0 : strlen(h) );
	flen = ( f == NULL ? 0 : strlen(f) );
	slen = ( suf == NULL ? 0 : strlen(suf) );
	orig_msg_length = ( msg->smart_sms.msgdata == NULL ? 
		0 : octstr_len(msg->smart_sms.msgdata) );
	orig_udh_length = ( msg->smart_sms.udhdata == NULL ? 
		0 : octstr_len(msg->smart_sms.udhdata) );

	ptr = octstr_get_cstr(msg->smart_sms.msgdata);
	if(ptr==NULL) goto error;

	/* Let's see what is the hard max message size. */
	if(msg->smart_sms.flag_udh == 1) {

		/* The basic SMS datablock size is 140 bytes
		   of 8 bit binary data. */
		hard_msg_size = (160 * 7) / 8;
		this_udh_length = orig_udh_length;

		/* Check if we need to fragment the 
		   datagram to several SMS messages. */
		if( orig_msg_length + orig_udh_length > hard_msg_size ) {
			/* The size of the fragmentation UDH
			   data to add is 5 bytes. */
			this_udh_length += 5;
		}

		hard_msg_size -= this_udh_length;

		/* Note that we don't use footers and headers when
		   sending a UDH message. */

	} else {

		/* We might have to append a header and a footer to
		   each and every message we send in this transaction. */
		hard_msg_size = 160 - hlen - flen;
		this_udh_length = 0;

	}

	/* Cannot deduce what to do if something is very wrong. */
	if(hard_msg_size < 0) {
		error(0, "do_split_char: too long header and/or footer");
		goto error;
	}

	/* Just the data, not the UDHs. */
	data_to_send = orig_msg_length;

	while(data_sent < data_to_send) {

		/* How much actual data to send in this message? */
		this_message_length = (data_to_send - data_sent) < hard_msg_size ?
			(data_to_send - data_sent) : hard_msg_size;


		/* Check that we're not exceeding the maximum amount of
		   messages allowed. */
		is_last_message = 0;
		this_message_number++;
		if(this_message_number == max_messages_to_send) {
			is_last_message = 1;

		} else if(this_message_number > max_messages_to_send) {

			/* Can't send any more messages. Done. */
			break;

		} else {
			/* This might _still_ be the last message. */
			if(data_to_send-data_sent <= this_message_length) {
				is_last_message = 1;
			}
		}

		tmpmsg = msg_create(smart_sms);
		tmpmsg->smart_sms.sender = 
			octstr_duplicate(msg->smart_sms.sender);
		tmpmsg->smart_sms.receiver = 
			octstr_duplicate(msg->smart_sms.receiver);
		tmpmsg->smart_sms.flag_udh = msg->smart_sms.flag_udh;
		tmpmsg->smart_sms.flag_8bit = msg->smart_sms.flag_8bit;

		/* Add a suffix if there is enough data to create a new
		   message after this one. */
		if( (suf != NULL) && (is_last_message != 1) )
			slen = strlen(suf);
		else
			slen = 0;

		if(msg->smart_sms.flag_udh == 1) {

			octstr_get_many_chars(tmpdata, 
				msg->smart_sms.msgdata,
				data_sent, this_message_length );

			/* Modify the existing UDHs to add the
			   fragmentation data. */
			octstr_get_many_chars(tmpudh,
				msg->smart_sms.udhdata, 
				0, this_udh_length );

			pos_inside_message = octstr_len(msg->smart_sms.udhdata);
/*
			tmpudh[0] = this_udh_length;
			tmpudh[pos_inside_message] = 
*/
		} else {

			pos_inside_message = 0;

			/* See if we need to find a user configured split
			   character and cut from there. */
			if(sc != NULL) {
				this_message_length =
					str_reverse_seek(ptr+data_sent,
						this_message_length-1, sc);
			}

			/* Add possible header. */
			if(h != NULL) {
				strcpy(tmpdata, h);
				pos_inside_message += strlen(h);
			}

			/* Copy message body. */
			strncpy(tmpdata + pos_inside_message,
				ptr + data_sent, this_message_length - slen);

			pos_inside_message += this_message_length - slen;

			/* Add a split suffix (typically "...") to a message
			   which is followed by another.. */
			if( slen > 0 ) {
				strcpy(tmpdata + pos_inside_message, suf);
				pos_inside_message += strlen(suf);
			}

			/* Add possible footer. */
			if(f != NULL) {
				strcpy(tmpdata + pos_inside_message, f);
				pos_inside_message += strlen(f);
			}

		}

		tmpmsg->smart_sms.msgdata = 
			octstr_create_from_data(tmpdata, 
				this_message_length + hlen + flen + slen);

		tmpmsg->smart_sms.udhdata = 
			octstr_create_from_data(tmpudh, this_udh_length);

		/* Actually send the message. */
		if( do_sending(tmpmsg) < 0 ) goto error;

		/* The tmpmsg is freed by the function called by do_sending. */

		data_sent += this_message_length - slen;
	}

	/* This function must destroy the original message. */
	msg_destroy(msg);

#endif    



/*
 * send the 'reply', according to settings in 'trans' and 'msg'
 *
 * return -1 if failed utterly, 0 otherwise
 */
static int send_message(URLTranslation *trans, Msg *msg)
{
    int max_msgs;
    int hl, fl;
    char *h, *f;
    static char *empty = "<Empty reply from service provider>";
    
    max_msgs = urltrans_max_messages(trans);

    if(msg_type(msg) != smart_sms) {
	error(0, "Weird messagetype for send_message!");
	goto error;
    }

    if (octstr_len(msg->smart_sms.msgdata)==0) {
	if (urltrans_omit_empty(trans) != 0) {
	    max_msgs = 0;
	} else { 
	    octstr_replace(msg->smart_sms.msgdata, empty, strlen(empty));
	}
    }

    if (max_msgs == 0) {
	info(0, "No reply sent, denied.");
	msg_destroy(msg);
	return 0;
    }

    h = urltrans_header(trans);
    f = urltrans_footer(trans);
    if (h != NULL) hl = strlen(h); else hl = 0;
    if (f != NULL) fl = strlen(f); else fl = 0;

    if (octstr_len(msg->smart_sms.msgdata) <= (sms_max_length - fl - hl)
	|| max_msgs == 1) {

	if (h != NULL)	/* if header set */
	    if (octstr_insert_data(msg->smart_sms.msgdata, 0, h, hl)== -1)
		goto error;
	/*
	 * truncate if the message is too long one (this only happens if
	 *  max_msgs == 1)
	 */
	if (octstr_len(msg->smart_sms.msgdata)+fl > sms_max_length)
	    octstr_truncate(msg->smart_sms.msgdata, sms_max_length - fl);
	    
	if (f != NULL)	/* if footer set */
	    if (octstr_insert_data(msg->smart_sms.msgdata,
				   octstr_len(msg->smart_sms.msgdata), f, fl)== -1)
		goto error;

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
    msg_destroy(msg);
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
	sender = send;
	if (global != NULL) {
		global_sender = gw_strdup(global);
		if (global_sender == NULL)
		    return -1;
	}
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

    msg->smart_sms.time = time(NULL);	/* set current time */

	/* send_message frees the 'msg' */
    if(send_message(trans, msg) < 0)
		error(0, "request_thread: failed");
    
    gw_free(reply);
    req_threads--;
    return NULL;
error:
    error(0, "Request_thread: failed");
    msg_destroy(msg);
    gw_free(reply);
    req_threads--;
    return NULL;
}


char *smsbox_req_sendsms(CGIArg *list)
{
	Msg *msg = NULL;
	URLTranslation *t = NULL;
	char *user = NULL, *val, *from, *to, *text;
	char *udh = NULL;
	int ret;

	if (cgiarg_get(list, "username", &user) == -1)
		t = urltrans_find_username(translations, "default");
	else 
		t = urltrans_find_username(translations, user);
    
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
    
	info(0, "/cgi-bin/sendsms <%s:%s> <%s> <%s>", user ? user : "default",
	     from, to, text);
  
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

