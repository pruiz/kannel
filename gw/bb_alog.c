/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2003 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * gw/bb_alog.c -- encapsulate custom access log logic and escape code parsing
 *
 * Stipe Tolj <tolj@wapme-systems.de
 */

#include "gwlib/gwlib.h"
#include "msg.h"
#include "sms.h"
#include "bearerbox.h"
#include "smscconn.h"

static Octstr *custom_log_format = NULL;


/********************************************************************
 * Routine to escape the values into the custom log format.
 *
 * The following escape code values are acceptable within the 
 * 'access-log-format' config directive of bearerbox:
 *
 *   %l - log message
 *   %i - smsc-id
 *   %n - service-name (for MO) or sendsms-user (for MT)
 *   %A  - account
 *   %B - billing identifier/information
 *   %p - sender (from) 
 *   %P - receiver (to)
 *   %m - message class (mclass)
 *   %c - coding
 *   %M - message waiting indicator (mwi)
 *   %C - compress indicator
 *   %d - dlr_mask
 *   %a - the orginal SMS message, spaces squeezed
 *   %u - UDH data (in escaped form)
 *   %U - length of UDH data
 *   %k - the keyword in the SMS request (the first word in the SMS message) 
 *   %s - next word from the SMS message, starting with the second one
 *   %S - same as %s, but '*' is converted to '~' 
 *   %r - words not yet used by %s
 *   %b - the orginal SMS message
 *   %L - length of SMS message
 *   %t - the time of the message, formatted as "YYYY-MM-DD HH:MM:SS"
 *   %T - the time of the message, in UNIX epoch timestamp format
 *   %I - the internal message ID
 *
 * Most escape codes should be compatibel with escape codes used in
 * sms-service groups.
 *
 * The default access-log-format would look like this (if access-log-clean is true):
 *   "%t %l [SMSC:%i] [SVC:%n] [ACT:%A] [BINF:%B] [from:%p] [to:%P] \
 *    [flags:%m:%c:%M:%C:%d] [msg:%L:%b] [udh:%U:%u]"
 */
  
static Octstr *get_pattern(SMSCConn *conn, Msg *msg, char *message)
{
    int nextarg, j;
    struct tm tm;
    int num_words;
    List *word_list;
    Octstr *result, *pattern;
    long pattern_len;
    long pos;
    int c;
    long i;
    Octstr *temp, *text, *udh;
 
    text = msg->sms.msgdata ? octstr_duplicate(msg->sms.msgdata) : octstr_create("");
    udh = msg->sms.udhdata ? octstr_duplicate(msg->sms.udhdata) : octstr_create("");
    if ((msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2))
        octstr_binary_to_hex(text, 1);
    octstr_binary_to_hex(udh, 1);

    if (octstr_len(text)) {
        word_list = octstr_split_words(text);
        num_words = list_len(word_list);
    } else {
    	word_list = list_create();
        num_words = 0;
    }

    result = octstr_create("");
    pattern = octstr_duplicate(custom_log_format);

    pattern_len = octstr_len(pattern);
    nextarg = 1;
    pos = 0;

    for (;;) {
        while (pos < pattern_len) {
            c = octstr_get_char(pattern, pos);
            if (c == '%' && pos + 1 < pattern_len)
                break;
            octstr_append_char(result, c);
            ++pos;
        }

        if (pos == pattern_len)
            break;

    switch (octstr_get_char(pattern, pos + 1)) {
	case 'k':
	    if (num_words <= 0)
            break;
	    octstr_append(result, list_get(word_list, 0));
	    break;

	case 's':
	    if (nextarg >= num_words)
            break;
	    octstr_append(result, list_get(word_list, nextarg));
	    ++nextarg;
	    break;

	case 'S':
	    if (nextarg >= num_words)
            break;
	    temp = list_get(word_list, nextarg);
	    for (i = 0; i < octstr_len(temp); ++i) {
		if (octstr_get_char(temp, i) == '*')
		    octstr_append_char(result, '~');
		else
		    octstr_append_char(result, octstr_get_char(temp, i));
	    }
	    ++nextarg;
	    break;

	case 'r':
	    for (j = nextarg; j < num_words; ++j) {
		if (j != nextarg)
		    octstr_append_char(result, '+');
		octstr_append(result, list_get(word_list, j));
	    }
	    break;
    
	case 'l':
	    octstr_append_cstr(result, message);
	    break;

	case 'P':
	    octstr_append(result, msg->sms.receiver);
	    break;

	case 'p':
	    octstr_append(result, msg->sms.sender);
	    break;

	case 'a':
	    for (j = 0; j < num_words; ++j) {
                if (j > 0)
                    octstr_append_char(result, ' ');
                octstr_append(result, list_get(word_list, j));
            }
            break;

	case 'b':
	    octstr_append(result, text);
	    break;

	case 'L':
	    octstr_append_decimal(result, octstr_len(msg->sms.msgdata));
	    break;

	case 't':
	    tm = gw_gmtime(msg->sms.time);
	    octstr_format_append(result, "%04d-%02d-%02d %02d:%02d:%02d",
				 tm.tm_year + 1900,
				 tm.tm_mon + 1,
				 tm.tm_mday,
				 tm.tm_hour,
				 tm.tm_min,
				 tm.tm_sec);
	    break;

	case 'T':
	    if (msg->sms.time == MSG_PARAM_UNDEFINED)
            break;
	    octstr_format_append(result, "%ld", msg->sms.time);
	    break;

	case 'i':
	    if (msg->sms.smsc_id == NULL)
            break;
	    octstr_append(result, msg->sms.smsc_id);
	    break;

	case 'I':
	    if (!uuid_is_null(msg->sms.id)) {
                char id[UUID_STR_LEN + 1];
                uuid_unparse(msg->sms.id, id);
	        octstr_append_cstr(result, id);
            }
	    break;

	case 'n':
	    if (msg->sms.service == NULL)
            break;
	    octstr_append(result, msg->sms.service);
	    break;

	case 'd':
	    octstr_append_decimal(result, msg->sms.dlr_mask);
	    break;

	case 'c':
	    octstr_append_decimal(result, msg->sms.coding);
	    break;

	case 'm':
	    octstr_append_decimal(result, msg->sms.mclass);
	    break;

	case 'C':
	    octstr_append_decimal(result, msg->sms.compress);
	    break;

	case 'M':
	    octstr_append_decimal(result, msg->sms.mwi);
	    break;

	case 'u':
	    if (octstr_len(udh)) {
                octstr_append(result, udh);
	    }
	    break;

	case 'U':
	    octstr_append_decimal(result, octstr_len(msg->sms.udhdata));
	    break;

	case 'B':  /* billing identifier/information */
	    if (octstr_len(msg->sms.binfo)) {
                octstr_append(result, msg->sms.binfo);
            }
            break;

	case 'A':  /* account */
	    if (octstr_len(msg->sms.account)) {
                octstr_append(result, msg->sms.account);
            }
            break;

    /* XXX add more here if needed */

	case '%':
	    octstr_format_append(result, "%%");
	    break;

	default:
	    octstr_format_append(result, "%%%c",
	    	    	    	 octstr_get_char(pattern, pos + 1));
	    break;
    } /* switch(...) */

	pos += 2;
    } /* for ... */

    list_destroy(word_list, octstr_destroy_item);

    return result;
}


/********************************************************************
 * 
 */

void bb_alog_init(Octstr *format)
{
    gw_assert(format != NULL);

    custom_log_format = octstr_duplicate(format);
}


void bb_alog_sms(SMSCConn *conn, Msg *sms, char *message)
{
    Octstr *text, *udh;

    text = udh = NULL;

    /* if we don't have any custom log, then use our "default" one */
    
    if (custom_log_format == NULL) {
        text = sms->sms.msgdata ? octstr_duplicate(sms->sms.msgdata) : octstr_create("");
        udh = sms->sms.udhdata ? octstr_duplicate(sms->sms.udhdata) : octstr_create("");
        if ((sms->sms.coding == DC_8BIT || sms->sms.coding == DC_UCS2))
            octstr_binary_to_hex(text, 1);
        octstr_binary_to_hex(udh, 1);

        alog("%s [SMSC:%s] [SVC:%s] [ACT:%s] [BINF:%s] [from:%s] [to:%s] [flags:%d:%d:%d:%d:%d] "
             "[msg:%d:%s] [udh:%d:%s]",
             message,
             conn ? (smscconn_id(conn) ? octstr_get_cstr(smscconn_id(conn)) : "") : "",
             sms->sms.service ? octstr_get_cstr(sms->sms.service) : "",
             sms->sms.account ? octstr_get_cstr(sms->sms.account) : "",
             sms->sms.binfo ? octstr_get_cstr(sms->sms.binfo) : "",
             sms->sms.sender ? octstr_get_cstr(sms->sms.sender) : "",
             sms->sms.receiver ? octstr_get_cstr(sms->sms.receiver) : "",
             sms->sms.mclass, sms->sms.coding, sms->sms.mwi, sms->sms.compress,
             sms->sms.dlr_mask, 
             octstr_len(sms->sms.msgdata), octstr_get_cstr(text),
             octstr_len(sms->sms.udhdata), octstr_get_cstr(udh)
        );

    } else {
        text = get_pattern(conn, sms, message);
        alog("%s", octstr_get_cstr(text));
    }

    octstr_destroy(udh);
    octstr_destroy(text);
}


