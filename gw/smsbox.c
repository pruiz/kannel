/*
 * smsbox.c - main program of the smsbox
 */

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include "gwlib/gwlib.h"

#include "msg.h"
#include "sms.h"
#include "bb.h"
#include "shared.h"
#include "heartbeat.h"
#include "html.h"
#include "urltrans.h"
#include "wap_ota_prov.h"


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
static Octstr *reply_couldnotfetch = NULL;
static Octstr *reply_couldnotrepresent = NULL;
static Octstr *reply_requestfailed = NULL;
static Octstr *reply_emptymessage = NULL;
static Numhash *white_list;
static Numhash *black_list;

static List *smsbox_requests = NULL;

int charset_processing (Octstr *charset, Octstr *text, int coding);

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

	if (msg_type(msg) == admin) {
	    if (msg->admin.command == cmd_shutdown) {
		info(0, "Bearerbox told us to die");
		program_status = shutting_down;
	    }
	    /*
	     * XXXX here should be suspend/resume, add RSN
	     */
	    msg_destroy(msg);
	} else if (msg_type(msg) == sms) {
	    if (total == 0)
		start = time(NULL);
	    total++;
	    list_produce(smsbox_requests, msg);
	} else {
	    warning(0, "Received other message than sms/admin, ignoring!");
	    msg_destroy(msg);
	}
    }
    secs = difftime(time(NULL), start);
    info(0, "Received (and handled?) %d requests in %d seconds "
    	 "(%.2f per second)", total, secs, (float)total / secs);
}


/***********************************************************************
 * Send Msg to bearerbox for delivery to phone, possibly split it first.
 */

/*
 * Counter for catenated SMS messages. The counter that can be put into
 * the catenated SMS message's UDH headers is actually the lowest 8 bits.
 */
static Counter *catenated_sms_counter;
 
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
     * warning defined  */
    if (octstr_len(msg->sms.msgdata) == 0) {
	if (trans != NULL && urltrans_omit_empty(trans))
            return 0;
        else
	    msg->sms.msgdata = octstr_duplicate(reply_emptymessage);
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
static Counter *num_outstanding_requests;


struct receiver {
    Msg *msg;
    URLTranslation *trans;
};


static void *remember_receiver(Msg *msg, URLTranslation *trans)
{
    struct receiver *receiver;
    
    counter_increase(num_outstanding_requests);
    receiver = gw_malloc(sizeof(*receiver));

    receiver->msg = msg_create(sms);
    receiver->msg->sms.sender = octstr_duplicate(msg->sms.sender);
    receiver->msg->sms.receiver = octstr_duplicate(msg->sms.receiver);
    receiver->msg->sms.service = octstr_duplicate(urltrans_name(trans));
    receiver->msg->sms.udhdata = NULL;
    receiver->msg->sms.mclass = 0;
    receiver->msg->sms.mwi = 0;
    receiver->msg->sms.coding = 0;
    receiver->msg->sms.compress = 0;
    receiver->msg->sms.msgdata = NULL;
    receiver->msg->sms.validity = 0;
    receiver->msg->sms.deferred = 0;
    receiver->msg->sms.time = (time_t) -1;
    receiver->msg->sms.smsc_id = octstr_duplicate(msg->sms.smsc_id);
    receiver->msg->sms.dlr_url = NULL;
    receiver->msg->sms.dlr_mask = msg->sms.dlr_mask; 
    	/* to remember if it's a DLR http get */
    
    receiver->trans = trans;

    return receiver;
}


static void get_receiver(void *id, Msg **msg, URLTranslation **trans)
{
    struct receiver *receiver;
    
    receiver = id;
    *msg = receiver->msg;
    *trans = receiver->trans;
    gw_free(receiver);
    counter_decrease(num_outstanding_requests);
}


static long outstanding_requests(void)
{
    return counter_value(num_outstanding_requests);
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


static void get_x_kannel_from_headers(List *headers, Octstr **from,
				      Octstr **to, Octstr **udh,
				      Octstr **user, Octstr **pass,
				      Octstr **smsc, int *mclass, int *mwi, 
				      int *coding, int *compress, 
				      int *validity, int *deferred, 
				      int *dlr_mask, Octstr **dlr_url)
{
    Octstr *name, *val;
    long l;

    *dlr_mask = 0;
    *dlr_url = NULL;
    *mclass = *mwi = *coding = *compress = *validity = *deferred = 0;
    for(l=0; l<list_len(headers); l++) {
	http_header_get(headers, l, &name, &val);

	if (octstr_case_compare(name, octstr_imm("X-Kannel-From")) == 0) {
	    *from = octstr_duplicate(val);
	    octstr_strip_blanks(*from);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-To")) == 0) {
	    *to = octstr_duplicate(val);
	    octstr_strip_blanks(*to);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Username")) == 0) {
	    if (user != NULL) {
		*user = octstr_duplicate(val);
		octstr_strip_blanks(*user);
	    }
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Password")) == 0) {
	    if (pass != NULL) {
		*pass = octstr_duplicate(val);
		octstr_strip_blanks(*pass);
	    }
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-SMSC")) == 0) {
	    if (smsc != NULL) {
		*smsc = octstr_duplicate(val);
		octstr_strip_blanks(*smsc);
	    }
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-UDH")) == 0) {
	    *udh = octstr_duplicate(val);
	    octstr_strip_blanks(*udh);
	    if (octstr_hex_to_binary(*udh) == -1) {
		octstr_destroy(*udh);
		*udh = NULL;
	    }
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-DLR-URL")) == 0) {
	    *dlr_url = octstr_duplicate(val);
	    octstr_strip_blanks(*dlr_url);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Flash")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", coding);
	    warning(0, "Flash field used and deprecated");
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Coding")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", coding);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-MWI")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", mwi);
        }
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-MClass")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", mclass);
        }
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Compress")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", compress);
        }
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Validity")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", validity);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Deferred")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", deferred);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-DLR-Mask")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", dlr_mask);
	}
	octstr_destroy(name);
	octstr_destroy(val);
    }
}

static void fill_message(Msg *msg, URLTranslation *trans,
			 Octstr *replytext, int octet_stream,
			 Octstr *from, Octstr *to, Octstr *udh, 
			 int mclass, int mwi, int coding, int compress,
			 int validity, int deferred,
			 Octstr *dlr_url, int dlr_mask)
{    
    msg->sms.msgdata = replytext;
    msg->sms.time = time(NULL);

    if (dlr_url != NULL) {
	if (urltrans_accept_x_kannel_headers(trans)) {
	    octstr_destroy(msg->sms.dlr_url);
	    msg->sms.dlr_url = dlr_url;
	} else {
	    warning(0, "Tried to change dlr_url to '%s', denied.",
		    octstr_get_cstr(dlr_url));
	    octstr_destroy(dlr_url);
	}
    }

    if (from != NULL) {
	if (urltrans_accept_x_kannel_headers(trans)) {
	    octstr_destroy(msg->sms.sender);
	    msg->sms.sender = from;
	} else {
	    warning(0, "Tried to change sender to '%s', denied.",
		    octstr_get_cstr(from));
	    octstr_destroy(from);
	}
    }
    if (to != NULL) {
	if (urltrans_accept_x_kannel_headers(trans)) {
	    octstr_destroy(msg->sms.receiver);
	    msg->sms.receiver = to;
	} else {
	    warning(0, "Tried to change receiver to '%s', denied.",
		    octstr_get_cstr(to));
	    octstr_destroy(to);
	}
    }
    if (udh != NULL) {
	if (urltrans_accept_x_kannel_headers(trans)) {
	    msg->sms.udhdata = udh;
	} else {
	    warning(0, "Tried to set UDH field, denied.");
	    octstr_destroy(udh);
	}
    }
    if (mclass) {
        if (urltrans_accept_x_kannel_headers(trans))
	    msg->sms.mclass = mclass;	  
	else 
	    warning(0, "Tried to set MClass field, denied.");
    }
    if (mwi) {
        if (urltrans_accept_x_kannel_headers(trans))
	    msg->sms.mwi = mwi;	  
	else 
	    warning(0, "Tried to set MWI field, denied.");
    }
    if (coding) {
        if (urltrans_accept_x_kannel_headers(trans))
	    msg->sms.coding = coding;	  
	else
	    warning(0, "Tried to set Coding field, denied.");
    }
    if (compress) {
        if (urltrans_accept_x_kannel_headers(trans))
	    msg->sms.compress = compress;	  
	else
	    warning(0, "Tried to set Compress field, denied.");
    }
    /* Compatibility Mode */
    if ( msg->sms.coding == DC_UNDEF) {
	if(octstr_len(udh))
	  msg->sms.coding = DC_8BIT;
	else
	  msg->sms.coding = DC_7BIT;
    }

    if (validity) {
	if (urltrans_accept_x_kannel_headers(trans))
	    msg->sms.validity = validity;
	else
	    warning(0, "Tried to change validity to '%d', denied.",
		    validity);
    }
    if (deferred) {
	if (urltrans_accept_x_kannel_headers(trans))
	    msg->sms.deferred = deferred;
	else
	    warning(0, "Tried to change deferred to '%d', denied.",
		    deferred);
    }

    if (dlr_mask) {
	if (urltrans_accept_x_kannel_headers(trans)) {
	    msg->sms.dlr_mask = dlr_mask;
	} else
	    warning(0, "Tried to change dlr_mask to '%d', denied.",
		    dlr_mask);
    }
}


static void url_result_thread(void *arg)
{
    Octstr *final_url, *reply_body, *type, *charset, *replytext;
    List *reply_headers;
    int status;
    void *id;
    Msg *msg;
    URLTranslation *trans;
    Octstr *text_html;
    Octstr *text_plain;
    Octstr *text_wml;
    Octstr *octet_stream;
    Octstr *udh, *from, *to;
    Octstr *dlr_url;
    int dlr_mask;
    int octets;
    int mclass, mwi, coding, compress;
    int validity, deferred;
    
    dlr_mask = 0;
    dlr_url = NULL;
    text_html = octstr_imm("text/html");
    text_wml = octstr_imm("text/vnd.wap.wml");
    text_plain = octstr_imm("text/plain");
    octet_stream = octstr_imm("application/octet-stream");

    for (;;) {
    	id = http_receive_result(caller, &status, &final_url, &reply_headers,
	    	    	    	 &reply_body);
    	if (id == NULL)
	    break;
    	
    	get_receiver(id, &msg, &trans);

    	from = to = udh = NULL;
	octets = mclass = mwi = coding = compress = 0;
	validity = deferred = 0;
	
    	if (status == HTTP_OK) {
	    http_header_get_content_type(reply_headers, &type, &charset);
	    if (octstr_case_compare(type, text_html) == 0 ||
		octstr_case_compare(type, text_wml) == 0) {
		strip_prefix_and_suffix(reply_body,
					urltrans_prefix(trans), 
					urltrans_suffix(trans));
		replytext = html_to_sms(reply_body);
		octstr_strip_blanks(replytext);
    	    	get_x_kannel_from_headers(reply_headers, &from, &to, &udh,
					  NULL, NULL, NULL, &mclass, &mwi, 
					  &coding, &compress, &validity, 
					  &deferred, &dlr_mask, &dlr_url);
	    } else if (octstr_case_compare(type, text_plain) == 0) {
		replytext = octstr_duplicate(reply_body);
		reply_body = NULL;
		octstr_strip_blanks(replytext);
    	    	get_x_kannel_from_headers(reply_headers, &from, &to, &udh,
					  NULL, NULL, NULL, &mclass, &mwi, 
					  &coding, &compress, &validity, 
					  &deferred, &dlr_mask, &dlr_url);
	    } else if (octstr_case_compare(type, octet_stream) == 0) {
		replytext = octstr_duplicate(reply_body);
		octets = 1;
		reply_body = NULL;
    	    	get_x_kannel_from_headers(reply_headers, &from, &to, &udh,
					  NULL, NULL, NULL, &mclass, &mwi, 
					  &coding, &compress, &validity, 
					  &deferred, &dlr_mask, &dlr_url);
	    } else {
		replytext = octstr_duplicate(reply_couldnotrepresent); 
	    }

	    if (charset_processing(charset, replytext, coding) == -1) {
		replytext = octstr_duplicate(reply_couldnotrepresent);
	    }
	    octstr_destroy(type);
	    octstr_destroy(charset);
	} else
	    replytext = octstr_duplicate(reply_couldnotfetch);

	fill_message(msg, trans, replytext, octets, from, to, udh, mclass,
			mwi, coding, compress, validity, deferred, dlr_url, 
			dlr_mask);

    	if (final_url == NULL)
	    final_url = octstr_imm("");
    	if (reply_body == NULL)
	    reply_body = octstr_imm("");

	if (msg->sms.dlr_mask == 0) {
	    alog("SMS HTTP-request sender:%s request: '%s' "
		"url: '%s' reply: %d '%s'",
		octstr_get_cstr(msg->sms.receiver),
		(msg->sms.msgdata != NULL) ? octstr_get_cstr(msg->sms.msgdata) : "",
		octstr_get_cstr(final_url),
		status,
		(status == HTTP_OK) 
		? "<< successful >>"
		: octstr_get_cstr(reply_body));
	}
		
    	octstr_destroy(final_url);
	http_destroy_headers(reply_headers);
	octstr_destroy(reply_body);
    
	if (msg->sms.dlr_mask == 0) {
	    if ( send_message(trans, msg) < 0)
		error(0, "failed to send message to phone");
	}	
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
    void *id;
    struct tm tm;
    char p[22];
    int type;
    
    gw_assert(msg != NULL);
    gw_assert(msg_type(msg) == sms);
    
    if (msg->sms.sms_type == report)
	type = TRANSTYPE_GET_URL;
    else
	type = urltrans_type(trans);

    pattern = urltrans_get_pattern(trans, msg);
    gw_assert(pattern != NULL);
    
    switch (type) {
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
    
    case TRANSTYPE_GET_URL:
	request_headers = http_create_empty_headers();
	http_header_add(request_headers, "User-Agent", "Kannel " VERSION);
	if (urltrans_send_sender(trans)) {
	    http_header_add(request_headers, "X-Kannel-From",
			    octstr_get_cstr(msg->sms.receiver));
	}
	
	id = remember_receiver(msg, trans);
	http_start_request(caller, pattern, request_headers, NULL, 1, id,
 			   NULL);
	octstr_destroy(pattern);
	http_destroy_headers(request_headers);
	*result = NULL;
	return 0;

    case TRANSTYPE_POST_URL:
	request_headers = http_create_empty_headers();
	http_header_add(request_headers, "User-Agent", "Kannel " VERSION);
	id = remember_receiver(msg, trans);
	/* XXX Which header should we use for UCS2 ? octstr also ? */
	if (msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2)
	    http_header_add(request_headers, "Content-Type",
			    "application/octet-stream");
	else
	    http_header_add(request_headers, "Content-Type", "text/plain");
	if (urltrans_send_sender(trans))
	    http_header_add(request_headers, "X-Kannel-From",
			    octstr_get_cstr(msg->sms.receiver));
	http_header_add(request_headers, "X-Kannel-To",
			octstr_get_cstr(msg->sms.sender));

	/* should we use localtime? FIX ME */
	tm = gw_gmtime(msg->sms.time);
	sprintf(p, "%04d-%02d-%02d %02d:%02d:%02d",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	http_header_add(request_headers, "X-Kannel-Time", p);
	if (octstr_len(msg->sms.udhdata)) {
	    Octstr *os;
	    os = octstr_duplicate(msg->sms.udhdata);
	    octstr_binary_to_hex(os, 1);
	    http_header_add(request_headers, "X-Kannel-UDH",
			    octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	if(msg->sms.mclass) {
	    Octstr *os;
	    os = octstr_format("%d",msg->sms.mclass);
	    http_header_add(request_headers, "X-Kannel-MClass", 
	    	octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	if(msg->sms.mwi) {
	    Octstr *os;
	    os = octstr_format("%d",msg->sms.mwi);
	    http_header_add(request_headers, "X-Kannel-MWI", 
	    	octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	if(msg->sms.coding) {
	    Octstr *os;
	    os = octstr_format("%d",msg->sms.coding);
	    http_header_add(request_headers, "X-Kannel-Coding", 
	    	octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	if(msg->sms.compress) {
	    Octstr *os;
	    os = octstr_format("%d",msg->sms.compress);
	    http_header_add(request_headers, "X-Kannel-Compress", 
	    	octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	if (msg->sms.validity) {
	    Octstr *os;
	    os = octstr_format("%d",msg->sms.validity);
	    http_header_add(request_headers, "X-Kannel-Validity", 
	    	octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	if (msg->sms.deferred) {
	    Octstr *os;
	    os = octstr_format("%d",msg->sms.deferred);
	    http_header_add(request_headers, "X-Kannel-Deferred", 
	    	octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	http_start_request(caller, pattern, request_headers, 
 			   msg->sms.msgdata, 1, id, NULL);
	octstr_destroy(pattern);
	http_destroy_headers(request_headers);
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
    int ret, dreport=0;
    
    while ((msg = list_consume(smsbox_requests)) != NULL) {
	if (msg->sms.sms_type == report)
	    dreport = 1;
	else
	    dreport = 0;

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
	reply_msg->ack.nack = 0;
	reply_msg->ack.time = msg->sms.time;
	reply_msg->ack.id = msg->sms.id;

    
	if (dreport) {
	    trans = urltrans_find_service(translations, msg);

	    info(0, "Starting delivery report <%s> from <%s>",
		octstr_get_cstr(msg->sms.service),
		octstr_get_cstr(msg->sms.sender));

	} else {
	    trans = urltrans_find(translations, msg->sms.msgdata, 
	    	    	      msg->sms.smsc_id, msg->sms.sender);
	    if (trans == NULL) {
		Octstr *t;
		warning(0, "No translation found for <%s> from <%s> to <%s>",
		    msg->sms.msgdata != NULL ? octstr_get_cstr(msg->sms.msgdata) : "",
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
		msg->sms.sender = octstr_duplicate(p);
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
	}

	/* TODO: check if the sender is approved to use this service */

	ret = obey_request(&reply, trans, msg);
	if (ret != 0) {
	    if (ret == -1) {
    error:
	        error(0, "request failed");
	        /* XXX this can be something different, according to 
	           urltranslation */
	        reply = reply_requestfailed;
	        trans = NULL;	/* do not use any special translation */
	    }
	    octstr_destroy(msg->sms.msgdata);
	    msg->sms.msgdata = reply;
	
	    msg->sms.coding = 0;
	    msg->sms.time = time(NULL);	/* set current time */
	
	    if (!dreport) {
	    if (send_message(trans, msg) < 0)
		error(0, "request_thread: failed");
	    }
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
    info(0, "/sendsms used by <%s>", login);
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






static Octstr *smsbox_req_handle(URLTranslation *t, Octstr *client_ip,
				 Octstr *from, Octstr *to, Octstr *text, 
				 Octstr *charset, Octstr *udh, Octstr *smsc,
				 int mclass, int mwi, int coding, int compress, 
				 int validity, int deferred, 
				 int *status, int dlr_mask, Octstr *dlr_url)
{				     
    Msg *msg = NULL;
    Octstr *newfrom, *returnerror;
    int ret;

    /*
     * check if UDH length is legal, or otherwise discard the
     * message, to prevent intentional buffer overflow schemes
     */
    if (udh != NULL && (octstr_len(udh) != octstr_get_char(udh, 0) + 1)) {
	returnerror = octstr_create("UDH field misformed, rejected");
	goto fielderror2;
    }

    if (strspn(octstr_get_cstr(to), sendsms_number_chars) < octstr_len(to)) {
	info(0,"Illegal characters in 'to' string ('%s') vs '%s'",
	     octstr_get_cstr(to), sendsms_number_chars);
	returnerror = octstr_create("Garbage 'to' field, rejected.");
	goto fielderror2;
    }
    if (urltrans_white_list(t) &&
	numhash_find_number(urltrans_white_list(t), to) < 1) {
	info(0, "Number <%s> is not in white-list, message discarded",
	octstr_get_cstr(to));
	returnerror = octstr_create("Number is not in white-list.");
	goto fielderror2;
    }
    if (urltrans_black_list(t) &&
	numhash_find_number(urltrans_black_list(t), to) == 1) {
	info(0, "Number <%s> is in black-list, message discarded",
	octstr_get_cstr(to));
	returnerror = octstr_create("Number is in black-list.");
	goto fielderror2;
    }
    
    if (white_list &&
	numhash_find_number(white_list, to) < 1) {
	info(0, "Number <%s> is not in global white-list, message discarded",
	octstr_get_cstr(to));
	returnerror = octstr_create("Number is not in global white-list.");
	goto fielderror2;
    }
    if (black_list &&
	numhash_find_number(black_list, to) == 1) {
	info(0, "Number <%s> is in global black-list, message discarded",
	octstr_get_cstr(to));
	returnerror = octstr_create("Number is in global black-list.");
	goto fielderror2;
    }
    
    if (urltrans_faked_sender(t) != NULL) {
	/* discard previous from */
	newfrom = octstr_duplicate(urltrans_faked_sender(t));
    } else if (octstr_len(from) > 0) {
	newfrom = octstr_duplicate(from);
    } else if (global_sender != NULL) {
	newfrom = octstr_duplicate(global_sender);
    } else {
	returnerror = octstr_create("Sender missing and no global set, rejected");
	goto fielderror2;
    }

    info(0, "sendsms sender:<%s:%s> (%s) to:<%s> msg:<%s>",
	 octstr_get_cstr(urltrans_username(t)),
	 octstr_get_cstr(newfrom),
	 octstr_get_cstr(client_ip),
	 octstr_get_cstr(to),
	 udh == NULL ? ( text == NULL ? "" : octstr_get_cstr(text) ) : "<< UDH >>");
    
    /*
     * XXX here we should validate and split the 'to' field
     *   to allow multi-cast. Waiting for octstr_split...
     */
    msg = msg_create(sms);
    
    msg->sms.service = octstr_duplicate(urltrans_name(t));
    msg->sms.sms_type = mt_push;
    msg->sms.receiver = octstr_duplicate(to);
    msg->sms.sender = octstr_duplicate(newfrom);
    msg->sms.msgdata = text ? octstr_duplicate(text) : octstr_create("");
    msg->sms.udhdata = udh ? octstr_duplicate(udh) : octstr_create("");
    msg->sms.dlr_mask = dlr_mask;
    msg->sms.dlr_url = dlr_url ? octstr_duplicate(dlr_url) : octstr_create("");

    if ( mclass < 0 || mclass > 4 ) {
	returnerror = octstr_create("MClass field misformed, rejected");
	goto fielderror;
    }
    msg->sms.mclass = mclass;
    
    if ( mwi < 0 || mwi > 8 ) {
	returnerror = octstr_create("MWI field misformed, rejected");
	goto fielderror;
    }
    msg->sms.mwi = mwi;

    if ( coding < 0 || coding > 4 ) {
	returnerror = octstr_create("Coding field misformed, rejected");
	goto fielderror;
    }
    msg->sms.coding = coding;

    if ( compress < 0 || compress > 1 ) {
	returnerror = octstr_create("Compress field misformed, rejected");
	goto fielderror;
    }
    msg->sms.compress = compress;

    /* Compatibility Mode */
    if ( msg->sms.coding == DC_UNDEF) {
	if(octstr_len(udh))
	  msg->sms.coding = DC_8BIT;
	else
	  msg->sms.coding = DC_7BIT;
    }
	

    if ( validity < 0 ) {
	returnerror = octstr_create("Validity field misformed, rejected");
	goto fielderror;
    }
    msg->sms.validity = validity;

    if ( deferred < 0 ) {
	returnerror = octstr_create("Deferred field misformed, rejected");
	goto fielderror;
    }
    msg->sms.deferred = deferred;

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

    if (charset_processing(charset, msg->sms.msgdata, msg->sms.coding) == -1) {
	returnerror = octstr_create("Charset or body misformed, rejected");
	goto fielderror;
    }

    msg->sms.time = time(NULL);
    ret = send_message(t, msg);
    msg_destroy(msg);
    
    if (ret == -1)
	goto error;
    
    alog("send-SMS request added - sender:%s:%s %s target:%s request: '%s'",
	 octstr_get_cstr(urltrans_username(t)),
         octstr_get_cstr(newfrom), octstr_get_cstr(client_ip),
	 octstr_get_cstr(to),
	 udh == NULL ? ( text == NULL ? "" : octstr_get_cstr(text) ) : "<< UDH >>");

    octstr_destroy(newfrom);
    *status = 202;
    return octstr_create("Sent.");
    

fielderror:
    octstr_destroy(newfrom);
    msg_destroy(msg);

fielderror2:
    alog("send-SMS request failed - %s",
	 octstr_get_cstr(returnerror));

    *status = 400;
    return returnerror;

error:
    error(0, "sendsms_request: failed");
    octstr_destroy(from);
    *status = 500;
    return octstr_create("Sending failed.");
}


/*
 * new authorisation, usable by POST and GET
 */
static URLTranslation *authorise_username(Octstr *username, Octstr *password,
					  Octstr *client_ip) 
{
    URLTranslation *t = NULL;

    if (username == NULL || password == NULL)
	return NULL;
    
    if ((t = urltrans_find_username(translations, username))==NULL)
	return NULL;

    if (octstr_compare(password, urltrans_password(t))!=0)
	return NULL;
    else {
	Octstr *allow_ip = urltrans_allow_ip(t);
	Octstr *deny_ip = urltrans_deny_ip(t);
	
        if (is_allowed_ip(allow_ip, deny_ip, client_ip) == 0) {
	    warning(0, "Non-allowed connect tried by <%s> from <%s>, ignored",
		    octstr_get_cstr(username), octstr_get_cstr(client_ip));
	    return NULL;
        }
    }

    info(0, "sendsms used by <%s>", octstr_get_cstr(username));
    return t;
}

/*
 * Authentication whith the database of Kannel.
 * Check for matching username and password for requests.
 * Return an URLTranslation if successful NULL otherwise.
 */
static URLTranslation *default_authorise_user(List *list, Octstr *client_ip) 
{
    Octstr *pass, *user = NULL;

    if ((user = http_cgi_variable(list, "username")) == NULL)
        user = http_cgi_variable(list, "user");

    if ((pass = http_cgi_variable(list, "password")) == NULL)
	pass = http_cgi_variable(list, "pass");

    return authorise_username(user, pass, client_ip);
}


static URLTranslation *authorise_user(List *list, Octstr *client_ip) 
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
	return default_authorise_user(list, client_ip);
#else
    return default_authorise_user(list, client_ip);
#endif
}


/*
 * Create and send an SMS message from an HTTP request.
 * Args: args contains the CGI parameters
 */
static Octstr *smsbox_req_sendsms(List *args, Octstr *client_ip, int *status)
{
    URLTranslation *t = NULL;
    Octstr *from, *to, *charset;
    Octstr *text, *udh, *smsc, *tmp_string;
    Octstr *dlr_url = NULL;
    int	dlr_mask = 0;
    Octstr *dlr_mask_string;
    int mclass, mwi, coding, compress, validity, deferred;
   
    /* check the username and password */
    t = authorise_user(args, client_ip);
    if (t == NULL) {
	*status = 403;
	return octstr_create("Authorization failed for sendsms");
    }
    
    udh = http_cgi_variable(args, "udh");
    text = http_cgi_variable(args, "text");
    charset = http_cgi_variable(args, "charset");
    smsc = http_cgi_variable(args, "smsc");
    from = http_cgi_variable(args, "from");
    to = http_cgi_variable(args, "to");
    dlr_url = http_cgi_variable(args, "dlrurl");
    dlr_mask_string = http_cgi_variable(args, "dlrmask");

    if(dlr_mask_string != NULL)
        sscanf(octstr_get_cstr(dlr_mask_string),"%d",&dlr_mask);
    else
    	dlr_mask = 0;

    mclass = mwi = coding = compress = validity = deferred = 0;

    tmp_string = NULL;
    tmp_string = http_cgi_variable(args, "flash");
    if(tmp_string != NULL) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &mclass);
	warning(0, "Flash field used and deprecated");
    }

    tmp_string = NULL;
    tmp_string = http_cgi_variable(args, "mclass");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &mclass);

    tmp_string = NULL;
    tmp_string = http_cgi_variable(args, "mwi");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &mwi);

    tmp_string = NULL;
    tmp_string = http_cgi_variable(args, "coding");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &coding);

    tmp_string = NULL;
    tmp_string = http_cgi_variable(args, "compress");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &compress);

    tmp_string = NULL;
    tmp_string = http_cgi_variable(args, "validity");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &validity);

    tmp_string = NULL;
    tmp_string = http_cgi_variable(args, "deferred");
    if(tmp_string != NULL) {
        sscanf(octstr_get_cstr(tmp_string),"%d", &deferred);
    }

    if (to == NULL) {
	error(0, "/sendsms got wrong args");
	*status = 400;
	return octstr_create("Wrong sendsms args, rejected");
    }

    return smsbox_req_handle(t, client_ip, from, to, text, charset, udh, 
			     smsc, mclass, mwi, coding, compress, validity, 
			     deferred, status, dlr_mask, dlr_url);
    
}


/*
 * Create and send an SMS message from an HTTP request.
 * Args: args contains the CGI parameters
 */
static Octstr *smsbox_sendsms_post(List *headers, Octstr *body,
				   Octstr *client_ip, int *status)
{
    URLTranslation *t = NULL;
    Octstr *from, *to, *user, *pass, *udh, *smsc;
    Octstr *ret;
    Octstr *type, *charset;
    Octstr *dlr_url;
    int dlr_mask = 0;
    int mclass, mwi, coding, compress, validity, deferred;
 
    from = to = user = pass = udh = smsc = dlr_url = NULL;
   
    get_x_kannel_from_headers(headers, &from, &to, &udh,
			      &user, &pass, &smsc, &mclass, &mwi, &coding,
			      &compress, &validity, &deferred, 
			      &dlr_mask, &dlr_url);
    
    ret = NULL;
    
    /* check the username and password */
    t = authorise_username(user, pass, client_ip);
    if (t == NULL) {
	*status = 403;
	ret = octstr_create("Authorization failed for sendsms");
    }
    else if (to == NULL) {
	error(0, "/sendsms got insufficient headers");
	*status = 400;
	ret = octstr_create("Insufficient headers, rejected");
    } else {
	/* XXX here we should take into account content-type of body
	 */
	http_header_get_content_type(headers, &type, &charset);

	if (octstr_case_compare(type,
				octstr_imm("application/octet-stream")) == 0) {
	    if (coding == DC_UNDEF)
		coding = DC_8BIT; /* XXX Force UCS2 with DC Field */
	} else if (octstr_case_compare(type,
				       octstr_imm("text/plain")) == 0) {
	    if (coding == DC_UNDEF)
		coding = DC_7BIT;
	} else {
	    error(0, "/sendsms got weird content type %s",
		  octstr_get_cstr(type));
	    *status = 415;
	    ret = octstr_create("Unsupported content-type, rejected");
	}

	if (ret == NULL)
	    ret = smsbox_req_handle(t, client_ip, from, to, body, charset,
				    udh, smsc, mclass, mwi, coding, compress, 
				    validity, deferred, status, 
				    dlr_mask, dlr_url);

	octstr_destroy(type);
	octstr_destroy(charset);
    }
    octstr_destroy(from);
    octstr_destroy(to);
    octstr_destroy(user);
    octstr_destroy(pass);
    octstr_destroy(udh);
    octstr_destroy(smsc);
    return ret;
}


/*
 * Create and send an SMS OTA (auto configuration) message from an HTTP 
 * request.
 * Args: list contains the CGI parameters
 * 
 * Official Nokia and Ericsson WAP OTA configuration settings coded 
 * by Stipe Tolj <tolj@wapme-systems.de>, Wapme Systems AG.
 * 
 * This will be changed later to use an XML compiler.
 */
static Octstr *smsbox_req_sendota(List *list, Octstr *client_ip, int *status)
{
    Octstr *url, *desc, *ipaddr, *phonenum, *username, *passwd, *id, *from;
    int speed, bearer, calltype, connection, security, authent;
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
    connection = WBXML_TOK_VALUE_PORT_9201;
    security = 0;
    authent = WBXML_TOK_VALUE_AUTH_PAP;
    phonenumber = NULL;

    /* check the username and password */
    t = authorise_user(list, client_ip);
    if (t == NULL) {
	*status = 403;
	return octstr_create("Authorization failed for sendota");
    }
    
    phonenumber = http_cgi_variable(list, "phonenumber");
    if (phonenumber == NULL) {
	error(0, "/cgi-bin/sendota needs a valid phone number.");
	*status = 400;
	return octstr_create("Wrong sendota args.");
    }

    if (urltrans_faked_sender(t) != NULL) {
	from = octstr_duplicate(urltrans_faked_sender(t));
    } else if ((from = http_cgi_variable(list, "from")) != NULL &&
	       octstr_len(from) > 0) {
	from = octstr_duplicate(from);
    } else if (global_sender != NULL) {
	from = octstr_duplicate(global_sender);
    } else {
	*status = 400;
	return octstr_create("Sender missing and no global set, rejected");
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
    *status = 400;
    return octstr_create("Missing otaconfig group.");

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
	    bearer = WBXML_TOK_VALUE_GSM_CSD;
	else
	    bearer = -1;
    octstr_destroy(p);
    }
    p = cfg_get(grp, octstr_imm("calltype"));
    if (p != NULL) {
	if (strcasecmp(octstr_get_cstr(p), "calltype") == 0)
	    calltype = WBXML_TOK_VALUE_CONN_ISDN;
	else
	    calltype = -1;
    octstr_destroy(p);
    }
	
    speed = WBXML_TOK_VALUE_SPEED_9600;
    p = cfg_get(grp, octstr_imm("speed"));
    if (p != NULL) {
	if (octstr_compare(p, octstr_imm("14400")) == 0)
	    speed = WBXML_TOK_VALUE_SPEED_14400;
    octstr_destroy(p);
    }

    /* connection mode and security */
    p = cfg_get(grp, octstr_imm("connection"));
    if (p != NULL) {
	if (strcasecmp(octstr_get_cstr(p), "temp") == 0)
	    connection = WBXML_TOK_VALUE_PORT_9200;
	else
	    connection = WBXML_TOK_VALUE_PORT_9201;
    octstr_destroy(p);
    }

    p = cfg_get(grp, octstr_imm("pppsecurity"));
    if (p != NULL) {
	if (strcasecmp(octstr_get_cstr(p), "on") == 0)
	    security = 1;
	else
	    security = WBXML_TOK_VALUE_PORT_9201;
    octstr_destroy(p);
    }
    if (security == 1)
	connection = (connection == WBXML_TOK_VALUE_PORT_9201)? 
        WBXML_TOK_VALUE_PORT_9203 : WBXML_TOK_VALUE_PORT_9202;
    
    p = cfg_get(grp, octstr_imm("authentication"));
    if (p != NULL) {
	if (strcasecmp(octstr_get_cstr(p), "secure") == 0)
	    authent = WBXML_TOK_VALUE_AUTH_CHAP;
	else
	    authent = WBXML_TOK_VALUE_AUTH_PAP;
    octstr_destroy(p);
    }
    
    username = cfg_get(grp, octstr_imm("login"));
    passwd = cfg_get(grp, octstr_imm("secret"));
    
    msg = msg_create(sms);

    /*
     * Append the User Data Header (UDH) including the lenght (UDHL)
     * WDP layer (start WDP headers)
     */
    
    msg->sms.sms_type = mt_push;
    msg->sms.udhdata = octstr_create("");

    octstr_append_from_hex(msg->sms.udhdata, "0B0504C34FC0020003040201");
    /* WDP layer (end WDP headers) */

    /*
     * WSP layer (start WSP headers)
     */
    
    msg->sms.msgdata = octstr_create("");
    /* PUSH ID, PDU type, header length, value length */
    octstr_append_from_hex(msg->sms.msgdata, "01062C1F2A");
    /* MIME-type: application/x-wap-prov.browser-settings */
    octstr_append_from_hex(msg->sms.msgdata, "6170706C69636174696F");
    octstr_append_from_hex(msg->sms.msgdata, "6E2F782D7761702D7072");
    octstr_append_from_hex(msg->sms.msgdata, "6F762E62726F77736572");
    octstr_append_from_hex(msg->sms.msgdata, "2D73657474696E677300");
    /* charset UTF-8 */
    octstr_append_from_hex(msg->sms.msgdata, "81EA");
    /* WSP layer (end WSP headers) */

    /*
     * WSP layer (start WSP data field)
     */

    /* WBXML version 1.1 */
    octstr_append_from_hex(msg->sms.msgdata, "0101");
    /* charset UTF-8 */
    octstr_append_from_hex(msg->sms.msgdata, "6A00");

    /* CHARACTERISTIC_LIST */
    octstr_append_from_hex(msg->sms.msgdata, "45");
    /* CHARACTERISTIC with content and attributes */
    octstr_append_from_hex(msg->sms.msgdata, "C6");
    /* TYPE=ADDRESS */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_TYPE_ADDRESS);
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);

    /* bearer type */
    if (bearer != -1) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=BEARER, VALUE=GSM_CSD */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_BEARER);
        octstr_append_char(msg->sms.msgdata, bearer);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* IP address */
    if (ipaddr != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PROXY, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PROXY);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, ipaddr);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* connection type */
    if (connection != -1) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PORT, VALUE */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PORT);
        octstr_append_char(msg->sms.msgdata, connection);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* phone number */
    if (phonenum != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=CSD_DIALSTRING, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_CSD_DIALSTRING);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, phonenum);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* authentication */
    /* PARM with attributes */
    octstr_append_from_hex(msg->sms.msgdata, "87");
     /* NAME=PPP_AUTHTYPE, VALUE */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PPP_AUTHTYPE);
    octstr_append_char(msg->sms.msgdata, authent);
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    /* user name */
    if (username != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PPP_AUTHNAME, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PPP_AUTHNAME);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, username);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* password */
    if (passwd != NULL) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=PPP_AUTHSECRET, VALUE, inline string */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_PPP_AUTHSECRET);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, passwd);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* data call type */
    if (calltype != -1) {
        /* PARM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=CSD_CALLTYPE, VALUE */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_CSD_CALLTYPE);
        octstr_append_char(msg->sms.msgdata, calltype);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }
    /* speed */
    /* PARM with attributes */
    octstr_append_from_hex(msg->sms.msgdata, "87");
    /* NAME=CSD_CALLSPEED, VALUE */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_CSD_CALLSPEED);
    octstr_append_char(msg->sms.msgdata, speed);
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);

    /* end CHARACTERISTIC TYPE=ADDRESS */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);

    /* homepage */
    if (url != NULL) {
        /* CHARACTERISTIC with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "86");
        /* TYPE=URL */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_TYPE_URL);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, url);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }

    /* CHARACTERISTIC with content and attributes */
    octstr_append_from_hex(msg->sms.msgdata, "C6");
    /* TYPE=NAME */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_TYPE_NAME);
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);

    /* service description */
    if (desc != NULL) {
        /* PARAM with attributes */
        octstr_append_from_hex(msg->sms.msgdata, "87");
        /* NAME=NAME, VALUE, inline */
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_NAME_NAME);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_VALUE);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_STR_I);
        octstr_append(msg->sms.msgdata, desc);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END_STR_I);
        octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    }

    /* end of CHARACTERISTIC */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    /* end of CHARACTERISTIC-LIST */
    octstr_append_char(msg->sms.msgdata, WBXML_TOK_END);
    /* WSP layer (end WSP data field) */

    msg->sms.sender = from;
    msg->sms.receiver = octstr_duplicate(phonenumber);
    msg->sms.coding = DC_8BIT;
    
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
	*status = 500;
	return octstr_create("Sending failed.");
    }

    *status = 202;
    return octstr_create("Sent.");
}


static void sendsms_thread(void *arg)
{
    HTTPClient *client;
    Octstr *ip, *url, *body, *answer;
    List *hdrs, *args, *reply_hdrs;
    int status;

    reply_hdrs = http_create_empty_headers();
    http_header_add(reply_hdrs, "Content-type", "text/html");
    http_header_add(reply_hdrs, "Pragma", "no-cache");
    http_header_add(reply_hdrs, "Cache-Control", "no-cache");

    for (;;) {
    	client = http_accept_request(sendsms_port, &ip, &url, &hdrs, &body, 
	    	    	    	     &args);
	if (client == NULL)
	    break;

	info(0, "smsbox: Got HTTP request <%s> from <%s>",
	    octstr_get_cstr(url), octstr_get_cstr(ip));

	if (octstr_str_compare(url, "/cgi-bin/sendsms") == 0
	    || octstr_str_compare(url, "/sendsms") == 0)
	{
	    if (body == NULL)
		answer = smsbox_req_sendsms(args, ip, &status);
	    else
		answer = smsbox_sendsms_post(hdrs, body, ip, &status);
	}
	else if (octstr_str_compare(url, "/cgi-bin/sendota") == 0)
	    answer = smsbox_req_sendota(args, ip, &status);
	else {
	    answer = octstr_create("Unknown request.\n");
	    status = 404;
	}
        debug("sms.http", 0, "Status: %d Answer: <%s>", status,
	      octstr_get_cstr(answer));

	octstr_destroy(ip);
	octstr_destroy(url);
	http_destroy_headers(hdrs);
	octstr_destroy(body);
	http_destroy_cgiargs(args);
	
	http_send_reply(client, status, reply_hdrs, answer);

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
        alog_reopen();
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
    int ssl = 0;

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

    conn_config_ssl (grp);
    
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

    reply_couldnotfetch= cfg_get(grp, octstr_imm("reply-couldnotfetch"));
    if (reply_couldnotfetch == NULL)
	reply_couldnotfetch = octstr_create("Could not fetch content, sorry.");

    reply_couldnotrepresent= cfg_get(grp, octstr_imm("reply-couldnotfetch"));
    if (reply_couldnotrepresent == NULL)
	reply_couldnotrepresent = octstr_create("Result could not be represented "
					        "as an SMS message.");
    reply_requestfailed= cfg_get(grp, octstr_imm("reply-requestfailed"));
    if (reply_requestfailed == NULL)
	reply_requestfailed = octstr_create("Request Failed");

    reply_emptymessage= cfg_get(grp, octstr_imm("reply-emptymessage"));
    if (reply_emptymessage == NULL)
	reply_emptymessage = octstr_create("<Empty reply from service provider>");

    {   
	Octstr *os;
	os = cfg_get(grp, octstr_imm("white-list"));
	if (os != NULL) {
	    white_list = numhash_create(octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	os = cfg_get(grp, octstr_imm("black-list"));
	if (os != NULL) {
	    black_list = numhash_create(octstr_get_cstr(os));
	    octstr_destroy(os);
	}
    }

    cfg_get_integer(&sendsms_port, grp, octstr_imm("sendsms-port"));
    cfg_get_integer(&sms_max_length, grp, octstr_imm("sms-length"));

#ifdef HAVE_LIBSSL
    cfg_get_bool(&ssl, grp, octstr_imm("sendsms-port-ssl"));
#endif /* HAVE_LIBSSL */

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
	if (http_open_port(sendsms_port, ssl) == -1) {
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
	panic(0, "urltrans_create failed");
    if (urltrans_add_cfg(translations, cfg) == -1)
	panic(0, "urltrans_add_cfg failed");

    sendsms_number_chars = SENDSMS_DEFAULT_CHARS;
    caller = http_caller_create();
    smsbox_requests = list_create();
    list_add_producer(smsbox_requests);
    num_outstanding_requests = counter_create();
    catenated_sms_counter = counter_create();
    gwthread_create(obey_request_thread, NULL);
    gwthread_create(url_result_thread, NULL);

    connect_to_bearerbox(bb_host, bb_port, NULL /* bb_our_host */);
	/* XXX add our_host if required */

    heartbeat_thread = heartbeat_start(write_to_bearerbox, heartbeat_freq,
				       outstanding_requests);

    read_messages_from_bearerbox();

    info(0, "Kannel smsbox terminating.");

    heartbeat_stop(heartbeat_thread);
    http_close_all_ports();
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
    counter_destroy(num_outstanding_requests);
    counter_destroy(catenated_sms_counter);
    octstr_destroy(bb_host);
    octstr_destroy(global_sender);
    octstr_destroy(reply_emptymessage);
    octstr_destroy(reply_requestfailed);
    octstr_destroy(reply_couldnotfetch);
    octstr_destroy(reply_couldnotrepresent);
    numhash_destroy(black_list);
    numhash_destroy(white_list);
    cfg_destroy(cfg);
    gwlib_shutdown();
    return 0;
}

int charset_processing (Octstr *charset, Octstr *body, int coding) {

    int resultcode = 0;
    
    if (octstr_len(charset)) {
	debug("sms.http", 0, "enter charset, coding=%d, msgdata is %s", coding, octstr_get_cstr(body));
	octstr_dump(body, 0);

	if (coding == DC_7BIT) {
	    /*
	     * For 7 bit, convert to ISO-8859-1
	     */
	    if (octstr_recode (octstr_imm ("ISO-8859-1"), charset, body) < 0) {
		resultcode = -1;
	    }
	} else if (coding == DC_UCS2) {
	    /*
	     * For UCS2, convert to UTF-16BE
	     */
	    if (octstr_recode (octstr_imm ("UTF-16BE"), charset, body) < 0) {
		resultcode = -1;
	    }
	}
	
	debug("sms.http", 0, "exit charset, coding=%d, msgdata is %s", coding, octstr_get_cstr(body));
	octstr_dump(body, 0);
    }
    
    return resultcode;
}
