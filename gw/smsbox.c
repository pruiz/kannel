/*
 * smsbox.c - main program of the smsbox
 */

#include <errno.h>
#include <unistd.h>
#include <string.h> /* XXX we should use octstr instead */
#include <signal.h>

#include "gwlib/gwlib.h"

#include "msg.h"
#include "bb.h"
#include "shared.h"
#include "heartbeat.h"
#include "html.h"
#include "urltrans.h"

#ifdef HAVE_SECURITY_PAM_APPL_H
#include <security/pam_appl.h>
#endif


/*
 * Maximum number of octets in an SMS message. Note that this is 8 bit
 * characters, not 7 bit characters.
 */
enum { MAX_SMS_OCTETS = 140 };


#define SENDSMS_DEFAULT_CHARS "0123456789 +-"


static Cfg *cfg;
static long bb_port;
static long sendsms_port = 0;
static Octstr *bb_host;
static char *pid_file;
static int heartbeat_freq;
static Octstr *accepted_chars = NULL;
static int only_try_http = 0;
static URLTranslationList *translations = NULL;
static long sms_max_length = MAX_SMS_OCTETS;
static char *sendsms_number_chars;
static Octstr *global_sender = NULL;

static List *smsbox_requests = NULL;


/***********************************************************************
 * Communication with the bearerbox.
 */


/*
 * Read an Msg from the bearerbox and send it to the proper receiver
 * via a List. At the moment all messages are sent to the smsbox_requests
 * List.
 */
static void read_messages_from_bearerbox(void)
{
    time_t start, t;
    int secs;
    int total = 0;
    Msg *msg;

    start = t = time(NULL);
    while (program_status != shutting_down) {
	msg = read_from_bearerbox();
	if (msg == NULL)
	    break;

        if (total == 0)
	    start = time(NULL);
	total++;

	if (msg_type(msg) != sms) {
	    warning(0, "Received other message than sms, ignoring!");
	    msg_destroy(msg);
	} else
	    list_produce(smsbox_requests, msg);
    }

    secs = difftime(time(NULL), start);
    info(0, "Received (and handled?) %d requests in %d seconds "
    	 "(%.2f per second)", total, secs, (float)total / secs);
}


/***********************************************************************
 * Send Msg to bearerbox for delivery to phone, possibly split it first.
 */


/*
 * Number of octets in the catenation UDH part.
 */
enum { CATENATE_UDH_LEN = 5 };


/*
 * Counter for catenated SMS messages. The counter that can be put into
 * the catenated SMS message's UDH headers is actually the lowest 8 bits.
 */
static Counter *catenated_sms_counter;


static void prepend_catenation_udh(Msg *sms, int part_no, int num_messages,
    	    	    	    	   int msg_sequence)
{
    gw_assert(sms->sms.udhdata != NULL);

    if (octstr_len(sms->sms.udhdata) == 0)
	octstr_append_char(sms->sms.udhdata, CATENATE_UDH_LEN);
    octstr_format_append(sms->sms.udhdata, "%c\3%c%c%c", 
    	    	    	 0, msg_sequence, num_messages, part_no);

    /* 
     * Now that we added the concatenation information the
     * length is all wrong. we need to recalculate it. 
     */
    octstr_set_char(sms->sms.udhdata, 0, octstr_len(sms->sms.udhdata) - 1 );
    
    sms->sms.flag_udh = 1;
}


static Octstr *extract_msgdata_part(Octstr *msgdata, Octstr *split_chars,
    	    	    	    	    int max_part_len)
{
    long i, len;
    Octstr *part;

    len = max_part_len;
    if (split_chars != NULL)
	for (i = max_part_len; i > 0; i--)
	    if (octstr_search_char(split_chars,
				   octstr_get_char(msgdata, i - 1), 0) != -1) {
		len = i;
		break;
	    }
    part = octstr_copy(msgdata, 0, len);
    octstr_delete(msgdata, 0, len);
    return part;
}


/*
 * 
 * Split an SMS message into smaller ones.
 * 
 * The original SMS message is represented as an Msg object, and the
 * resulting list of smaller ones is represented as a List of Msg objects.
 * A plain text header and/or footer can be added to each part, and an
 * additional suffix can be added to each part except the last one.
 * Optionally, a UDH prefix can be added to each part so that phones
 * that understand this prefix can join the messages into one large one
 * again. At most `max_messages' parts will be generated; surplus text
 * from the original message will be silently ignored.
 * 
 * If the original message has UDH, they will be duplicated in each part.
 * It is an error to use catenation and UDH together, or catenation and 7
 * bit mode toghether; in these cases, catenation is silently ignored.
 * 
 * If `catenate' is true, `msg_sequence' is used as the sequence number for
 * the logical message. The catenation UDH contain three numbers: the
 * concatenated message reference, which is constant for all parts of
 * the logical message, the total number of parts in the logical message,
 * and the sequence number of the current part.
 *
 * Note that `msg_sequence' must have a value in the range 0..255.
 * 
 * `max_octets' gives the maximum number of octets in on message, including
 * UDH, and after 7 bit characters have been packed into octets.
 */
static List *sms_split(Msg *orig, Octstr *header, Octstr *footer, 
		       Octstr *nonlast_suffix, Octstr *split_chars, 
		       int catenate, int msg_sequence, int max_messages,
		       int max_octets)
{
    long max_part_len, udh_len, hf_len, nlsuf_len;
    long total_messages, msgno, last;
    List *list;
    Msg *part, *partlist[256];
    Octstr *msgdata;

    hf_len = octstr_len(header) + octstr_len(footer);
    nlsuf_len = octstr_len(nonlast_suffix);
    if (orig->sms.flag_udh)
	udh_len = octstr_len(orig->sms.udhdata);
    else
	udh_len = 0;
    /* First check whether the message is under one-part maximum */
    if (orig->sms.flag_8bit)
	max_part_len = max_octets - udh_len - hf_len;
    else
	max_part_len = max_octets * 8 / 7 - (udh_len * 8 + 6) / 7 - hf_len;
    if (octstr_len(orig->sms.msgdata) > max_part_len && catenate) {
	/* Change part length to take concatenation overhead into account */
	if (udh_len == 0)
	    udh_len = 1;  /* To add the udh total length octet */
	udh_len += CATENATE_UDH_LEN;
	if (orig->sms.flag_8bit)
	    max_part_len = max_octets - udh_len - hf_len;
	else
	    max_part_len = max_octets * 8 / 7 - (udh_len * 8 + 6) / 7 - hf_len;
    }

    msgdata = octstr_duplicate(orig->sms.msgdata);
    msgno = 0;
    do {
	msgno++;
	part = msg_duplicate(orig);
	octstr_destroy(part->sms.msgdata);
	if (octstr_len(msgdata) <= max_part_len || msgno == max_messages) {
	    part->sms.msgdata = octstr_copy(msgdata, 0, max_part_len);
	    last = 1;
	}
	else {
	    part->sms.msgdata = extract_msgdata_part(msgdata, split_chars,
						     max_part_len - nlsuf_len);
	    last = 0;
	}
	if (header)
	    octstr_insert(part->sms.msgdata, header, 0);
	if (footer)
	    octstr_append(part->sms.msgdata, footer);
	if (!last && nonlast_suffix)
	    octstr_append(part->sms.msgdata, nonlast_suffix);
	partlist[msgno] = part;
    } while (!last);
    total_messages = msgno;
    octstr_destroy(msgdata);
    list = list_create();
    for (msgno = 1; msgno <= total_messages; msgno++) {
	part = partlist[msgno];
	if (catenate && total_messages > 1)
	    prepend_catenation_udh(part, msgno, total_messages, msg_sequence);
	list_append(list, part);
    }
    return list;
}


/*
 * Send a message to the bearerbox for delivery to a phone. Use
 * configuration from `trans' to format the message before sending.
 * Return 0 for success, -1 for failure.  Does not destroy the msg.
 */
static int send_message(URLTranslation *trans, Msg *msg)
{
    int max_msgs;
    Octstr *header, *footer, *suffix, *split_chars;
    int catenate, msg_sequence;
    List *list;
    Msg *part;
    static char *empty = "<Empty reply from service provider>";
    
    gw_assert(msg != NULL);
    gw_assert(msg_type(msg) == sms);
    
    if (trans != NULL)
	max_msgs = urltrans_max_messages(trans);
    else
	max_msgs = 1;
    
    if (max_msgs == 0) {
	info(0, "No reply sent, denied.");
	return 0;
    }
    
    /* Empty message?  Either ignore it or substitute the "empty"
     * warning defined above. */
    if (msg->sms.flag_udh == 0 && octstr_len(msg->sms.msgdata) == 0) {
	if (trans != NULL && urltrans_omit_empty(trans))
            return 0;
        else
	    msg->sms.msgdata = octstr_create(empty);
    }

    if (trans == NULL) {
	header = NULL;
	footer = NULL;
	suffix = NULL;
	split_chars = NULL;
	catenate = 0;
    } else {
    	header = urltrans_header(trans);
	footer = urltrans_footer(trans);
	suffix = urltrans_split_suffix(trans);
	split_chars = urltrans_split_chars(trans);
	catenate = urltrans_concatenation(trans);
    }

    if (catenate)
    	msg_sequence = counter_increase(catenated_sms_counter) & 0xFF;
    else
    	msg_sequence = 0;

    list = sms_split(msg, header, footer, suffix, split_chars, catenate,
    	    	     msg_sequence, max_msgs, sms_max_length);
    while ((part = list_extract_first(list)) != NULL)
	write_to_bearerbox(part);
    list_destroy(list, NULL);
    
    return 0;
}


/***********************************************************************
 * Stuff to remember which receiver belongs to which HTTP query.
 */


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


static long outstanding_requests(void)
{
    return dict_key_count(receivers);
}


/***********************************************************************
 * Thread for receiving reply from HTTP query and sending it to phone.
 */


static void strip_prefix_and_suffix(Octstr *html, Octstr *prefix, 
    	    	    	    	    Octstr *suffix)
{
    long prefix_end, suffix_start;

    if (prefix == NULL || suffix == NULL)
    	return;
    prefix_end = octstr_case_search(html, prefix, 0);
    if (prefix_end == -1)
        return;
    prefix_end += octstr_len(prefix);
    suffix_start = octstr_case_search(html, suffix, prefix_end);
    if (suffix_start == -1)
        return;
    octstr_delete(html, 0, prefix_end);
    octstr_truncate(html, suffix_start - prefix_end);
}


static void url_result_thread(void *arg)
{
    Octstr *final_url, *reply_body, *type, *charset, *replytext;
    List *reply_headers;
    int status;
    long id;
    Msg *msg;
    URLTranslation *trans;
    Octstr *text_html;
    Octstr *text_plain;
    Octstr *text_wml;

    text_html = octstr_imm("text/html");
    text_wml = octstr_imm("text/vnd.wap.wml");
    text_plain = octstr_imm("text/plain");

    for (;;) {
    	id = http_receive_result(caller, &status, &final_url, &reply_headers,
	    	    	    	 &reply_body);
    	if (id == -1)
	    break;
    	
    	get_receiver(id, &msg, &trans);

    	if (status == HTTP_OK) {
	    http_header_get_content_type(reply_headers, &type, &charset);
	    if (octstr_compare(type, text_html) == 0 ||
		octstr_compare(type, text_wml) == 0) {
		strip_prefix_and_suffix(reply_body,
					urltrans_prefix(trans), 
					urltrans_suffix(trans));
		replytext = html_to_sms(reply_body);
	    } else if (octstr_compare(type, text_plain) == 0) {
		replytext = reply_body;
		reply_body = NULL;
	    } else {
		replytext = octstr_create("Result could not be represented "
					  "as an SMS message.");
	    }
	    octstr_destroy(type);
	    octstr_destroy(charset);
	} else
	    replytext = octstr_create("Could not fetch content, sorry.");
    
	octstr_strip_blanks(replytext);
	msg->sms.msgdata = replytext;
	msg->sms.time = time(NULL);
    
    	if (final_url == NULL)
	    final_url = octstr_imm("");
    	if (reply_body == NULL)
	    reply_body = octstr_imm("");
	alog("SMS HTTP-request sender:%s request: '%s' "
	     "url: '%s' reply: %d '%s'",
	     octstr_get_cstr(msg->sms.receiver),
	     octstr_get_cstr(msg->sms.msgdata),
	     octstr_get_cstr(final_url),
	     status,
	     (status == HTTP_OK) 
		? "<< successful >>"
		: octstr_get_cstr(reply_body));
		
    	octstr_destroy(final_url);
	http_destroy_headers(reply_headers);
	octstr_destroy(reply_body);
    
	if (send_message(trans, msg) < 0)
	    error(0, "failed to send message to phone");
	msg_destroy(msg);
    }
}


/***********************************************************************
 * Thread to receive SMS messages from bearerbox and obeying the requests
 * in them. HTTP requests are started in the background (another thread
 * will deal with the replies) and other requests are fulfilled directly.
 */


/*
 * Perform the service requested by the user: translate the request into
 * a pattern, if it is an URL, start its fetch and return 0, otherwise
 * return the string in `*result' and return 1. Return -1 for errors,
 */
static int obey_request(Octstr **result, URLTranslation *trans, Msg *msg)
{
    Octstr *pattern;
    List *request_headers;
    long id;
    
    gw_assert(msg != NULL);
    gw_assert(msg_type(msg) == sms);
    
    pattern = urltrans_get_pattern(trans, msg);
    gw_assert(pattern != NULL);
    
    switch (urltrans_type(trans)) {
    case TRANSTYPE_TEXT:
	debug("sms", 0, "formatted text answer: <%s>", 
	      octstr_get_cstr(pattern));
	*result = pattern;
	alog("SMS request sender:%s request: '%s' fixed answer: '%s'",
	     octstr_get_cstr(msg->sms.receiver),
	     octstr_get_cstr(msg->sms.msgdata),
	     octstr_get_cstr(pattern));
	break;
    
    case TRANSTYPE_FILE:
	*result = octstr_read_file(octstr_get_cstr(pattern));
	octstr_destroy(pattern);
	alog("SMS request sender:%s request: '%s' file answer: '%s'",
	     octstr_get_cstr(msg->sms.receiver),
	     octstr_get_cstr(msg->sms.msgdata),
	     octstr_get_cstr(*result));
	break;
    
    case TRANSTYPE_URL:
	request_headers = list_create();
	id = http_start_request(caller, pattern, request_headers, NULL, 1);
	octstr_destroy(pattern);
	http_destroy_headers(request_headers);
	if (id == -1)
	    goto error;
	remember_receiver(id, msg, trans);
	*result = NULL;
	return 0;

    case TRANSTYPE_SENDSMS:
	error(0, "Got URL translation type SENDSMS for incoming message.");
	alog("SMS request sender:%s request: '%s' FAILED bad translation",
	     octstr_get_cstr(msg->sms.receiver),
	     octstr_get_cstr(msg->sms.msgdata));
	octstr_destroy(pattern);
	goto error;
    
    default:
	error(0, "Unknown URL translation type %d", urltrans_type(trans));
	alog("SMS request sender:%s request: '%s' FAILED unknown translation",
	     octstr_get_cstr(msg->sms.receiver),
	     octstr_get_cstr(msg->sms.msgdata));
	octstr_destroy(pattern);
	goto error;
    }
    
    return 1;
    
error:
    return -1;
}


static void obey_request_thread(void *arg) 
{
    Msg *msg, *reply_msg;
    Octstr *tmp, *reply;
    URLTranslation *trans;
    Octstr *p;
    int ret;
    
    while ((msg = list_consume(smsbox_requests)) != NULL) {
	if (octstr_len(msg->sms.sender) == 0 ||
	    octstr_len(msg->sms.receiver) == 0) {
	    error(0, "smsbox_req_thread: no sender/receiver, dump follows:");
	    msg_dump(msg, 0);
	    /* NACK should be returned here if we use such 
	       things... future implementation! */
	    msg_destroy(msg);
	    continue;
	}
    
	if (octstr_compare(msg->sms.sender, msg->sms.receiver) == 0) {
	    info(0, "NOTE: sender and receiver same number <%s>, ignoring!",
		 octstr_get_cstr(msg->sms.sender));
	    msg_destroy(msg);
	    continue;
	}
    
	/* create reply message to be sent afterwards */
	
	reply_msg = msg_create(ack);
	reply_msg->ack.time = msg->sms.time;
	reply_msg->ack.id = msg->sms.id;


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
	if (p != NULL) {
	    octstr_destroy(msg->sms.sender);
	    msg->sms.sender = p;
	} else if (global_sender != NULL) {
	    octstr_destroy(msg->sms.sender);
	    msg->sms.sender = octstr_duplicate(global_sender);
	} else {
	    octstr_destroy(msg->sms.sender);
	    msg->sms.sender = octstr_duplicate(msg->sms.receiver);
	}
	octstr_destroy(msg->sms.receiver);
	msg->sms.receiver = tmp;
	msg->sms.sms_type = mt_reply;
    
	/* TODO: check if the sender is approved to use this service */

	ret = obey_request(&reply, trans, msg);
	if (ret != 0) {
	    if (ret == -1) {
    error:
	        error(0, "request failed");
	        /* XXX this can be something different, according to 
	           urltranslation */
	        reply = octstr_create("Request failed");
	        trans = NULL;	/* do not use any special translation */
	    }
	    octstr_destroy(msg->sms.msgdata);
	    msg->sms.msgdata = reply;
	
	    msg->sms.flag_8bit = 0;
	    msg->sms.flag_udh  = 0;
	    msg->sms.time = time(NULL);	/* set current time */
	
	    if (send_message(trans, msg) < 0)
		error(0, "request_thread: failed");
	}
	write_to_bearerbox(reply_msg); /* implicit msg_destroy */

	msg_destroy(msg);
    }
}



/***********************************************************************
 * HTTP sendsms interface.
 */


#ifdef HAVE_SECURITY_PAM_APPL_H /*Module for pam authentication */

/*
 * Use PAM (Pluggable Authentication Module) to check sendsms authentication.
 */

typedef const struct pam_message pam_message_type;

static const char *PAM_username;
static const char *PAM_password;

static int PAM_conv (int num_msg, pam_message_type **msg,
		     struct pam_response **resp,
		     void *appdata_ptr)
{
    int count = 0, replies = 0;
    struct pam_response *repl = NULL;
    int size = sizeof(struct pam_response);

#define GET_MEM \
	repl = gw_realloc(repl, size); \
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
	    warning(0, "unexpected message from PAM: %s", msg[count]->msg);
	    break;

	case PAM_ERROR_MSG:
	default:
	    /* Must be an error of some sort... */
	    error(0, "unexpected error from PAM: %s", msg[count]->msg);
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


static int authenticate(const char *login, const char *passwd)
{
    pam_handle_t *pamh;
    int pam_error;
    
    PAM_username = login;
    PAM_password = passwd;
    
    pam_error = pam_start("kannel", login, &PAM_conversation, &pamh);
    if (pam_error != PAM_SUCCESS ||
        (pam_error = pam_authenticate(pamh, 0)) != PAM_SUCCESS) {
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

static int pam_authorise_user(List *list) 
{
    Octstr *val, *user = NULL;
    char *pwd, *login;
    int result;

    if ((user = http_cgi_variable(list, "user")) == NULL &&
        (user = http_cgi_variable(list, "username"))==NULL)
	return 0;
    login = octstr_get_cstr(user);
    
    if ((val = http_cgi_variable(list, "password")) == NULL &&
        (val = http_cgi_variable(list, "pass")) == NULL)
	return 0;

    pwd = octstr_get_cstr(val);
    result = authenticate(login, pwd);
    
    return result;
}

#endif /* HAVE_SECURITY_PAM_APPL_H */


/*
 * Authentication whith the database of Kannel.
 * Check for matching username and password for requests.
 * Return an URLTranslation if successful NULL otherwise.
 */
static URLTranslation *default_authorise_user(List *list, char *client_ip) 
{
    URLTranslation *t = NULL;
    Octstr *val, *user = NULL;

    if ((user = http_cgi_variable(list, "username")) == NULL
        && (user = http_cgi_variable(list, "user")) == NULL)
        t = urltrans_find_username(translations, 
	    	    	    	   octstr_imm("default"));
    else
        t = urltrans_find_username(translations, user);

    if (((val = http_cgi_variable(list, "password")) == NULL
         && (val = http_cgi_variable(list, "pass")) == NULL)
        || t == NULL ||
        octstr_compare(val, urltrans_password(t)) != 0)
    {
        /* if the password is not correct, reset the translation. */
        t = NULL;
    }
    if (t) {
        Octstr *ip = octstr_create(client_ip);
	Octstr *allow_ip = urltrans_allow_ip(t);
	Octstr *deny_ip = urltrans_deny_ip(t);
	
        if (is_allowed_ip(allow_ip, deny_ip, ip) == 0) {
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
    URLTranslation *t;
    
    t = urltrans_find_username(translations, octstr_imm("pam"));
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


/*
 * Create and send an SMS message from an HTTP request.
 * Args: list contains the CGI parameters
 */
static char *smsbox_req_sendsms(List *list, char *client_ip)
{
    Msg *msg = NULL;
    URLTranslation *t = NULL;
    Octstr *from = NULL, *to;
    Octstr *text = NULL, *udh = NULL, *smsc = NULL;
    int ret;
    
    /* check the username and password */
    t = authorise_user(list, client_ip);
    if (t == NULL) {
	return "Authorization failed for sendsms";
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
	from = octstr_duplicate(urltrans_faked_sender(t));
    } else if ((from = http_cgi_variable(list, "from")) != NULL &&
	       octstr_len(from) > 0) {
	from = octstr_duplicate(from);
    } else if (global_sender != NULL) {
	from = octstr_duplicate(global_sender);
    } else {
	return "Sender missing and no global set, rejected";
    }
    
    info(0, "/cgi-bin/sendsms sender:<%s:%s> (%s) to:<%s> msg:<%s>",
	 octstr_get_cstr(urltrans_username(t)),
	 octstr_get_cstr(from),
	 client_ip,
	 octstr_get_cstr(to),
	 udh == NULL ? octstr_get_cstr(text) : "<< UDH >>");
    
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
	msg->sms.smsc_id = octstr_duplicate(urltrans_forced_smsc(t));
	if (smsc)
	    info(0, "send-sms request smsc id ignored, "
	    	    "as smsc id forced to %s",
		    octstr_get_cstr(urltrans_forced_smsc(t)));
    } else if (smsc) {
	msg->sms.smsc_id = octstr_duplicate(smsc);
    } else if (urltrans_default_smsc(t)) {
	msg->sms.smsc_id = octstr_duplicate(urltrans_default_smsc(t));
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
    msg_destroy(msg);
    
    if (ret == -1)
	goto error;
    
    alog("send-SMS request added - sender:%s:%s %s target:%s request: '%s'",
	 urltrans_username(t), octstr_get_cstr(from), client_ip,
	 octstr_get_cstr(to),
	 udh == NULL ? octstr_get_cstr(text) : "<< UDH >>");

    octstr_destroy(from);
    return "Sent.";
    
error:
    error(0, "sendsms_request: failed");
    octstr_destroy(from);
    return "Sending failed.";
}


/*
 * Constants related to sendota.
 */

#define	CONN_TEMP	0x60
#define	CONN_CONT	0x61
#define	CONN_SECTEMP	0x62
#define	CONN_SECCONT	0x63
#define AUTH_NORMAL	0x70
#define AUTH_SECURE	0x71
#define BEARER_DATA	0x45
#define CALL_ISDN	0x73
#define SPEED_9600	"6B"
#define SPEED_14400	"6C"
#define ENDTAG		"01"

/*
 * Create and send an SMS OTA (auto configuration) message from an HTTP 
 * request.
 * Args: list contains the CGI parameters
 * 
 * This will be changed later to use an XML compiler.
 */
static char *smsbox_req_sendota(List *list, char *client_ip)
{
    Octstr *url, *desc, *ipaddr, *phonenum, *username, *passwd, *id, *from;
    char *speed;
    int bearer, calltype, connection, security, authent;
    CfgGroup *grp;
    List *grplist;
    Octstr *p;
    Msg *msg;
    URLTranslation *t;
    int ret;
    Octstr *phonenumber;
    
    url = NULL;
    desc = NULL;
    ipaddr = NULL;
    phonenum = NULL;
    username = NULL;
    passwd = NULL;
    id = NULL;
    bearer = -1;
    calltype = -1;
    connection = CONN_CONT;
    security = 0;
    authent = AUTH_NORMAL;
    phonenumber = NULL;

    /* check the username and password */
    t = authorise_user(list, client_ip);
    if (t == NULL)
	return "Authorization failed for sendota";
    
    phonenumber = http_cgi_variable(list, "phonenumber");
    if (phonenumber == NULL) {
	error(0, "/cgi-bin/sendota needs a valid phone number.");
	return "Wrong sendota args.";
    }

    if (urltrans_faked_sender(t) != NULL) {
	from = octstr_duplicate(urltrans_faked_sender(t));
    } else if ((from = http_cgi_variable(list, "from")) != NULL &&
	       octstr_len(from) > 0) {
	from = octstr_duplicate(from);
    } else if (global_sender != NULL) {
	from = octstr_duplicate(global_sender);
    } else {
	return "Sender missing and no global set, rejected";
    }

    /* check if a otaconfig id has been given and decide which OTA
     * properties to be send to the client otherwise send the default */
    id = http_cgi_variable(list, "otaid");
    
    grplist = cfg_get_multi_group(cfg, octstr_imm("otaconfig"));
    while (grplist && (grp = list_extract_first(grplist)) != NULL) {
	p = cfg_get(grp, octstr_imm("ota-id"));
	if (id == NULL || (p != NULL && octstr_compare(p, id) == 0))
	    goto found;
	octstr_destroy(p);
    }

    list_destroy(grplist, NULL);
    if (id != NULL)
	error(0, "/cgi-bin/sendota can't find otaconfig with ota-id '%s'.", 
	      octstr_get_cstr(id));
    else
	error(0, "/cgi-bin/sendota can't find any otaconfig group.");
    octstr_destroy(from);
    return "Missing otaconfig group.";

found:
    octstr_destroy(p);
    list_destroy(grplist, NULL);
    url = cfg_get(grp, octstr_imm("location"));
    desc = cfg_get(grp, octstr_imm("service"));
    ipaddr = cfg_get(grp, octstr_imm("ipaddress"));
    phonenum = cfg_get(grp, octstr_imm("phonenumber"));
    p = cfg_get(grp, octstr_imm("bearer"));
    if (p != NULL) {
	if (strcasecmp(octstr_get_cstr(p), "data") == 0)
	    bearer = BEARER_DATA;
	else
	    bearer = -1;
	octstr_destroy(p);
    }
    p = cfg_get(grp, octstr_imm("calltype"));
    if (p != NULL) {
	if (strcasecmp(octstr_get_cstr(p), "calltype") == 0)
	    calltype = CALL_ISDN;
	else
	    calltype = -1;
	octstr_destroy(p);
    }
	
    speed = SPEED_9600;
    p = cfg_get(grp, octstr_imm("speed"));
    if (p != NULL) {
	if (octstr_compare(p, octstr_imm("14400")) == 0)
	    speed = SPEED_14400;
	octstr_destroy(p);
    }

    /* connection mode and security */
    p = cfg_get(grp, octstr_imm("connection"));
    if (p != NULL) {
	if (strcasecmp(octstr_get_cstr(p), "temp") == 0)
	    connection = CONN_TEMP;
	else
	    connection = CONN_CONT;
	octstr_destroy(p);
    }

    p = cfg_get(grp, octstr_imm("pppsecurity"));
    if (p != NULL) {
	if (strcasecmp(octstr_get_cstr(p), "on") == 0)
	    security = 1;
	else
	    security = CONN_CONT;
	octstr_destroy(p);
    }
    if (security == 1)
	connection = (connection == CONN_CONT)? CONN_SECCONT : CONN_SECTEMP;
    
    p = cfg_get(grp, octstr_imm("authentication"));
    if (p != NULL) {
	if (strcasecmp(octstr_get_cstr(p), "secure") == 0)
	    authent = AUTH_SECURE;
	else
	    authent = AUTH_NORMAL;
	octstr_destroy(p);
    }
    
    username = cfg_get(grp, octstr_imm("login"));
    passwd = cfg_get(grp, octstr_imm("secret"));

    msg = msg_create(sms);
    
    msg->sms.udhdata = octstr_create("");

    /* UDH including the lenght (UDHL) */
    octstr_append_from_hex(msg->sms.udhdata, "060504C34FC002");
    
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
	octstr_append(msg->sms.msgdata, ipaddr);
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
	octstr_append(msg->sms.msgdata, phonenum);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* authentication */
    octstr_append_from_hex(msg->sms.msgdata, "8722");
    octstr_append_char(msg->sms.msgdata, authent);
    octstr_append_from_hex(msg->sms.msgdata, ENDTAG);
    /* user name */
    if (username != NULL) {
	octstr_append_from_hex(msg->sms.msgdata, "87231103");
	octstr_append(msg->sms.msgdata, username);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* password */
    if (passwd != NULL) {
	octstr_append_from_hex(msg->sms.msgdata, "87241103");
	octstr_append(msg->sms.msgdata, passwd);
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
	octstr_append(msg->sms.msgdata, url);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* unknow field */
    octstr_append_from_hex(msg->sms.msgdata, "C60801");
    /* service description */
    if (desc != NULL) {
	octstr_append_from_hex(msg->sms.msgdata, "87151103");
	octstr_append(msg->sms.msgdata, desc);
	octstr_append_from_hex(msg->sms.msgdata, "0001");
    }
    /* message footer */
    octstr_append_from_hex(msg->sms.msgdata, "0101");

    msg->sms.sender = from;
    msg->sms.receiver = octstr_duplicate(phonenumber);
    msg->sms.flag_8bit = 1;
    msg->sms.flag_udh  = 1;
    
    msg->sms.time = time(NULL);
    
    octstr_dump(msg->sms.msgdata, 0);
    
    info(0, "/cgi-bin/sendota <%s> <%s>", 
    	 id ? octstr_get_cstr(id) : "<default>", octstr_get_cstr(phonenumber));
    
    ret = send_message(t, msg); 
    msg_destroy(msg);

    octstr_destroy(url);
    octstr_destroy(desc);
    octstr_destroy(ipaddr);
    octstr_destroy(phonenum);
    octstr_destroy(username);
    octstr_destroy(passwd);

    if (ret == -1) {
	error(0, "sendota_request: failed");
	return "Sending failed.";
    }

    return "Sent.";
}


static void sendsms_thread(void *arg)
{
    HTTPClient *client;
    Octstr *ip, *url, *body, *answer;
    List *hdrs, *args, *reply_hdrs;

    reply_hdrs = http_create_empty_headers();
    http_header_add(reply_hdrs, "Content-type", "text/html");

    for (;;) {
    	client = http_accept_request(&ip, &url, &hdrs, &body, &args);
	if (client == NULL)
	    break;

	info(0, "smsbox: Got HTTP request <%s> from <%s>",
	    octstr_get_cstr(url), octstr_get_cstr(ip));

	if (octstr_str_compare(url, "/cgi-bin/sendsms") == 0)
	    answer = octstr_create(smsbox_req_sendsms(args, 
	    	    	    	    	    	      octstr_get_cstr(ip)));
	else if (octstr_str_compare(url, "/cgi-bin/sendota") == 0)
	    answer = octstr_create(smsbox_req_sendota(args, 
	    	    	    	    	    	      octstr_get_cstr(ip)));
	else
	    answer = octstr_create("Unknown request.\n");
        debug("sms.http", 0, "Answer: <%s>", octstr_get_cstr(answer));

	octstr_destroy(ip);
	octstr_destroy(url);
	http_destroy_headers(hdrs);
	octstr_destroy(body);
	http_destroy_cgiargs(args);
	
	http_send_reply(client, HTTP_OK, reply_hdrs, answer);

	octstr_destroy(answer);
    }

    http_destroy_headers(reply_hdrs);
}


/***********************************************************************
 * Main program. Configuration, signal handling, etc.
 */

static void write_pid_file(void) {
    FILE *f;
        
    if (pid_file != NULL) {
	f = fopen(pid_file, "w");
	fprintf(f, "%d\n", (int)getpid());
	fclose(f);
    }
}


static void signal_handler(int signum) {
    /* On some implementations (i.e. linuxthreads), signals are delivered
     * to all threads.  We only want to handle each signal once for the
     * entire box, and we let the gwthread wrapper take care of choosing
     * one.
     */
    if (!gwthread_shouldhandlesignal(signum))
        return;

    if (signum == SIGINT) {
	if (program_status != shutting_down) {
	    error(0, "SIGINT received, aborting program...");
	    program_status = shutting_down;
	}
    } else if (signum == SIGHUP) {
        warning(0, "SIGHUP received, catching and re-opening logs");
        log_reopen();
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



static void init_smsbox(Cfg *cfg)
{
    CfgGroup *grp;
    Octstr *logfile;
    Octstr *p;
    long lvl;
    Octstr *http_proxy_host = NULL;
    long http_proxy_port = -1;
    List *http_proxy_exceptions = NULL;
    Octstr *http_proxy_username = NULL;
    Octstr *http_proxy_password = NULL;


    bb_port = BB_DEFAULT_SMSBOX_PORT;
    bb_host = octstr_create(BB_DEFAULT_HOST);
    heartbeat_freq = BB_DEFAULT_HEARTBEAT;
    logfile = NULL;
    lvl = 0;

    /*
     * first we take the port number in bearerbox and other values from the
     * core group in configuration file
     */

    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    
    if (cfg_get_integer(&bb_port, grp, octstr_imm("smsbox-port")) == -1)
	panic(0, "Missing or bad 'smsbox-port' in core group");

    cfg_get_integer(&http_proxy_port, grp, octstr_imm("http-proxy-port"));

    http_proxy_host = cfg_get(grp, 
    	    	    	octstr_imm("http-proxy-host"));
    http_proxy_username = cfg_get(grp, 
    	    	    	    octstr_imm("http-proxy-username"));
    http_proxy_password = cfg_get(grp, 
    	    	    	    octstr_imm("http-proxy-password"));
    http_proxy_exceptions = cfg_get_list(grp,
    	    	    	    octstr_imm("http-proxy-exceptions"));


    /*
     * get the remaining values from the smsbox group
     */
    grp = cfg_get_single_group(cfg, octstr_imm("smsbox"));
    if (grp == NULL)
	panic(0, "No 'smsbox' group in configuration");

    p = cfg_get(grp, octstr_imm("bearerbox-host"));
    if (p != NULL) {
	octstr_destroy(bb_host);
	bb_host = p;
    }

    cfg_get_integer(&sendsms_port, grp, octstr_imm("sendsms-port"));
    cfg_get_integer(&sms_max_length, grp, octstr_imm("sms-length"));

    global_sender = cfg_get(grp, octstr_imm("global-sender"));
    accepted_chars = cfg_get(grp, octstr_imm("sendsms-chars"));
    logfile = cfg_get(grp, octstr_imm("log-file"));

    cfg_get_integer(&lvl, grp, octstr_imm("log-level"));

    if (logfile != NULL) {
	info(0, "Starting to log to file %s level %ld", 
	     octstr_get_cstr(logfile), lvl);
	log_open(octstr_get_cstr(logfile), lvl);
	octstr_destroy(logfile);
    }
    if (global_sender != NULL) {
	info(0, "Service global sender set as '%s'", 
	     octstr_get_cstr(global_sender));
    }
    
    p = cfg_get(grp, octstr_imm("access-log"));
    if (p != NULL) {
	info(0, "Logging accesses to '%s'.", octstr_get_cstr(p));
	alog_open(octstr_get_cstr(p), 1);
	    /* XXX should be able to use gmtime, too */
	octstr_destroy(p);
    }

    if (sendsms_port > 0) {
	if (http_open_server(sendsms_port) == -1) {
	    if (only_try_http)
		error(0, "Failed to open HTTP socket, ignoring it");
	    else
		panic(0, "Failed to open HTTP socket");
	}
	else {
	    info(0, "Set up send sms service at port %ld", sendsms_port);
	    gwthread_create(sendsms_thread, NULL);
	}
    }

    if (http_proxy_host != NULL && http_proxy_port > 0) {
    	http_use_proxy(http_proxy_host, http_proxy_port,
		       http_proxy_exceptions, http_proxy_username,
                       http_proxy_password);
    }

    octstr_destroy(http_proxy_host);
    octstr_destroy(http_proxy_username);
    octstr_destroy(http_proxy_password);
    list_destroy(http_proxy_exceptions, octstr_destroy_item);
}


static int check_args(int i, int argc, char **argv) {
    if (strcmp(argv[i], "-H")==0 || strcmp(argv[i], "--tryhttp")==0) {
	only_try_http = 1;
    } else
	return -1;

    return 0;
} 


int main(int argc, char **argv)
{
    int cf_index;
    long heartbeat_thread;
    Octstr *cfg_name;

    gwlib_init();
    cf_index = get_and_set_debugs(argc, argv, check_args);
    
    setup_signal_handlers();
    
    if (argv[cf_index] == NULL)
	cfg_name = octstr_create("kannel.conf");
    else
	cfg_name = octstr_create(argv[cf_index]);
    cfg = cfg_create(cfg_name);
    octstr_destroy(cfg_name);

    if (cfg_read(cfg) == -1)
	panic(0, "Error reading configuration file, cannot start.");

    report_versions("smsbox");

    init_smsbox(cfg);

    debug("sms", 0, "----------------------------------------------");
    debug("sms", 0, "Kannel smsbox version %s starting", VERSION);
    write_pid_file();

    translations = urltrans_create();
    if (translations == NULL)
	panic(errno, "urltrans_create failed");
    if (urltrans_add_cfg(translations, cfg) == -1)
	panic(errno, "urltrans_add_cfg failed");

    sendsms_number_chars = SENDSMS_DEFAULT_CHARS;
    caller = http_caller_create();
    smsbox_requests = list_create();
    list_add_producer(smsbox_requests);
    receivers = dict_create(1024, destroy_receiver);
    catenated_sms_counter = counter_create();
    gwthread_create(obey_request_thread, NULL);
    gwthread_create(url_result_thread, NULL);

    connect_to_bearerbox(bb_host, bb_port);

    heartbeat_thread = heartbeat_start(write_to_bearerbox, heartbeat_freq,
				       outstanding_requests);

    read_messages_from_bearerbox();

    info(0, "Kannel smsbox terminating.");

    heartbeat_stop(heartbeat_thread);
    http_close_all_servers();
    gwthread_join_every(sendsms_thread);
    list_remove_producer(smsbox_requests);
    gwthread_join_every(obey_request_thread);
    http_caller_signal_shutdown(caller);
    gwthread_join_every(url_result_thread);

    close_connection_to_bearerbox();
    alog_close();
    urltrans_destroy(translations);
    gw_assert(list_len(smsbox_requests) == 0);
    list_destroy(smsbox_requests, NULL);
    http_caller_destroy(caller);
    dict_destroy(receivers);
    counter_destroy(catenated_sms_counter);
    octstr_destroy(bb_host);
    octstr_destroy(global_sender);
    cfg_destroy(cfg);
    gwlib_shutdown();
    return 0;
}
