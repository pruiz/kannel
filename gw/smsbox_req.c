/*
 * smsbox_req.c - fulfill sms requests from users
 *
 * this module handles the request handling - that is, finding
 * the correct urltranslation, fetching the result and then
 * splitting it into several messages if needed to
 */

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
#include "msg.h"

#include "smsbox_req.h"
#include "urltrans.h"
#include "config.h"

#ifdef HAVE_SECURITY_PAM_APPL_H
#include <security/pam_appl.h>
#endif


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
static void (*sender) (Msg *msg) = NULL;
static Config 	*cfg = NULL;

#define SENDSMS_DEFAULT_CHARS "0123456789 +-"


List *smsbox_requests = NULL;


/*-------------------------------------------------------------------*
 * STATIC FUNCTIONS
 */

static int send_message(URLTranslation *trans, Msg *msg);

static HTTPCaller *caller;
static Dict *receivers;

struct receiver {
    Msg *msg;
    URLTranslation *trans;
};

static void destroy_receiver(void *p)
{
    struct receiver *r;
    
    r = p;
    msg_destroy(r->msg);
    gw_free(r);
}

static void remember_receiver(long id, Msg *msg, URLTranslation *trans)
{
    Octstr *idstr;
    struct receiver *receiver;
    
    receiver = gw_malloc(sizeof(*receiver));

    receiver->msg = msg_create(sms);
    receiver->msg->sms.sender = octstr_duplicate(msg->sms.sender);
    receiver->msg->sms.receiver = octstr_duplicate(msg->sms.receiver);
    receiver->msg->sms.flag_8bit = 0;
    receiver->msg->sms.flag_udh = 0;
    receiver->msg->sms.udhdata = NULL;
    receiver->msg->sms.msgdata = NULL;
    receiver->msg->sms.time = (time_t) -1;
    receiver->msg->sms.smsc_id = octstr_duplicate(msg->sms.smsc_id);
    
    receiver->trans = trans;

    idstr = octstr_format("%ld", id);
    dict_put(receivers, idstr, receiver);
    octstr_destroy(idstr);
}


static void get_receiver(long id, Msg **msg, URLTranslation **trans)
{
    Octstr *idstr;
    struct receiver *receiver;
    
    idstr = octstr_format("%ld", id);
    receiver = dict_remove(receivers, idstr);
    octstr_destroy(idstr);
    *msg = receiver->msg;
    *trans = receiver->trans;
    gw_free(receiver);
}


static void url_result_thread(void *arg)
{
    Octstr *final_url, *reply_body, *type, *charset, *temp, *replytext;
    List *reply_headers;
    int status;
    long id;
    Msg *msg;
    URLTranslation *trans;

    for (;;) {
    	id = http_receive_result(caller, &status, &final_url, &reply_headers,
	    	    	    	 &reply_body);
    	if (id == -1)
	    break;
    	
    	get_receiver(id, &msg, &trans);

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
	http_destroy_headers(reply_headers);
	octstr_destroy(reply_body);
    
	octstr_strip_blanks(replytext);
	
	msg->sms.msgdata = replytext;
	msg->sms.time = time(NULL);	/* set current time */
    
	alog("SMS HTTP-request sender:%s request: '%s' url: '%s' reply: %d '%s'",
	     octstr_get_cstr(msg->sms.receiver),
	     octstr_get_cstr(msg->sms.msgdata),
	     octstr_get_cstr(final_url), status,
	     (status == 200) ? "<< successful >>"
	     : (reply_body != NULL) ? octstr_get_cstr(reply_body) : "");
		
	/* send_message frees the 'msg' */
	if (send_message(trans, msg) < 0)
	    error(0, "request_thread: failed");
    }
}


/*
 * Perform the service requested by the user: translate the request into
 * a pattern, if it is an URL, start its fetch and return 0, otherwise
 * return the string in `*result' and return 1. Return -1 for errors,
 */
static int obey_request(Octstr **result, URLTranslation *trans, Msg *msg)
{
    char *pattern;
    Octstr *url;
    List *request_headers;
    long id;
    
    gw_assert(msg != NULL);
    gw_assert(msg_type(msg) == sms);
    
    pattern = urltrans_get_pattern(trans, msg);
    gw_assert(pattern != NULL);
    
    switch (urltrans_type(trans)) {
    case TRANSTYPE_TEXT:
	debug("sms", 0, "formatted text answer: <%s>", pattern);
	*result = octstr_create(pattern);
	alog("SMS request sender:%s request: '%s' fixed answer: '%s'",
	     octstr_get_cstr(msg->sms.receiver),
	     octstr_get_cstr(msg->sms.msgdata),
	     pattern);
	break;
    
    case TRANSTYPE_FILE:
	*result = octstr_read_file(pattern);
	gw_free(pattern);
	alog("SMS request sender:%s request: '%s' file answer: '%s'",
	     octstr_get_cstr(msg->sms.receiver),
	     octstr_get_cstr(msg->sms.msgdata),
	     octstr_get_cstr(*result));
	break;
    
    case TRANSTYPE_URL:
	url = octstr_create(pattern);
	gw_free(pattern);
	request_headers = list_create();
	id = http_start_request(caller, url, request_headers, NULL, 1);
	octstr_destroy(url);
	http_destroy_headers(request_headers);
	if (id == -1)
	    goto error;
	remember_receiver(id, msg, trans);
	*result = NULL;
	return 0;
    
    default:
	error(0, "Unknown URL translation type %d", urltrans_type(trans));
	alog("SMS request sender:%s request: '%s' FAILED unknown translation",
	     octstr_get_cstr(msg->sms.receiver),
	     octstr_get_cstr(msg->sms.msgdata));
	goto error;
    }
    
    return 1;
    
error:
    return -1;
}


/*
 * sends the buf, with msg-info - does NO splitting etc. just the sending
 * NOTE: the sender gw_frees the message!
 */
static void do_sending(Msg *msg)
{
    /* sender does the freeing (or uses msg as it sees fit) */
    sender(msg);

    debug("smsbox_req", 0, "message sent\n");
}



/*
 * Take a Msg structure and send it as a MT SMS message.
 * Parameters: msg: message to send, maxmsgs: limit to the number of parts the
 *     message can be split into, h: header, hl: header length, f: footer,
 *     fl: footer length.
 */
static void do_split_send(Msg *msg, int maxmsgs, int maxdatalength, 
    	    	    	  URLTranslation *trans, char *h, int hl, char *f, 
			  int fl)
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
    /* The concatenation adds some information in the UDH so the maximum 
     * length of the data goes down */
    if (concat) {
	if (msg->sms.flag_8bit) {
	    maxdatalength -= CONCAT_IEL;
	} else {
	    /* in 7bit mode it is easier to remove the length of the UDH and
	     * calculate it again */
	    maxdatalength += roundup_div(octstr_len(msg->sms.udhdata)*8, 7);
	    maxdatalength -= roundup_div(
				  (CONCAT_IEL + 
				   octstr_len(msg->sms.udhdata)) * 8, 7);
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
    
    total_len = octstr_len(msg->sms.msgdata);
    
    /* number of messages that will be needed 
     * The value is rounded up */
    msgcount = roundup_div(total_len, maxdatalength);
    
    /* Go through the full message and send it in parts. The maximum number
     * of messages is respected even if the message has not been completely 
     sent. */
    p = octstr_get_cstr(msg->sms.msgdata);
    for(pos = 0; maxmsgs > 0 && pos < total_len; maxmsgs--) {
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
    
	if (h != NULL) {	/* add header and message */
	    octstr_replace(split->sms.msgdata, h, hl);
	    octstr_insert_data(split->sms.msgdata, hl, p+pos, size);    
	} else			/* just the message */
	    octstr_replace(split->sms.msgdata, p+pos, size);
    
	if (suf != NULL)
	    octstr_insert_data(split->sms.msgdata, size+hl, suf, suflen);
	
	if (f != NULL)	/* add footer */
	    octstr_insert_data(split->sms.msgdata, size+hl+suflen, f, fl);
	
	/* for concatenated messages add the UDH Element */
	if (concat == 1) {
	    /* Add the UDH with the concatenation information */
	    octstr_append_char(split->sms.udhdata, CONCAT_IEI); /* IEI */
	    octstr_append_char(split->sms.udhdata, 3); /* IEI Length = 3 octets */
	    octstr_append_char(split->sms.udhdata, msgref); /* ref */
	    octstr_append_char(split->sms.udhdata, msgcount); /* total nbr of msg */
	    octstr_append_char(split->sms.udhdata, msgseq); /* msg sequence */
	    split->sms.flag_udh = 1;
	}
	
	do_sending(split);
	pos += size;
	
	msgseq++; /* sequence number for the next message */
    }
    msg_destroy(msg); /* we must delete it as it is supposed to be deleted */
    
    /* Increment the message reference. It is an unsigned value so it 
     * will wrap. */
    msgref++;
}



/*
 * send message (or messages) according to data in *msg
 */
static int send_sms(URLTranslation *trans, Msg *msg, int max_msgs)
{
    int hl = 0, fl = 0;
    char *h, *f;
    int maxdatalength;
    
    if (trans != NULL) {
	h = urltrans_header(trans);
	f = urltrans_footer(trans);
    } else {
	h = f = NULL;
    }
    if (h != NULL)
    	hl = strlen(h);
    else hl = 0;
    if (f != NULL)
    	fl = strlen(f); 
    else
    	fl = 0;
    
    /* maximum length of the data in the SMS */
    maxdatalength = sms_max_length;
    if (maxdatalength < 0) {
	/* If the maximum length of the SMS data hasn't been set in the 
	 * config file, set it to the maximum length depending on the 
	 * 7bit or 8bit settings. */ 
	maxdatalength = (msg->sms.flag_8bit != 0) 
	    	    	? MAX8BITLENGTH 
			: MAX7BITLENGTH;
    }
    if (maxdatalength == 0) {	/* Don't send a message is maxdatalength is 0 ! */
	return -1;
    }
    
    if (msg->sms.flag_8bit) {		/* 8 bit */
	if (maxdatalength > MAX8BITLENGTH) {
	    maxdatalength = MAX8BITLENGTH;
	}
	if (msg->sms.flag_udh) {
	    maxdatalength -= octstr_len(msg->sms.udhdata);
	}
    } else {				/* 7 bit */
	if (maxdatalength > MAX7BITLENGTH) {
	    maxdatalength = MAX7BITLENGTH;
	}
	if (msg->sms.flag_udh) {
	/* the length is in 7bit characters! +1 for the length of the UDH. */
	maxdatalength -= roundup_div(octstr_len(msg->sms.udhdata)*8, 7) + 1;
	}
    }
    
    if (octstr_len(msg->sms.msgdata) <= (maxdatalength - fl - hl)
	|| max_msgs == 1) { 
	if (h != NULL)	/* if header set */
	    octstr_insert_data(msg->sms.msgdata, 0, h, hl);
    
	/* truncate if the message is too long (this only happens if
	 *  max_msgs == 1) */
    
	if (octstr_len(msg->sms.msgdata)+fl > sms_max_length)
	    octstr_truncate(msg->sms.msgdata, sms_max_length - fl);
    
	if (f != NULL)	/* if footer set */
	    octstr_insert_data(msg->sms.msgdata, 
	    	    	       octstr_len(msg->sms.msgdata), f, fl);
    
	do_sending(msg);
	return 0;
    } else {
	/*
	 * we have a message that is longer than what fits in one
	 * SMS message and we are allowed to split it
	 */
	do_split_send(msg, max_msgs, maxdatalength, trans, h, hl, f, fl);
	return 0;
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
    
    if (msg_type(msg) != sms) {
	error(0, "Weird message type for send_message!");
	msg_destroy(msg);
	return -1;
    }
    
    if (max_msgs == 0) {
	info(0, "No reply sent, denied.");
	msg_destroy(msg);
	return 0;
    }
    
    if (msg->sms.flag_udh == 0 && octstr_len(msg->sms.msgdata) == 0) {
	if (trans != NULL && urltrans_omit_empty(trans) != 0) {
	    max_msgs = 0;
	} else { 
	    octstr_replace(msg->sms.msgdata, empty, 
	    strlen(empty));
	}
    }
    if (max_msgs > 0)
	return send_sms(trans, msg, max_msgs);
    return 0;
}

/* Function that test the authentification via Plugable authentification module*/
#ifdef HAVE_SECURITY_PAM_APPL_H /*Module for pam authentication */

typedef const struct pam_message pam_message_type;

static const char *PAM_username;
static const char *PAM_password;

static int PAM_conv (int num_msg, pam_message_type **msg,
	  struct pam_response **resp,
	  void *appdata_ptr)
{
  int             count = 0, replies = 0;
  struct pam_response *repl = NULL;
  int             size = sizeof(struct pam_response);

#define GET_MEM \
	if (!(repl = (gw_realloc(repl, size)))) \
  		return PAM_CONV_ERR; \
	size += sizeof(struct pam_response)
#define COPY_STRING(s) (s) ? gw_strdup(s) : NULL

  for (count = 0; count < num_msg; count++) {
    switch (msg[count]->msg_style) {
    case PAM_PROMPT_ECHO_ON:
      GET_MEM;
      repl[replies].resp_retcode = PAM_SUCCESS;
      repl[replies++].resp = COPY_STRING(PAM_username);
      /* PAM frees resp */
      break;
    case PAM_PROMPT_ECHO_OFF:
      GET_MEM;
      repl[replies].resp_retcode = PAM_SUCCESS;
      repl[replies++].resp = COPY_STRING(PAM_password);
      /* PAM frees resp */
      break;
    case PAM_TEXT_INFO:
      printf("unexpected message from PAM: %s\n",
	      msg[count]->msg);
      break;
    case PAM_ERROR_MSG:
    default:
      /* Must be an error of some sort... */
      printf("unexpected error from PAM: %s\n",
	     msg[count]->msg);
      gw_free(repl);
      return PAM_CONV_ERR;
    }
  }
  if (repl)
    *resp = repl;
  return PAM_SUCCESS;
}

static struct pam_conv PAM_conversation = {
  &PAM_conv,
  NULL
};


int authenticate(const char *login, const char *passwd)
{
  pam_handle_t	*pamh;
  int		pam_error;

  PAM_username = login;
  PAM_password = passwd;

  pam_error = pam_start("kannel", login, &PAM_conversation, &pamh);
  if (pam_error != PAM_SUCCESS
      || (pam_error = pam_authenticate(pamh, 0)) != PAM_SUCCESS) {
    pam_end(pamh, pam_error);
    return 0;
  }
  pam_end(pamh, PAM_SUCCESS);
  return 1;
}

/*
 * Check for matching username and password for requests.
 * Return an URLTranslation if successful NULL otherwise.
 */

int pam_authorise_user(List *list) {
  
  Octstr *val, *user = NULL;
  char *pwd, *login;
  int result;
  if ( (user=http_cgi_variable(list, "user"))==NULL  &&  (user=http_cgi_variable(list, "username") )==NULL )
    return 0;

  login =  octstr_get_cstr(user);
  
  if ( (val=http_cgi_variable(list, "password"))==NULL  &&  (val=http_cgi_variable(list, "pass"))==NULL )
    return 0;
  pwd   =  octstr_get_cstr(val);
  
  result=authenticate(login,pwd);
  
  return result;
}
#endif /* HAVE_SECURITY_PAM_APPL_H */

/*
 * Authentification whith the data base of kannel 
 * Check for matching username and password for requests.
 * Return an URLTranslation if successful NULL otherwise.
 */
static URLTranslation *default_authorise_user(List *list, char *client_ip) 
{
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

static URLTranslation *authorise_user(List *list, char *client_ip) 
{
#ifdef HAVE_SECURITY_PAM_APPL_H
    URLTranslation *t = urltrans_find_username(translations, "pam");
    
    if (t != NULL) {
	if (pam_authorise_user(list))
	    return t;
	else 
	    return NULL;
    } else
	return default_authorise_user(list,client_ip);
#else
    return default_authorise_user(list,client_ip);
#endif
}

/*----------------------------------------------------------------*
 * PUBLIC FUNCTIONS
 */

void smsbox_req_init(URLTranslationList *transls,
		    Config *config,
		    int sms_max,
		    char *global,
		    char *accept_str,
		    void (*send) (Msg *msg))
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
    
    caller = http_caller_create();
    smsbox_requests = list_create();
    list_add_producer(smsbox_requests);
    receivers = dict_create(1024, destroy_receiver);
    gwthread_create(smsbox_req_thread, NULL);
    gwthread_create(url_result_thread, NULL);
}

void smsbox_req_shutdown(void)
{
    list_remove_producer(smsbox_requests);
    gwthread_join_every(smsbox_req_thread);
    http_caller_signal_shutdown(caller);
    gwthread_join_every(url_result_thread);
    gw_assert(list_len(smsbox_requests) == 0);
    list_destroy(smsbox_requests, NULL);
    gw_free(global_sender);
    http_caller_destroy(caller);
    dict_destroy(receivers);
}

long smsbox_req_count(void)
{
    return 0; /* XXX should check number of pending http requests */
}

void smsbox_req_thread(void *arg) 
{
    Msg *msg;
    Octstr *tmp, *reply;
    URLTranslation *trans;
    char *p;
    
    while ((msg = list_consume(smsbox_requests)) != NULL) {
	if (octstr_len(msg->sms.sender) == 0 ||
	    octstr_len(msg->sms.receiver) == 0) 
	{
	    error(0, "smsbox_req_thread: no sender/receiver, dump follows:");
	    msg_dump(msg, 0);
		    /* NACK should be returned here if we use such 
		       things... future implementation! */
	    continue;
	}
    
	if (octstr_compare(msg->sms.sender, msg->sms.receiver) == 0) {
	    info(0, "NOTE: sender and receiver same number <%s>, ignoring!",
		 octstr_get_cstr(msg->sms.sender));
	    continue;
	}
    
	trans = urltrans_find(translations, msg->sms.msgdata, 
	    	    	      msg->sms.smsc_id);
	if (trans == NULL) {
	    Octstr *t;
	    warning(0, "No translation found for <%s> from <%s> to <%s>",
		    octstr_get_cstr(msg->sms.msgdata),
		    octstr_get_cstr(msg->sms.sender),
		    octstr_get_cstr(msg->sms.receiver));
	    t = msg->sms.sender;
	    msg->sms.sender = msg->sms.receiver;
	    msg->sms.receiver = t;
	    goto error;
	}
    
	info(0, "Starting to service <%s> from <%s> to <%s>",
	     octstr_get_cstr(msg->sms.msgdata),
	     octstr_get_cstr(msg->sms.sender),
	     octstr_get_cstr(msg->sms.receiver));
    
	/*
	 * now, we change the sender (receiver now 'cause we swap them later)
	 * if faked-sender or similar set. Note that we ignore if the 
	 * replacement fails.
	 */
	tmp = octstr_duplicate(msg->sms.sender);
	    
	p = urltrans_faked_sender(trans);
	if (p != NULL)
	    octstr_replace(msg->sms.sender, p, strlen(p));
	else if (global_sender != NULL)
	    octstr_replace(msg->sms.sender, global_sender, 
	    	    	   strlen(global_sender));
	else {
	    octstr_replace(msg->sms.sender, 
	    	    	   octstr_get_cstr(msg->sms.receiver),
			   octstr_len(msg->sms.receiver));
	}
	octstr_destroy(msg->sms.receiver);
	msg->sms.receiver = tmp;
    
	/* TODO: check if the sender is approved to use this service */
    
	switch (obey_request(&reply, trans, msg)) {
	case -1:
    error:
	    error(0, "request failed");
	    /* XXX this can be something different, according to 
	       urltranslation */
	    reply = octstr_create("Request failed");
	    trans = NULL;	/* do not use any special translation */
    	    break;
	    
	case 1:
	    octstr_destroy(msg->sms.msgdata);
	    msg->sms.msgdata = reply;
	
	    msg->sms.flag_8bit = 0;
	    msg->sms.flag_udh  = 0;
	    msg->sms.time = time(NULL);	/* set current time */
	
	    if (send_message(trans, msg) < 0)
		error(0, "request_thread: failed");
    	    break;
    	
	default:
	    msg_destroy(msg);
	}
    }
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
	(text == NULL && udh == NULL)) {
	error(0, "/cgi-bin/sendsms got wrong args");
	return "Wrong sendsms args, rejected";
    }
    
    /*
     * check if UDH length is legal, or otherwise discard the
     * message, to prevent intentional buffer overflow schemes
     */
    if (udh != NULL) {
	if (octstr_len(udh) != (octstr_get_char(udh, 0) + 1))
	    return "UDH field misformed, rejected";
    }
    
    if (strspn(octstr_get_cstr(to), sendsms_number_chars) < octstr_len(to)) {
	info(0,"Illegal characters in 'to' string ('%s') vs '%s'",
	     octstr_get_cstr(to), sendsms_number_chars);
	return "Garbage 'to' field, rejected.";
    }
    
    if (urltrans_faked_sender(t) != NULL) {
	from = octstr_create(urltrans_faked_sender(t));
    } else if ((from = http_cgi_variable(list, "from")) != NULL &&
	       octstr_len(from) > 0) {
	from = octstr_duplicate(from);
    } else if (global_sender != NULL) {
	from = octstr_create(global_sender);
    } else {
	return "Sender missing and no global set, rejected";
    }
    
    info(0, "/cgi-bin/sendsms <%s:%s> <%s> <%s>",
	 user ? octstr_get_cstr(user) : "default",
	 octstr_get_cstr(from), octstr_get_cstr(to),
	 text ? octstr_get_cstr(text) : "<< UDH >>");
    
    /*
     * XXX here we should validate and split the 'to' field
     *   to allow multi-cast. Waiting for octstr_split...
     */
    msg = msg_create(sms);
    
    msg->sms.receiver = octstr_duplicate(to);
    msg->sms.sender = octstr_duplicate(from);
    msg->sms.msgdata = text ? octstr_duplicate(text) : octstr_create("");
    msg->sms.udhdata = udh ? octstr_duplicate(udh) : octstr_create("");
    
    /* new smsc-id argument - we should check this one, if able,
       but that's advanced logics -- Kalle */
    
    if (urltrans_forced_smsc(t)) {
	msg->sms.smsc_id = octstr_create(urltrans_forced_smsc(t));
	if (smsc)
	    info(0, "send-sms request smsc id ignored, "
	    	    "as smsc id forced to %s",
		    urltrans_forced_smsc(t));
    } else if (smsc) {
	msg->sms.smsc_id = octstr_duplicate(smsc);
    } else if (urltrans_default_smsc(t)) {
	msg->sms.smsc_id = octstr_create(urltrans_default_smsc(t));
    } else
	msg->sms.smsc_id = NULL;
    
    if (udh==NULL) {
	msg->sms.flag_8bit = 0;
	msg->sms.flag_udh  = 0;
    } else {
	msg->sms.flag_8bit = 1;
	msg->sms.flag_udh  = 1;
	octstr_dump(msg->sms.udhdata, 0);
    }
    
    msg->sms.time = time(NULL);
    
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


/*****************************************************************************
 * Creates and sends an SMS OTA (auto configuration) message from an HTTP request
 * Args: list contains the CGI parameters
 * 
 * This will be changed later to use an XML compiler.
 */
char *smsbox_req_sendota(List *list, char *client_ip)
{
    char *url = NULL, *desc = NULL, *ipaddr = NULL, *phonenum = NULL;
    char *username = NULL, *passwd = NULL, *id = NULL;
    char *speed;
    int bearer = -1, calltype = -1;
    int connection = CONN_CONT, security = 0, authent = AUTH_NORMAL;
    ConfigGroup *grp;
    char *p;
    Msg *msg = NULL;
    URLTranslation *t = NULL;
    int ret;
    Octstr *phonenumber = NULL, *otaid = NULL;
    
    /* check the username and password */
    t = authorise_user(list, client_ip);
    if (t == NULL) {
	return "Authorization failed";
    }
    
    phonenumber = http_cgi_variable(list, "phonenumber");
    if(phonenumber == NULL) {
	error(0, "/cgi-bin/sendota needs a valid phone number.");
	return "Wrong sendota args.";
    }
    
    /* check if a otaconfig id has been given and decide which OTA
     * properties to be send to the client otherwise send the default */
    otaid = http_cgi_variable(list, "otaid");
    if (otaid != NULL)
	id = octstr_get_cstr(otaid);
    
    grp = config_find_first_group(cfg, "group", "otaconfig");
    while (otaid != NULL && grp != NULL) {
	p = config_get(grp, "ota-id");
	if (p!= NULL && strcasecmp(p, id) == 0)
	    goto found;
	grp = config_find_next_group(grp, "group", "otaconfig");
    }
    
    if (otaid != NULL) {
	error(0, "/cgi-bin/sendota can't find otaconfig with ota-id '%s'.", 
	      id);
	return "Missing otaconfig group.";
    }
    
found:
    if ((p = config_get(grp, "location")) != NULL)
	url = p;
    if ((p = config_get(grp, "service")) != NULL)
	desc = p;
    if ((p = config_get(grp, "ipaddress")) != NULL)
	ipaddr = p;
    if ((p = config_get(grp, "phonenumber")) != NULL)
	phonenum = p;
    if ((p = config_get(grp, "bearer")) != NULL)
	bearer = (strcasecmp(p, "data") == 0)? BEARER_DATA : -1;
    if ((p = config_get(grp, "calltype")) != NULL)
	calltype = (strcasecmp(p, "isdn") == 0)? CALL_ISDN : -1;
    speed = SPEED_9660;
    if ((p = config_get(grp, "speed")) != NULL) {
	if (strcasecmp(p, "14400") == 0)
	    speed = SPEED_14400;
    }
    /* connection mode and security */
    if ((p = config_get(grp, "connection")) != NULL)
	connection = (strcasecmp(p, "temp") == 0)? CONN_TEMP : CONN_CONT;
    if ((p = config_get(grp, "pppsecurity")) != NULL)
	security = (strcasecmp(p, "on") == 0)? 1 : 0;
    if (security == 1)
	connection = (connection == CONN_CONT)? CONN_SECCONT : CONN_SECTEMP;
    
    if ((p = config_get(grp, "authentication")) != NULL)
	authent = (strcasecmp(p, "secure") == 0)? AUTH_SECURE : AUTH_NORMAL;
    
    if ((p = config_get(grp, "login")) != NULL)
	username = p;
    if ((p = config_get(grp, "secret")) != NULL)
	passwd = p;
    
    msg = msg_create(sms);
    if (msg == NULL)
    	goto error;
    
    msg->sms.udhdata = octstr_create("");
    
    octstr_append_from_hex(msg->sms.udhdata, "0504C34FC002");
    
    msg->sms.msgdata = octstr_create("");
    /* header for the data part of the message */
    octstr_append_from_hex(msg->sms.msgdata, "010604039481EA0001");
    /* unknow field */
    octstr_append_from_hex(msg->sms.msgdata, "45C60601");
    /* bearer type */
    if (bearer != -1) {
	octstr_append_from_hex(msg->sms.msgdata, "8712");
	octstr_append_char(msg->sms.msgdata, bearer);
	octstr_append_from_hex(msg->sms.msgdata, ENDTAG);
    }
    /* IP address */
    if (ipaddr != NULL) {
	octstr_append_from_hex(msg->sms.msgdata , "87131103");
	octstr_append_cstr(msg->sms.msgdata, ipaddr);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* connection type */
    if (connection != -1) {
	octstr_append_from_hex(msg->sms.msgdata, "8714");
	octstr_append_char(msg->sms.msgdata, connection);
	octstr_append_from_hex(msg->sms.msgdata, ENDTAG);
    }
    /* phone number */
    if (phonenum != NULL) {
	octstr_append_from_hex(msg->sms.msgdata, "87211103");
	octstr_append_cstr(msg->sms.msgdata, phonenum);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* authentication */
    octstr_append_from_hex(msg->sms.msgdata, "8722");
    octstr_append_char(msg->sms.msgdata, authent);
    octstr_append_from_hex(msg->sms.msgdata, ENDTAG);
    /* user name */
    if (username != NULL) {
	octstr_append_from_hex(msg->sms.msgdata, "87231103");
	octstr_append_cstr(msg->sms.msgdata, username);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* password */
    if (passwd != NULL) {
	octstr_append_from_hex(msg->sms.msgdata, "87241103");
	octstr_append_cstr(msg->sms.msgdata, passwd);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* data call type */
    if (calltype != -1) {
	octstr_append_from_hex(msg->sms.msgdata, "8728");
	octstr_append_char(msg->sms.msgdata, calltype);
	octstr_append_from_hex(msg->sms.msgdata, ENDTAG);
    }
    /* speed */
    octstr_append_from_hex(msg->sms.msgdata, "8729");
    octstr_append_from_hex(msg->sms.msgdata, speed);
    octstr_append_from_hex(msg->sms.msgdata, ENDTAG);
    octstr_append_from_hex(msg->sms.msgdata, ENDTAG);
    /* homepage */
    if (url != NULL) {
	octstr_append_from_hex(msg->sms.msgdata, "86071103");
	octstr_append_cstr(msg->sms.msgdata, url);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* unknow field */
    octstr_append_from_hex(msg->sms.msgdata, "C60801");
    /* service description */
    if (desc != NULL) {
	octstr_append_from_hex(msg->sms.msgdata, "87151103");
	octstr_append_cstr(msg->sms.msgdata, desc);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* message footer */
    octstr_append_from_hex(msg->sms.msgdata, "0101");
    
    msg->sms.receiver = octstr_duplicate(phonenumber);
    /* msg->sms.sender = from; */	
    msg->sms.flag_8bit = 1;
    msg->sms.flag_udh  = 1;
    
    msg->sms.time = time(NULL);
    
    octstr_dump(msg->sms.msgdata, 0);
    
    info(0, "/cgi-bin/sendota <%s> <%s>", id, octstr_get_cstr(phonenumber));
    
    /* send_message frees the 'msg' */
    ret = send_message(t, msg); 
    
    if (ret == -1)
	goto error;
    
    return "Sent.";
    
error:
    error(0, "sendota_request: failed");
    /*octstr_destroy(from);*/
    return "Sending failed.";
}

