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
#include "ota_prov_attr.h"
#include "ota_prov.h"
#include "ota_compiler.h"

#ifdef HAVE_SECURITY_PAM_APPL_H
#include <security/pam_appl.h>
#endif


/*
 * Maximum number of octets in an SMS message. Note that this is 8 bit
 * characters, not 7 bit characters.
 */
enum { MAX_SMS_OCTETS = 140 };


#define SENDSMS_DEFAULT_CHARS "0123456789 +-"

#define O_DESTROY(a) { if(a) octstr_destroy(a); a = NULL; }


static Cfg *cfg;
static long bb_port;
static int bb_ssl = 0;
static long sendsms_port = 0;
static Octstr *sendsms_url = NULL;
static Octstr *sendota_url = NULL;
static Octstr *xmlrpc_url = NULL;
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
static int mo_recode = 0;
static Numhash *white_list;
static Numhash *black_list;

static List *smsbox_requests = NULL;

int charset_processing (Octstr *charset, Octstr *text, int coding);
long get_tag(Octstr *body, Octstr *tag, Octstr **value, long pos, int nostrip);

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
 * Return >= 0 for success & count of splitted sms messages, 
 * -1 for failure.  Does not destroy the msg.
 */
static int send_message(URLTranslation *trans, Msg *msg)
{
    int max_msgs;
    Octstr *header, *footer, *suffix, *split_chars;
    int catenate, msg_sequence, msg_count;
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
    msg_count = list_len(list);

    debug("sms", 0, "message length %ld, sending %d messages", 
          octstr_len(msg->sms.msgdata), msg_count);

    while ((part = list_extract_first(list)) != NULL)
	write_to_bearerbox(part);
    list_destroy(list, NULL);
    
    return msg_count;
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
    receiver->msg->sms.alt_dcs = 0;
    receiver->msg->sms.pid = 0;
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
				      int *dlr_mask, Octstr **dlr_url, 
				      Octstr **account, int *pid, int *alt_dcs)
{
    Octstr *name, *val;
    long l;

    *dlr_mask = 0;
    *dlr_url = NULL;
    *mclass = *mwi = *coding = *compress = *validity = 
	*deferred = *pid = *alt_dcs = 0;
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
	        warning(0, "Invalid UDH received in X-Kannel-UDH");
		octstr_destroy(*udh);
		*udh = NULL;
	    }
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-DLR-URL")) == 0) {
	    *dlr_url = octstr_duplicate(val);
	    octstr_strip_blanks(*dlr_url);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Account")) == 0) {
	    *account = octstr_duplicate(val);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Flash")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", coding);
	    warning(0, "Flash field used and deprecated");
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Coding")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", coding);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-PID")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", pid);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-MWI")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", mwi);
        }
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-MClass")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", mclass);
        }
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Alt-DCS")) == 0) {
    	    sscanf(octstr_get_cstr(val),"%d", alt_dcs);
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

long get_tag(Octstr *body, Octstr *tag, Octstr **value, long pos, int nostrip) {
    long start, end;
    int taglen;
    Octstr *tmp;

    tmp = octstr_create("<");
    octstr_append(tmp, tag);
    octstr_append(tmp, octstr_imm(">"));
    taglen = octstr_len(tmp);

    start = octstr_search(body, tmp, pos);
    octstr_destroy(tmp);
    if(start != -1) {
	tmp = octstr_create("</");
	octstr_append(tmp, tag);
	octstr_append(tmp, octstr_imm(">"));

	end = octstr_search(body, tmp, start);
	octstr_destroy(tmp);
	if(end != -1) {
	    octstr_destroy(*value);
	    *value = octstr_copy(body, start + taglen, end - start - taglen);
	    if(nostrip == 0) {
		octstr_strip_blanks(*value);
		debug("sms", 0, "XMLParsing: tag <%s> value <%s>", octstr_get_cstr(tag), 
			octstr_get_cstr(*value));
	    }
	    return end + taglen + 1;
	} else {
	    debug("sms", 0, "XMLParsing: end tag </%s> not found", octstr_get_cstr(tag));
	    return -1;
	}
    } else {
	debug("sms", 0, "XMLParsing: tag <%s> not found", octstr_get_cstr(tag));
	return -1;
    }
}

// requesttype = mt_reply or mt_push. for example, auth is only read on mt_push
// parse body and populate fields, including replacing body for <ud> value and
// type to text/plain
static void get_x_kannel_from_xml(int requesttype , Octstr **type, Octstr **body, 
                                  List *headers, Octstr **from,
                                  Octstr **to, Octstr **udh,
                                  Octstr **user, Octstr **pass,
                                  Octstr **smsc, int *mclass, int *mwi,
                                  int *coding, int *compress, 
                                  int *validity, int *deferred,
                                  int *dlr_mask, Octstr **dlr_url,
                                  Octstr **account, int *pid, int *alt_dcs)
{                                    

/*
 * <?xml version="1.0" encoding="xxx"?>
 * <!DOCTYPE ...
 * <message>
 *   <submit>
 *     <da><number>...</number></da>
 *     <da>...
 *     <oa><number>...</number></oa>
 *     <ud>...</ud>
 *     <udh>0123</ud>
 *     <dcs>
 *       <mclass>.</mclass>
 *       <coding>.</coding>
 *       <mwi>.</mwi>
 *       <compress>.</compress>
 *       <alt-dcs>.</alt-dcs>
 *     </dcs>
 *     <pid>..</pid>
 *     <statusrequest>
 *       <dlr-mask>..</dlr-mask>
 *       <dlr-url>...</dlr-url>
 *     </statusrequest>
 *     <from>
 *       <user>...</user> ( or username)
 *       <pass>...</pass> ( or password)
 *       <account>...</account>
 *     </from>
 *     <to>smsc-id</to>
 *   </submit>
 * </message>
 */

    Octstr *text, *tmp, *tmp2;
    long tmplong, where;
    
    tmp = tmp2 = text = NULL;

    *dlr_mask = 0;
    *dlr_url = NULL;
    *mclass = *mwi = *coding = *compress = *validity = 
	*deferred = *pid = *alt_dcs = 0;

    debug("sms", 0, "XMLParsing: XML: <%s>", octstr_get_cstr(*body));


    if(requesttype == mt_push) {
	/* auth */
	get_tag(*body, octstr_imm("from"), &tmp, 0, 0);
	if(tmp) {
	    /* user */
	    get_tag(tmp, octstr_imm("user"), user, 0, 0);
	    get_tag(tmp, octstr_imm("username"), user, 0, 0);

	    /* pass */
	    get_tag(tmp, octstr_imm("pass"), pass, 0, 0);
	    get_tag(tmp, octstr_imm("password"), pass, 0, 0);

	    /* account */
	    get_tag(tmp, octstr_imm("account"), account, 0, 0);
	    O_DESTROY(tmp);
	}

	/* to (da/number) Multiple tags */ 
	where = get_tag(*body, octstr_imm("da"), &tmp, 0, 0);
	if(tmp) {
	    get_tag(tmp, octstr_imm("number"), to, 0, 0);
	    while(tmp && where != -1) {
		O_DESTROY(tmp);
		where = get_tag(*body, octstr_imm("da"), &tmp, where, 0);
		if(tmp) {
		    get_tag(tmp, octstr_imm("number"), &tmp2, 0, 0);
		    octstr_append_char(*to, ' ');
		    octstr_append(*to, tmp2);
		    O_DESTROY(tmp2);
		}
	    }
	}
    }

    /* from (oa/number) */
    get_tag(*body, octstr_imm("oa"), &tmp, 0, 0);
    if(tmp) {
	get_tag(tmp, octstr_imm("number"), from, 0, 0);
	O_DESTROY(tmp);
    }
    
    /* udh */
    get_tag(*body, octstr_imm("udh"), &tmp, 0, 0);
    if(tmp) {
	O_DESTROY(*udh);
	*udh = octstr_duplicate(tmp);
	octstr_hex_to_binary(*udh);
	O_DESTROY(tmp);
    }

    /* smsc */
    get_tag(*body, octstr_imm("to"), smsc, 0, 0);

    /* pid */
    get_tag(*body, octstr_imm("pid"), &tmp, 0, 0);
    if(tmp) {
	if(octstr_parse_long(&tmplong, tmp, 0, 10) != -1)
	    *pid = tmplong;
	O_DESTROY(tmp);
    }

    /* dcs* (dcs/ *) */
    get_tag(*body, octstr_imm("dcs"), &tmp, 0, 0);
    if(tmp) {
	/* mclass (dcs/mclass) */
	get_tag(tmp, octstr_imm("mclass"), &tmp2, 0, 0);
	if(tmp2) {
	    if(octstr_parse_long(&tmplong, tmp2, 0, 10) != -1)
		*mclass = tmplong;
	    O_DESTROY(tmp2);
	}
	/* mwi (dcs/mwi) */
	get_tag(tmp, octstr_imm("mwi"), &tmp2, 0, 0);
	if(tmp2) {
	    if(octstr_parse_long(&tmplong, tmp2, 0, 10) != -1)
		*mwi = tmplong;
	    O_DESTROY(tmp2);
	}
	/* coding (dcs/coding) */
	get_tag(tmp, octstr_imm("coding"), &tmp2, 0, 0);
	if(tmp2) {
	    if(octstr_parse_long(&tmplong, tmp2, 0, 10) != -1)
		*coding = tmplong;
	    O_DESTROY(tmp2);
	}
	/* compress (dcs/compress) */
	get_tag(tmp, octstr_imm("compress"), &tmp2, 0, 0);
	if(tmp2) {
	    if(octstr_parse_long(&tmplong, tmp2, 0, 10) != -1)
		*compress = tmplong;
	    O_DESTROY(tmp2);
	}
	/* alt-dcs (dcs/alt-dcs) */
	get_tag(tmp, octstr_imm("alt-dcs"), &tmp2, 0, 0);
	if(tmp2) {
	    if(octstr_parse_long(&tmplong, tmp2, 0, 10) != -1)
		*alt_dcs = tmplong;
	    O_DESTROY(tmp2);
	}
	O_DESTROY(tmp);
    }

    /* statusrequest* (statusrequest/ *) */
    get_tag(*body, octstr_imm("statusrequest"), &tmp, 0, 0);
    if(tmp) {
	/* dlr-mask (statusrequest/dlr-mask) */
	get_tag(tmp, octstr_imm("dlr-mask"), &tmp2, 0, 0);
	if(tmp2) {
	    if(octstr_parse_long(&tmplong, tmp2, 0, 10) != -1)
		*dlr_mask = tmplong;
	    O_DESTROY(tmp2);
	}
	get_tag(tmp, octstr_imm("dlr-url"), dlr_url, 0, 0);
	O_DESTROY(tmp);
    }

    /* validity (vp/delay) */
    get_tag(*body, octstr_imm("vp"), &tmp, 0, 0);
    if(tmp) {
	get_tag(tmp, octstr_imm("delay"), &tmp2, 0, 0);
	if(tmp2) {
	    if(octstr_parse_long(&tmplong, tmp2, 0, 10) != -1)
		*validity = tmplong;
	    O_DESTROY(tmp2);
	}
	O_DESTROY(tmp);
    }

    /* deferred (timing/delay) */
    get_tag(*body, octstr_imm("timing"), &tmp, 0, 0);
    if(tmp) {
	get_tag(tmp, octstr_imm("delay"), &tmp2, 0, 0);
	if(tmp2) {
	    if(octstr_parse_long(&tmplong, tmp2, 0, 10) != -1)
		*deferred = tmplong;
	    O_DESTROY(tmp2);
	}
	O_DESTROY(tmp);
    }

    /* text */
    get_tag(*body, octstr_imm("ud"), &text, 0, 0);
    get_tag(*body, octstr_imm("ud-raw"), &text, 0, 1);

    octstr_truncate(*body, 0);
    octstr_append(*body, text);
    O_DESTROY(text);
    
    O_DESTROY(*type);
    *type = octstr_create("text/plain");
}


static void fill_message(Msg *msg, URLTranslation *trans,
			 Octstr *replytext, int octet_stream,
			 Octstr *from, Octstr *to, Octstr *udh, 
			 int mclass, int mwi, int coding, int compress,
			 int validity, int deferred,
			 Octstr *dlr_url, int dlr_mask, int pid, int alt_dcs,
			 Octstr *smsc)
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

    if (smsc != NULL) {
	if (urltrans_accept_x_kannel_headers(trans)) {
	    octstr_destroy(msg->sms.smsc_id);
	    msg->sms.smsc_id = smsc;
	} else {
	    warning(0, "Tried to change SMSC to '%s', denied.",
		    octstr_get_cstr(smsc));
	    octstr_destroy(smsc);
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
	    O_DESTROY(udh);
	}
    }
    if (mclass) {
        if (urltrans_accept_x_kannel_headers(trans))
	    msg->sms.mclass = mclass;	  
	else 
	    warning(0, "Tried to set MClass field, denied.");
    }
    if (pid) {
        if (urltrans_accept_x_kannel_headers(trans))
	    msg->sms.pid = pid;	  
	else 
	    warning(0, "Tried to set PID field, denied.");
    }
    if (alt_dcs) {
        if (urltrans_accept_x_kannel_headers(trans))
	    msg->sms.alt_dcs = alt_dcs;	  
	else 
	    warning(0, "Tried to set Alt-DCS field, denied.");
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
    Octstr *text_xml;
    Octstr *octet_stream;
    Octstr *udh, *from, *to;
    Octstr *dlr_url;
    Octstr *account;
    Octstr *smsc;
    int dlr_mask;
    int octets;
    int mclass, mwi, coding, compress, pid, alt_dcs;
    int validity, deferred;
    
    dlr_mask = 0;
    dlr_url = NULL;
    text_html = octstr_imm("text/html");
    text_wml = octstr_imm("text/vnd.wap.wml");
    text_plain = octstr_imm("text/plain");
    text_xml = octstr_imm("text/xml");
    octet_stream = octstr_imm("application/octet-stream");

    for (;;) {
    	id = http_receive_result(caller, &status, &final_url, &reply_headers,
	    	    	    	 &reply_body);
    	if (id == NULL)
	    break;
    	
    	get_receiver(id, &msg, &trans);

    	from = to = udh = smsc = NULL;
	octets = mclass = mwi = coding = compress = pid = alt_dcs = 0;
	validity = deferred = 0;
	account = NULL;
	
    	if (status == HTTP_OK || status == HTTP_ACCEPTED) {
	    http_header_get_content_type(reply_headers, &type, &charset);
	    if (octstr_case_compare(type, text_html) == 0 ||
		octstr_case_compare(type, text_wml) == 0) {
		strip_prefix_and_suffix(reply_body,
					urltrans_prefix(trans), 
					urltrans_suffix(trans));
		replytext = html_to_sms(reply_body);
		octstr_strip_blanks(replytext);
    	    	get_x_kannel_from_headers(reply_headers, &from, &to, &udh,
					  NULL, NULL, &smsc, &mclass, &mwi, 
					  &coding, &compress, &validity, 
					  &deferred, &dlr_mask, &dlr_url, 
					  &account, &pid, &alt_dcs);
	    } else if (octstr_case_compare(type, text_plain) == 0) {
		replytext = octstr_duplicate(reply_body);
                octstr_destroy(reply_body);
		reply_body = NULL;
		// XXX full text octstr_strip_blanks(replytext);
    	    	get_x_kannel_from_headers(reply_headers, &from, &to, &udh,
					  NULL, NULL, &smsc, &mclass, &mwi, 
					  &coding, &compress, &validity, 
					  &deferred, &dlr_mask, &dlr_url, 
					  &account, &pid, &alt_dcs);
	    } else if (octstr_case_compare(type, text_xml) == 0) {
		replytext = octstr_duplicate(reply_body);
		octstr_destroy(reply_body); 
		reply_body = NULL;
		get_x_kannel_from_xml(mt_reply, &type, &replytext, reply_headers, &from, &to, &udh,
				NULL, NULL, &smsc, &mclass, &mwi, &coding,
				&compress, &validity, &deferred,
				&dlr_mask, &dlr_url, &account, &pid, &alt_dcs);
	    } else if (octstr_case_compare(type, octet_stream) == 0) {
		replytext = octstr_duplicate(reply_body);
                octstr_destroy(reply_body);
		octets = 1;
		reply_body = NULL;
    	    	get_x_kannel_from_headers(reply_headers, &from, &to, &udh,
					  NULL, NULL, &smsc, &mclass, &mwi, 
					  &coding, &compress, &validity, 
					  &deferred, &dlr_mask, &dlr_url, 
					  &account, &pid, &alt_dcs);
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
			dlr_mask, pid, alt_dcs, smsc);

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
    Octstr *pattern, *xml, *tmp;
    List *request_headers;
    void *id;
    struct tm tm;
    char p[22];
    int type;
    FILE *f;
    
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
    
    case TRANSTYPE_EXECUTE:
        debug("sms.exec", 0, "executing sms-service '%s'", 
              octstr_get_cstr(pattern));
        if ((f = popen(octstr_get_cstr(pattern), "r")) != NULL) {
            octstr_destroy(pattern);
            *result = octstr_read_pipe(f);
            pclose(f);
            alog("SMS request sender:%s request: '%s' file answer: '%s'",
                octstr_get_cstr(msg->sms.receiver),
                octstr_get_cstr(msg->sms.msgdata),
                octstr_get_cstr(*result));
        } else {
            error(0, "popen failed for '%s': %d: %s",
                  octstr_get_cstr(pattern), errno, strerror(errno));
            *result = NULL;
            octstr_destroy(pattern);
            goto error;
        }
        break;

    case TRANSTYPE_GET_URL:
	request_headers = http_create_empty_headers();
        http_header_add(request_headers, "User-Agent", GW_NAME " " VERSION);
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
	http_header_add(request_headers, "User-Agent", GW_NAME " " VERSION);
	id = remember_receiver(msg, trans);
	/* XXX Which header should we use for UCS2 ? octstr also ? */
	/* XXX UCS2 should be text/ *, charset=UTF16-BE ? */
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
	if (octstr_len(msg->sms.smsc_id)) {
	    Octstr *os;
	    os = octstr_duplicate(msg->sms.smsc_id);
	    http_header_add(request_headers, "X-Kannel-SMSC",
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
	if(msg->sms.pid) {
	    Octstr *os;
	    os = octstr_format("%d",msg->sms.pid);
	    http_header_add(request_headers, "X-Kannel-PID", 
	    	octstr_get_cstr(os));
	    octstr_destroy(os);
	}
	if(msg->sms.alt_dcs) {
	    Octstr *os;
	    os = octstr_format("%d",msg->sms.alt_dcs);
	    http_header_add(request_headers, "X-Kannel-Alt-DCS", 
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

    case TRANSTYPE_POST_XML:

/* XXX The first two chars are beeing eaten somewhere and
 * only sometimes - something must be ungry */

#define OCTSTR_APPEND_XML(xml, tag, text)                  \
	octstr_append(xml, octstr_imm("  "));        \
	octstr_append(xml, octstr_imm("\t\t<"));        \
	octstr_append(xml, octstr_imm(tag));            \
	octstr_append(xml, octstr_imm(">"));            \
	octstr_append(xml, text);                       \
	octstr_append(xml, octstr_imm("</"));           \
	octstr_append(xml, octstr_imm(tag));            \
	octstr_append(xml, octstr_imm(">\n"));

#define OCTSTR_APPEND_XML_NUMBER(xml, tag, value)          \
	octstr_append(xml, octstr_imm("  "));        \
	octstr_append(xml, octstr_imm("\t\t<"));        \
	octstr_append(xml, octstr_imm(tag));            \
	octstr_append(xml, octstr_imm(">"));            \
	octstr_append_decimal(xml, value);              \
	octstr_append(xml, octstr_imm("</"));           \
	octstr_append(xml, octstr_imm(tag));            \
	octstr_append(xml, octstr_imm(">\n"));

	request_headers = http_create_empty_headers();
	http_header_add(request_headers, "User-Agent", GW_NAME " " VERSION);
	id = remember_receiver(msg, trans);

	http_header_add(request_headers, "Content-Type", "text/xml");

	xml = octstr_create("");
	octstr_append(xml, octstr_imm("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n")); 
	octstr_append(xml, octstr_imm("<!DOCTYPE message SYSTEM \"SMSmessage.dtd\">\n")); 

	octstr_append(xml, octstr_imm("<message cid=\"1\">\n"));
	octstr_append(xml, octstr_imm("\t<submit>\n"));

	/* oa */
	if(urltrans_send_sender(trans)) {
	    tmp = octstr_create("");
	    OCTSTR_APPEND_XML(tmp, "number", msg->sms.receiver);
	    OCTSTR_APPEND_XML(xml, "oa", tmp);
	    octstr_destroy(tmp);
	}

	/* da */
	{
	    tmp = octstr_create("");
	    OCTSTR_APPEND_XML(tmp, "number", msg->sms.sender);
	    OCTSTR_APPEND_XML(xml, "da", tmp);
	    octstr_destroy(tmp);
	}

	/* udh */
	if(octstr_len(msg->sms.udhdata)) {
	    Octstr *t;
	    t = octstr_duplicate(msg->sms.udhdata);
	    octstr_binary_to_hex(t, 1);
	    OCTSTR_APPEND_XML(xml, "udh", t);
	    octstr_destroy(t);
	}

	/* ud */
	if(octstr_len(msg->sms.msgdata)) {
	    OCTSTR_APPEND_XML(xml, "ud", msg->sms.msgdata);
	}

	/* pid */
	if(msg->sms.pid != 0) {
	    OCTSTR_APPEND_XML_NUMBER(xml, "pid", msg->sms.pid);
	}

	/* dcs */
	{
	    tmp = octstr_create("");

	    if(msg->sms.coding != 0) { 
		OCTSTR_APPEND_XML_NUMBER(tmp, "coding", msg->sms.coding);
	    }
	    if(msg->sms.mclass != 0) {
		OCTSTR_APPEND_XML_NUMBER(tmp, "mclass", msg->sms.mclass);
	    }
	    if(msg->sms.alt_dcs != 0) {
		OCTSTR_APPEND_XML_NUMBER(tmp, "alt-dcs", msg->sms.alt_dcs);
	    }
	    if(msg->sms.mwi != 0) {
		OCTSTR_APPEND_XML_NUMBER(tmp, "mwi", msg->sms.mwi);
	    }
	    if(msg->sms.compress != 0) {
		OCTSTR_APPEND_XML_NUMBER(tmp, "compress", msg->sms.compress);
	    }

	    if(octstr_len(tmp)) {
		OCTSTR_APPEND_XML(xml, "dcs", tmp)
	    }
	    octstr_destroy(tmp);
	}

	/* XXX timing = deferred */
	/* XXX vp = validity */

	/* at */
	tm = gw_gmtime(msg->sms.time);
	tmp = octstr_format("<year>%04d</year><month>%02d</month>"
			"<day>%02d</day><hour>%02d</hour><minute>%02d</minute>"
			"<second>%02d</second><timezone>0</timezone>",
		       	tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		       	tm.tm_hour, tm.tm_min, tm.tm_sec);
	OCTSTR_APPEND_XML(xml, "at", tmp);
	octstr_destroy(tmp);

	/* smsc = from */
	if(octstr_len(msg->sms.smsc_id)) {
	    OCTSTR_APPEND_XML(xml, "from", msg->sms.smsc_id);
	}

	/* service = to */
	if(octstr_len(msg->sms.service)) {
	    OCTSTR_APPEND_XML(xml, "to", msg->sms.service);
	}

	/* End XML */
	octstr_append(xml, octstr_imm("\t</submit>\n"));
	octstr_append(xml, octstr_imm("</message>\n"));

	if(msg->sms.msgdata != NULL)
	    octstr_destroy(msg->sms.msgdata);

	msg->sms.msgdata = xml;

	debug("sms", 0, "XMLBuild: XML: <%s>", octstr_get_cstr(msg->sms.msgdata));
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


	/* Recode to iso-8859-1 the MO message if possible */
	if(mo_recode && msg->sms.coding == DC_UCS2) {
	    Octstr *text;
	    text = octstr_duplicate(msg->sms.msgdata);

	    if(0 == octstr_recode (octstr_imm("iso-8859-1"), octstr_imm("UTF-16BE"), text)) {
		if(octstr_search(text, octstr_imm("&#"), 0) == -1) {
		    /* XXX I'm trying to search for &#xxxx; text, which indicates that the
		     * text couldn't be recoded.
		     * We should use other function to do the recode or detect it using
		     * other method */
		    info(0, "MO message converted from UCS2 to ISO-8859-1");
		    octstr_destroy(msg->sms.msgdata);
		    msg->sms.msgdata = octstr_duplicate(text);
		    msg->sms.charset = octstr_create("ISO-8859-1");
		    msg->sms.coding = DC_7BIT;
		}
	    }
	    octstr_destroy(text);
	}

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
	        reply = octstr_duplicate(reply_requestfailed);
	        trans = NULL;	/* do not use any special translation */
	    }
	    octstr_destroy(msg->sms.msgdata);
	    msg->sms.msgdata = reply;
	    if(msg->sms.service == NULL && trans != NULL)
		msg->sms.service = octstr_duplicate(urltrans_name(trans));
	
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
    info(0, "sendsms used by <%s>", login);
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
				 int *status, int dlr_mask, Octstr *dlr_url, 
				 Octstr *account, int pid, int alt_dcs)
{				     
    Msg *msg = NULL;
    Octstr *newfrom, *returnerror, *receiv;
    List *receiver, *failed_id, *allowed, *denied;
    int no_recv, ret = 0, i;
    long del;

    /*
     * Multi-cast messages with several receivers in 'to' are handled
     * in a loop. We only change sms.time and sms.receiver within the
     * loop below, because everything else is identical for all receivers.
     */
    receiver = octstr_split_words(to);
    no_recv = list_len(receiver);

    /*
     * check if UDH length is legal, or otherwise discard the
     * message, to prevent intentional buffer overflow schemes
     */
    if (udh != NULL && (octstr_len(udh) != octstr_get_char(udh, 0) + 1)) {
	returnerror = octstr_create("UDH field misformed, rejected");
	goto fielderror2;
    }

    /*
     * Check if there are any illegal characters in the 'to' scheme
     */
    if (strspn(octstr_get_cstr(to), sendsms_number_chars) < octstr_len(to)) {
	info(0,"Illegal characters in 'to' string ('%s') vs '%s'",
	     octstr_get_cstr(to), sendsms_number_chars);
	returnerror = octstr_create("Garbage 'to' field, rejected.");
	goto fielderror2;
    }

    /*
     * Check for white and black lists, first for the URLTranlation
     * lists and then for the global lists.
     *
     * Set the 'allowed' and 'denied' lists accordingly to process at
     * least all allowed receiver messages. This is a constrain
     * walk through all disallowing rules within the lists.
     */
    allowed = list_create();
    denied = list_create();

    for (i = 0; i < no_recv; i++) {
        receiv = list_get(receiver, i); 
            
        /*
         * First of all fill the two lists systematicaly by the rules,
         * then we will revice the lists.
         */
        if (urltrans_white_list(t) &&
            numhash_find_number(urltrans_white_list(t), receiv) < 1) {
            info(0, "Number <%s> is not in white-list, message discarded",
                 octstr_get_cstr(receiv));
            list_append_unique(denied, receiv, octstr_item_match);
        } else {
            list_append_unique(allowed, receiv, octstr_item_match);
        }
        if (urltrans_black_list(t) &&
            numhash_find_number(urltrans_black_list(t), receiv) == 1) {
            info(0, "Number <%s> is in black-list, message discarded",
                 octstr_get_cstr(receiv));
            list_append_unique(denied, receiv, octstr_item_match);
        } else {
            list_append_unique(allowed, receiv, octstr_item_match);
        }
        if (white_list &&
            numhash_find_number(white_list, receiv) < 1) {
            info(0, "Number <%s> is not in global white-list, message discarded",
                 octstr_get_cstr(receiv));
            list_append_unique(denied, receiv, octstr_item_match);
        } else {
            list_append_unique(allowed, receiv, octstr_item_match);
        }
        if (black_list &&
            numhash_find_number(black_list, receiv) == 1) {
            info(0, "Number <%s> is in global black-list, message discarded",
                 octstr_get_cstr(receiv));
            list_append_unique(denied, receiv, octstr_item_match);
        } else {
            list_append_unique(allowed, receiv, octstr_item_match);
        }
    }
    
    /*
     * Now we have to revise the 'allowed' and 'denied' lists by walking
     * the 'denied' list and check if items are also present in 'allowed',
     * then we will discard them from 'allowed'.
     */
    for (i = 0; i < list_len(denied); i++) {
        receiv = list_get(denied, i);
        del = list_delete_matching(allowed, receiv, octstr_item_match);
    }
    
    if (urltrans_faked_sender(t) != NULL) {
	/* discard previous from */
	newfrom = octstr_duplicate(urltrans_faked_sender(t));
    } else if (octstr_len(from) > 0) {
	newfrom = octstr_duplicate(from);
    } else if (urltrans_default_sender(t) != NULL) {
	newfrom = octstr_duplicate(urltrans_default_sender(t));
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
         ( text == NULL ? "" : octstr_get_cstr(text) ));
    
    /*
     * Create the msg structure and fill the types. Note that sms.receiver
     * and sms.time are set in the multi-cast support loop below.
     */
    msg = msg_create(sms);
    
    msg->sms.service = octstr_duplicate(urltrans_name(t));
    msg->sms.sms_type = mt_push;
    msg->sms.sender = octstr_duplicate(newfrom);
    msg->sms.account = account ? octstr_duplicate(account) : NULL;
    msg->sms.msgdata = text ? octstr_duplicate(text) : octstr_create("");
    msg->sms.udhdata = udh ? octstr_duplicate(udh) : octstr_create("");
    msg->sms.dlr_mask = dlr_mask;
    msg->sms.dlr_url = dlr_url ? octstr_duplicate(dlr_url) : octstr_create("");

    if ( mclass < 0 || mclass > 4 ) {
	returnerror = octstr_create("MClass field misformed, rejected");
	goto fielderror;
    }
    msg->sms.mclass = mclass;
    
    if ( pid < 0 || pid > 255 ) {
	returnerror = octstr_create("PID field misformed, rejected");
	goto fielderror;
    }
    msg->sms.pid = pid;
    
    if ( alt_dcs < 0 || alt_dcs > 2 ) {
	returnerror = octstr_create("Alt-DCS field misformed, rejected");
	goto fielderror;
    }
    msg->sms.alt_dcs = alt_dcs;
    
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

    /* 
     * All checks are done, now add multi-cast request support by
     * looping through 'allowed'. This should work for any
     * number of receivers within 'to'. If the message fails append
     * it to 'failed_id'.
     */
    failed_id = list_create();

    while ((receiv = list_extract_first(allowed)) != NULL) {
        
        msg->sms.receiver = octstr_duplicate(receiv);
        msg->sms.time = time(NULL);
        /* send the message and return number of splits */
        ret = send_message(t, msg);

        if (ret == -1) {
            /* add the receiver to the failed list */
            list_append(failed_id, receiv);
        } else {
            /* log the sending as successfull for this particular message */
            alog("send-SMS request added - sender:%s:%s %s target:%s request: '%s'",
	             octstr_get_cstr(urltrans_username(t)),
                 octstr_get_cstr(newfrom), octstr_get_cstr(client_ip),
	             octstr_get_cstr(receiv),
	             udh == NULL ? ( text == NULL ? "" : octstr_get_cstr(text) ) : "<< UDH >>");
        }
    }
    msg_destroy(msg);
    list_destroy(receiver, octstr_destroy_item);
    list_destroy(allowed, octstr_destroy_item);

    /* have all receivers been denied by list rules?! */
    if (no_recv == list_len(denied)) {
        returnerror = octstr_create("Number(s) has/have been denied by white- and/or black-lists.");
        goto fielderror2;
    }

    if (list_len(failed_id) > 0)
	goto error;
    
    list_destroy(failed_id, octstr_destroy_item);
    octstr_destroy(newfrom);
    *status = HTTP_ACCEPTED;
    returnerror = octstr_create("Sent.");

    /* 
     * Append all denied receivers to the returned body in case this is
     * a multi-cast send request
     */
    if (list_len(denied) > 0) {
        octstr_format_append(returnerror, " Denied receivers are:");
        while ((receiv = list_extract_first(denied)) != NULL) {
            octstr_format_append(returnerror, " %s", octstr_get_cstr(receiv));
        }
    }               
    list_destroy(denied, octstr_destroy_item);  

    /*
     * Append number of splits to returned body. 
     * This may be used by the calling client.
     */
    if (ret > 1) 
        octstr_format_append(returnerror, " Message splits: %d", ret);

    return returnerror;
    

fielderror:
    octstr_destroy(newfrom);
    msg_destroy(msg);

fielderror2:
    alog("send-SMS request failed - %s",
         octstr_get_cstr(returnerror));

    *status = HTTP_BAD_REQUEST;
    return returnerror;

error:
    error(0, "sendsms_request: failed");
    octstr_destroy(from);
    *status = HTTP_INTERNAL_SERVER_ERROR;
    returnerror = octstr_create("Sending failed.");

    /* 
     * Append all receivers to the returned body in case this is
     * a multi-cast send request
     */
    if (no_recv > 1) {
        octstr_format_append(returnerror, " Failed receivers are:");
        while ((receiv = list_extract_first(failed_id)) != NULL) {
            octstr_format_append(returnerror, " %s", octstr_get_cstr(receiv));
        }
    }

    octstr_destroy(receiv); 
    list_destroy(failed_id, octstr_destroy_item);
    list_destroy(denied, octstr_destroy_item);
    return returnerror;
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
    Octstr *account = NULL;
    int	dlr_mask = 0;
    Octstr *dlr_mask_string;
    int mclass, mwi, coding, compress, validity, deferred, pid, alt_dcs;
   
    /* check the username and password */
    t = authorise_user(args, client_ip);
    if (t == NULL) {
	*status = HTTP_FORBIDDEN;
	return octstr_create("Authorization failed for sendsms");
    }
    
    udh = http_cgi_variable(args, "udh");
    text = http_cgi_variable(args, "text");
    charset = http_cgi_variable(args, "charset");
    smsc = http_cgi_variable(args, "smsc");
    from = http_cgi_variable(args, "from");
    to = http_cgi_variable(args, "to");
    account = http_cgi_variable(args,"account");
    dlr_url = http_cgi_variable(args, "dlrurl");
    dlr_mask_string = http_cgi_variable(args, "dlrmask");

    if(dlr_mask_string != NULL)
        sscanf(octstr_get_cstr(dlr_mask_string),"%d",&dlr_mask);
    else
    	dlr_mask = 0;

    mclass = mwi = coding = compress = validity = 
	deferred = pid = alt_dcs = 0;

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
    tmp_string = http_cgi_variable(args, "pid");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &pid);

    tmp_string = NULL;
    tmp_string = http_cgi_variable(args, "alt-dcs");
    if(tmp_string != NULL)
        sscanf(octstr_get_cstr(tmp_string),"%d", &alt_dcs);

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

    /*
     * we expect cgi var text to be defined, even if it may be
     * empty to allow empty messages, st.
     */
    if (to == NULL) {
	error(0, "%s got insufficient headers",octstr_get_cstr(sendsms_url));
	*status = HTTP_BAD_REQUEST;
	return octstr_create("Insufficient headers, rejected");
    } 
    else if (octstr_case_compare(to,
				octstr_imm("")) == 0) {
	error(0, "%s got empty to cgi variable", octstr_get_cstr(sendsms_url));
	*status = HTTP_BAD_REQUEST;
	return octstr_create("Empty receiver number not allowed, rejected");
    }

    return smsbox_req_handle(t, client_ip, from, to, text, charset, udh, 
			     smsc, mclass, mwi, coding, compress, validity, 
			     deferred, status, dlr_mask, dlr_url, account,
			     pid, alt_dcs);
    
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
    Octstr *account;
    int dlr_mask = 0;
    int mclass, mwi, coding, compress, validity, deferred, pid, alt_dcs;
 
    from = to = user = pass = udh = smsc = dlr_url = account = NULL;
   
    ret = NULL;
    
    /* XXX here we should take into account content-type of body
    */
    http_header_get_content_type(headers, &type, &charset);
    if(octstr_case_compare(type, octstr_imm("text/xml")) == 0) {
	get_x_kannel_from_xml(mt_push, &type, &body, headers, &from, &to, &udh,
		       	&user, &pass, &smsc, &mclass, &mwi, &coding,
		       	&compress, &validity, &deferred,
		       	&dlr_mask, &dlr_url, &account, &pid, &alt_dcs);
    } else {
	get_x_kannel_from_headers(headers, &from, &to, &udh,
			      &user, &pass, &smsc, &mclass, &mwi, &coding,
			      &compress, &validity, &deferred, 
			      &dlr_mask, &dlr_url, &account, &pid, &alt_dcs);
    }

    /* check the username and password */
    t = authorise_username(user, pass, client_ip);
    if (t == NULL) {
	*status = HTTP_FORBIDDEN;
	ret = octstr_create("Authorization failed for sendsms");
    }
    else if (to == NULL) {
	error(0, "%s got insufficient headers", octstr_get_cstr(sendsms_url));
	*status = HTTP_BAD_REQUEST;
	ret = octstr_create("Insufficient headers, rejected");
    } 
    else if (octstr_case_compare(to,
				octstr_imm("")) == 0) {
	error(0, "%s got empty to cgi variable", octstr_get_cstr(sendsms_url));
	*status = HTTP_BAD_REQUEST;
	return octstr_create("Empty receiver number not allowed, rejected");
    } 
    else {
	if (octstr_case_compare(type,
				octstr_imm("application/octet-stream")) == 0) {
	    if (coding == DC_UNDEF)
		coding = DC_8BIT; /* XXX Force UCS2 with DC Field */
	} else if (octstr_case_compare(type,
				       octstr_imm("text/plain")) == 0) {
	    if (coding == DC_UNDEF)
		coding = DC_7BIT;
	} else {
	    error(0, "%s got weird content type %s", octstr_get_cstr(sendsms_url),
		  octstr_get_cstr(type));
	    *status = HTTP_UNSUPPORTED_MEDIA_TYPE;
	    ret = octstr_create("Unsupported content-type, rejected");
	}

	if (ret == NULL)
	    ret = smsbox_req_handle(t, client_ip, from, to, body, charset,
				    udh, smsc, mclass, mwi, coding, compress, 
				    validity, deferred, status, 
				    dlr_mask, dlr_url, account, pid, alt_dcs);

	octstr_destroy(type);
	octstr_destroy(charset);
    }
    octstr_destroy(from);
    octstr_destroy(to);
    octstr_destroy(user);
    octstr_destroy(pass);
    octstr_destroy(udh);
    octstr_destroy(smsc);
    octstr_destroy(dlr_url);
    octstr_destroy(account);
    return ret;
}


/*
 * Create and send an SMS message from an XML-RPC request.
 * Answer with a valid XML-RPC response for a successfull request.
 * 
 * function signature: boolean sms.send(struct)
 * 
 * The <struct> MUST contain at least <member>'s with name 'username',
 * 'password', 'to' and MAY contain additional <member>'s with name
 * 'from', 'account', 'smsc', 'udh', 'dlrmask', 'dlrurl'. All values
 * are of type string.
 */
static Octstr *smsbox_xmlrpc_post(List *headers, Octstr *body,
                                  Octstr *client_ip, int *status)
{
    Octstr *from, *to, *user, *pass, *udh, *smsc;
    Octstr *ret;
    Octstr *type, *charset;
    Octstr *dlr_url;
    Octstr *account;
    Octstr *output;
    Octstr *method_name;
    XMLRPCMethodCall *msg;

    from = to = user = pass = udh = smsc = dlr_url = account = NULL;
    ret = NULL;

    /*
     * check if the content type is valid for this request
     */
    http_header_get_content_type(headers, &type, &charset);
    if (octstr_case_compare(type, octstr_imm("text/xml")) != 0) {
        error(0, "Unsupported content-type '%s'", octstr_get_cstr(type));
        *status = HTTP_BAD_REQUEST;
        ret = octstr_format("Unsupported content-type '%s'", octstr_get_cstr(type));
    } else {

        /*
         * parse the body of the request and check if it is a valid XML-RPC
         * structure
         */
        msg = xmlrpc_call_parse(body);

        if ((xmlrpc_parse_status(msg) != XMLRPC_COMPILE_OK) && 
            ((output = xmlrpc_parse_error(msg)) != NULL)) {
            /* parse failure */
            error(0, "%s", octstr_get_cstr(output));
            *status = HTTP_BAD_REQUEST;
            ret = octstr_format("%s", octstr_get_cstr(output));
            octstr_destroy(output);
        } else {

            /*
             * at least the structure has been valid, now check for the
             * required methodName and the required variables
             */
            if (octstr_case_compare((method_name = xmlrpc_get_method_name(msg)), 
                                    octstr_imm("sms.send")) != 0) {
                error(0, "Unknown method name '%s'", octstr_get_cstr(method_name));
                *status = HTTP_BAD_REQUEST;
                ret = octstr_format("Unkown method name '%s'", 
                                    octstr_get_cstr(method_name));
            } else {

                /*
                 * check for the required struct members
                 */

            }
        }

        xmlrpc_call_destroy(msg);
    }
    
    return ret;
}


/*
 * Create and send an SMS OTA (auto configuration) message from an HTTP 
 * request. If cgivar "text" is present, use it as a xml configuration source,
 * otherwise read the configuration from the configuration file.
 * Args: list contains the CGI parameters
 */
static Octstr *smsbox_req_sendota(List *list, Octstr *client_ip, int *status)
{
    Octstr *id, *from, *phonenumber, *smsc, *ota_doc, *doc_type;
    CfgGroup *grp;
    List *grplist;
    Octstr *p;
    URLTranslation *t;
    Msg *msg;
    int ret, ota_type;
    
    id = phonenumber = smsc = NULL;

    /* check the username and password */
    t = authorise_user(list, client_ip);
    if (t == NULL) {
	*status = HTTP_FORBIDDEN;
	return octstr_create("Authorization failed for sendota");
    }
    
    phonenumber = http_cgi_variable(list, "phonenumber");
    if (phonenumber == NULL) {
	error(0, "%s needs a valid phone number.", octstr_get_cstr(sendota_url));
	*status = HTTP_BAD_REQUEST;
	return octstr_create("Wrong sendota args.");
    }

    if (urltrans_faked_sender(t) != NULL) {
	from = octstr_duplicate(urltrans_faked_sender(t));
    } else if ((from = http_cgi_variable(list, "from")) != NULL &&
	       octstr_len(from) > 0) {
	from = octstr_duplicate(from);
    } else if (urltrans_default_sender(t) != NULL) {
	from = octstr_duplicate(urltrans_default_sender(t));
    } else if (global_sender != NULL) {
	from = octstr_duplicate(global_sender);
    } else {
	*status = HTTP_BAD_REQUEST;
	return octstr_create("Sender missing and no global set, rejected");
    }
        
    /* check does we have an external XML source for configuration */
    if ((ota_doc = http_cgi_variable(list, "text")) != NULL) {
        
        /*
         * We are doing the XML OTA compiler mode for this request
         */
        debug("sms", 0, "OTA service with XML document");
        ota_doc = octstr_duplicate(ota_doc);
        if ((doc_type = http_cgi_variable(list, "type")) == NULL) {
	    doc_type = octstr_format("%s", "settings");
        } else {
	    doc_type = octstr_duplicate(doc_type);
        }

        if ((ret = ota_pack_message(&msg, ota_doc, doc_type, from, 
                                phonenumber)) < 0) {
            *status = HTTP_BAD_REQUEST;
            msg_destroy(msg);
            if (ret == -2)
                return octstr_create("Erroneous document type, cannot"
                                     " compile\n");
            else if (ret == -1)
	        return octstr_create("Erroneous ota source, cannot compile\n");
        }

        goto send;

    } else {

        /* 
         * We are doing the ota-settings or ota-bookmark group mode
         * for this request.
         *
         * Check if a ota-setting ID has been given and decide which OTA
         * properties to be send to the client otherwise try to find a
         * ota-bookmark ID. If none is found then send the default 
         * ota-setting group, which is the first within the config file.
         */
        id = http_cgi_variable(list, "otaid");
    
        grplist = cfg_get_multi_group(cfg, octstr_imm("ota-setting"));
        while (grplist && (grp = list_extract_first(grplist)) != NULL) {
            p = cfg_get(grp, octstr_imm("ota-id"));
            if (id == NULL || (p != NULL && octstr_compare(p, id) == 0)) {
                ota_type = 1;
                goto found;
            }
            octstr_destroy(p);
        }
        list_destroy(grplist, NULL);
        
        grplist = cfg_get_multi_group(cfg, octstr_imm("ota-bookmark"));
        while (grplist && (grp = list_extract_first(grplist)) != NULL) {
            p = cfg_get(grp, octstr_imm("ota-id"));
            if (id == NULL || (p != NULL && octstr_compare(p, id) == 0)) {
                ota_type = 0;             
                goto found;
            }
            octstr_destroy(p);
        }
        list_destroy(grplist, NULL);
        
        if (id != NULL)
            error(0, "%s can't find any ota-setting or ota-bookmark group with ota-id '%s'.", 
                 octstr_get_cstr(sendota_url), octstr_get_cstr(id));
        else
	       error(0, "%s can't find any ota-setting group.", octstr_get_cstr(sendota_url));
        octstr_destroy(from);
        *status = HTTP_BAD_REQUEST;
        return octstr_create("Missing ota-setting or ota-bookmark group.");
    }
    
found:
    octstr_destroy(p);
    list_destroy(grplist, NULL);

    /* tokenize the OTA settings or bookmarks group and return the message */
    if (ota_type)
        msg = ota_tokenize_settings(grp, from, phonenumber);
    else
        msg = ota_tokenize_bookmarks(grp, from, phonenumber);

send: 
    /* we still need to check if smsc is forced for this */
    smsc = http_cgi_variable(list, "smsc");
    if (urltrans_forced_smsc(t)) {
        msg->sms.smsc_id = octstr_duplicate(urltrans_forced_smsc(t));
        if (smsc)
            info(0, "send-sms request smsc id ignored, as smsc id forced to %s",
                 octstr_get_cstr(urltrans_forced_smsc(t)));
    } else if (smsc) {
        msg->sms.smsc_id = octstr_duplicate(smsc);
    } else if (urltrans_default_smsc(t)) {
        msg->sms.smsc_id = octstr_duplicate(urltrans_default_smsc(t));
    } else
        msg->sms.smsc_id = NULL;

    octstr_dump(msg->sms.msgdata, 0);

    info(0, "%s <%s> <%s>", octstr_get_cstr(sendota_url), 
    	 id ? octstr_get_cstr(id) : "<default>", octstr_get_cstr(phonenumber));
    
    ret = send_message(t, msg); 
    msg_destroy(msg);

    if (ret == -1) {
        error(0, "sendota_request: failed");
        *status = HTTP_INTERNAL_SERVER_ERROR;
        return octstr_create("Sending failed.");
    }

    *status = HTTP_ACCEPTED;
    return octstr_create("Sent.");
}


/*
 * Create and send an SMS OTA (auto configuration) message from an HTTP POST 
 * request. Take the X-Kannel-foobar HTTP headers as parameter information.
 * Args: list contains the CGI parameters
 *
 * We still care about passed GET variable, in case the X-Kannel-foobar
 * parameters are not used but the POST contains the XML body itself.
 */
static Octstr *smsbox_sendota_post(List *args, List *headers, Octstr *body,
                                   Octstr *client_ip, int *status)
{
    Octstr *name, *val, *ret;
    Octstr *from, *to, *id, *user, *pass, *smsc;
    Octstr *type, *charset, *doc_type, *ota_doc;
    URLTranslation *t;
    Msg *msg;
    long l;
    int r;

    id = from = to = user = pass = smsc = NULL;
    doc_type = ota_doc = NULL;

    /* 
     * process all special HTTP headers 
     */
    for (l = 0; l < list_len(headers); l++) {
	http_header_get(headers, l, &name, &val);

	if (octstr_case_compare(name, octstr_imm("X-Kannel-OTA-ID")) == 0) {
	    id = octstr_duplicate(val);
	    octstr_strip_blanks(id);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-From")) == 0) {
	    from = octstr_duplicate(val);
	    octstr_strip_blanks(from);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-To")) == 0) {
	    to = octstr_duplicate(val);
	    octstr_strip_blanks(to);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Username")) == 0) {
		user = octstr_duplicate(val);
		octstr_strip_blanks(user);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-Password")) == 0) {
		pass = octstr_duplicate(val);
		octstr_strip_blanks(pass);
	}
	else if (octstr_case_compare(name, octstr_imm("X-Kannel-SMSC")) == 0) {
		smsc = octstr_duplicate(val);
		octstr_strip_blanks(smsc);
	}
    }

    /* 
     * try to catch at least the GET variables if available 
     */
    id = !id ? http_cgi_variable(args, "otaid") : id;
    from = !from ? http_cgi_variable(args, "from") : from;
    to = !to ? http_cgi_variable(args, "phonenumber") : to;
    user = !user ? http_cgi_variable(args, "username") : user;
    pass = !pass ? http_cgi_variable(args, "password") : pass;
    smsc = !smsc ? http_cgi_variable(args, "smsc") : smsc;

    /* check the username and password */
    t = authorise_username(user, pass, client_ip);
    if (t == NULL) {
	   *status = HTTP_FORBIDDEN;
	   ret = octstr_create("Authorization failed for sendota");
    }
    /* let's see if we have at least a target msisdn */
    else if (to == NULL) {
	   error(0, "%s needs a valid phone number.", octstr_get_cstr(sendota_url));
	   *status = HTTP_BAD_REQUEST;
       ret = octstr_create("Wrong sendota args.");
    } else {

    if (urltrans_faked_sender(t) != NULL) {
        from = octstr_duplicate(urltrans_faked_sender(t));
    } else if (from != NULL && octstr_len(from) > 0) {
    } else if (urltrans_default_sender(t) != NULL) {
        from = octstr_duplicate(urltrans_default_sender(t));
    } else if (global_sender != NULL) {
        from = octstr_duplicate(global_sender);
    } else {
        *status = HTTP_BAD_REQUEST;
        ret = octstr_create("Sender missing and no global set, rejected");
        goto error;
    }

    /*
     * get the content-type of the body document 
     */
    http_header_get_content_type(headers, &type, &charset);

	if (octstr_case_compare(type, 
        octstr_imm("application/x-wap-prov.browser-settings")) == 0) {
        doc_type = octstr_format("%s", "settings");
    } 
    else if (octstr_case_compare(type, 
             octstr_imm("application/x-wap-prov.browser-bookmarks")) == 0) {
	    doc_type = octstr_format("%s", "bookmarks");
    }

    if (doc_type == NULL) {
	    error(0, "%s got weird content type %s", octstr_get_cstr(sendota_url),
              octstr_get_cstr(type));
	    *status = HTTP_UNSUPPORTED_MEDIA_TYPE;
	    ret = octstr_create("Unsupported content-type, rejected");
	} else {

        /* 
         * ok, this is want we expect
         * now lets compile the whole thing 
         */
        ota_doc = octstr_duplicate(body);

        if ((r = ota_pack_message(&msg, ota_doc, doc_type, from, to)) < 0) {
            *status = HTTP_BAD_REQUEST;
            msg_destroy(msg);
            if (r == -2) {
                ret = octstr_create("Erroneous document type, cannot"
                                     " compile\n");
                goto error;
            }
            else if (r == -1) {
	           ret = octstr_create("Erroneous ota source, cannot compile\n");
               goto error;
            }
        }

        /* we still need to check if smsc is forced for this */
        if (urltrans_forced_smsc(t)) {
            msg->sms.smsc_id = octstr_duplicate(urltrans_forced_smsc(t));
            if (smsc)
                info(0, "send-sms request smsc id ignored, as smsc id forced to %s",
                     octstr_get_cstr(urltrans_forced_smsc(t)));
        } else if (smsc) {
            msg->sms.smsc_id = octstr_duplicate(smsc);
        } else if (urltrans_default_smsc(t)) {
            msg->sms.smsc_id = octstr_duplicate(urltrans_default_smsc(t));
        } else
            msg->sms.smsc_id = NULL;

        info(0, "%s <%s> <%s>", octstr_get_cstr(sendota_url), 
             id ? octstr_get_cstr(id) : "<default>", octstr_get_cstr(to));
    
        r = send_message(t, msg); 
        msg_destroy(msg);

        if (r == -1) {
            error(0, "sendota_request: failed");
            *status = HTTP_INTERNAL_SERVER_ERROR;
            ret = octstr_create("Sending failed.");
        }

        *status = HTTP_ACCEPTED;
        ret = octstr_create("Sent.");
    }
    }    
      
error:
    octstr_destroy(user);
    octstr_destroy(pass);
    octstr_destroy(smsc);

    return ret;
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

    /*
     * determine which kind of HTTP request this is any
     * call the necessary routine for it
     */

    /* sendsms */
    if (octstr_compare(url, sendsms_url) == 0)
    {
	/* 
	 * decide if this is a GET or POST request and let the 
	 * related routine handle the checking
	 */
	if (body == NULL)
	    answer = smsbox_req_sendsms(args, ip, &status);
	else
	    answer = smsbox_sendsms_post(hdrs, body, ip, &status);
    }
    /* XML-RPC */
    else if (octstr_compare(url, xmlrpc_url) == 0)
    {
        /*
         * XML-RPC request needs to have a POST body
         */
        if (body == NULL) {
            answer = octstr_create("Incomplete request.");
            status = HTTP_BAD_REQUEST;
        } else
            answer = smsbox_xmlrpc_post(hdrs, body, ip, &status);
    }
    /* sendota */
    else if (octstr_compare(url, sendota_url) == 0)
    {
	if (body == NULL)
            answer = smsbox_req_sendota(args, ip, &status);
        else
            answer = smsbox_sendota_post(args, hdrs, body, ip, &status);
    }
    /* add aditional URI compares here */
    else {
        answer = octstr_create("Unknown request.");
        status = HTTP_NOT_FOUND;
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

    switch (signum) {
        case SIGINT:

       	    if (program_status != shutting_down) {
                error(0, "SIGINT received, aborting program...");
                program_status = shutting_down;
            }
            break;

        case SIGHUP:
            warning(0, "SIGHUP received, catching and re-opening logs");
            log_reopen();
            alog_reopen();
            break;

        /* 
         * It would be more proper to use SIGUSR1 for this, but on some
         * platforms that's reserved by the pthread support. 
         */
        case SIGQUIT:
	       warning(0, "SIGQUIT received, reporting memory usage.");
	       gw_check_leaks();
	       break;
    }
}


static void setup_signal_handlers(void) {
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
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
    bb_ssl = 0;
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
#ifdef HAVE_LIBSSL
    cfg_get_bool(&bb_ssl, grp, octstr_imm("smsbox-port-ssl"));
#endif /* HAVE_LIBSSL */

    cfg_get_integer(&http_proxy_port, grp, octstr_imm("http-proxy-port"));

    http_proxy_host = cfg_get(grp, 
    	    	    	octstr_imm("http-proxy-host"));
    http_proxy_username = cfg_get(grp, 
    	    	    	    octstr_imm("http-proxy-username"));
    http_proxy_password = cfg_get(grp, 
    	    	    	    octstr_imm("http-proxy-password"));
    http_proxy_exceptions = cfg_get_list(grp,
    	    	    	    octstr_imm("http-proxy-exceptions"));

#ifdef HAVE_LIBSSL
    conn_config_ssl(grp);
#endif 
    
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

    cfg_get_bool(&mo_recode, grp, octstr_imm("mo-recode"));
    if(mo_recode < 0)
	mo_recode = 0;

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

    /*
     * load the configuration settings for the sendsms and sendota URIs
     * else assume the default URIs, st.
     */
    if ((sendsms_url = cfg_get(grp, octstr_imm("sendsms-url"))) == NULL)
        sendsms_url = octstr_imm("/cgi-bin/sendsms");
    if ((xmlrpc_url = cfg_get(grp, octstr_imm("xmlrpc-url"))) == NULL)
        xmlrpc_url = octstr_imm("/cgi-bin/xmlrpc");
    if ((sendota_url = cfg_get(grp, octstr_imm("sendota-url"))) == NULL)
        sendota_url = octstr_imm("/cgi-bin/sendota");

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
    debug("sms", 0, GW_NAME " smsbox version %s starting", VERSION);
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

    connect_to_bearerbox(bb_host, bb_port, bb_ssl, NULL /* bb_our_host */);
	/* XXX add our_host if required */

    heartbeat_thread = heartbeat_start(write_to_bearerbox, heartbeat_freq,
				       outstanding_requests);

    read_messages_from_bearerbox();

    info(0, GW_NAME " smsbox terminating.");

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
    octstr_destroy(sendsms_url);
    octstr_destroy(sendota_url);
    octstr_destroy(xmlrpc_url);
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
	// debug("sms.http", 0, "enter charset, coding=%d, msgdata is %s", coding, octstr_get_cstr(body));
	// octstr_dump(body, 0);

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
	
	// debug("sms.http", 0, "exit charset, coding=%d, msgdata is %s", coding, octstr_get_cstr(body));
	// octstr_dump(body, 0);
    }
    
    return resultcode;
}












