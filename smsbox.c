/*
 * SMS BOX
 *
 * (WAP/SMS) Gateway
 *
 * Kalle Marjola 1999 for Wapit ltd.
 *
 */

/*
 * this is a SMS Service BOX
 *
 * it's main function is to receive SMS Messages from
 * (gateway) Bearer Box and then fulfill requests in those
 * messages
 *
 * It may also send SMS Messages on its own, sending them
 * to Bearer box and that way into SMS Centers
 *
 * 
 * FUNCTION:
 *
 * 1. main loop opens a TCP/IP socket into the bearer box, doing
 *    necessary handshake
 *
 * 2. for each SMS Message received, an ACK is sent back to bearer
 *    box and then a new thread is created to handle the request
 *
 * 3. replies to requests and HTTP-initiated messages are added
 *    to reply queue, which is then emptied by the main loop onto
 *    the bearer box
 *
 * THREAD FUNCTION:
 *
 * this program can also be used as a separate thread in Bearer Box
 * When used this way, different main porgram is simply used and messages
 * are transfered via queue
 *
 * CONFIGURATION:
 *
 * - Information required for connecting the bearer box is stored into
 *   a seperate configuration file.
 * - Service handling information is received from the bearer box during
 *   handshake procedure
 *
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>

#include "wapitlib.h"
#include "config.h"
#include "http.h"
#include "html.h"
#include "urltrans.h"


/* Perform the service requested by the user: translate the request into
 * a URL, fetch it, and return a string
 */
static char *obey_request(SMSMessage *sms, URLTranslation **url_t) {
        int num_words;
        char *words[1024];
        URLTranslation *t;
        char *url;
        char *data;
        size_t size;
        int type;
        char *stripped;
        char *new_data;
        char text[1024];
        char replytext[40*160+1];       /* ! absolute limit 40 SMSes ! */

        *url_t = NULL;  /* until found */
        
        /* XXX this should use octstr_split_words instead. */
        octstr_get_many_chars(text, sms->text, 0, octstr_len(sms->text));
        text[octstr_len(sms->text)] = '\0';
        num_words = split_words(text, 1024, words);
        if (num_words == 0)
                return strdup("Empty request");

        /*
         * Handle special, builtin requests.
         */
        if (strcasecmp(words[0], "nop") == 0)
                return strdup("You asked me to do nothing and I did it!");
/*        else if (strcasecmp(words[0], "stop") == 0) {
                if (!from_controller(sms))
                        return NULL;
                suspended = 1;
                return strdup("System suspended, use `start' to start.");
        } else if (strcasecmp(words[0], "start") == 0) {
                if (!from_controller(sms))
                        return NULL;
                suspended = 0;
                return strdup("System now running.");
        }
*/
	
        /*
         * Don't do anything, if we're suspended.
         */
/*        if (suspended) {
                info(0, "Ignoring request, because system is suspended.");
                return NULL;
        }
*/
/*	
        t = urltrans_find(translations, sms);
*/
        url = urltrans_get_url(t, sms);
        if (url == NULL) {
                error(0, "Oops, urltrans_get_url failed.");
                return NULL;
        }
        *url_t = t;
        
        
        debug(0, "formatted url: <%s>", url);

        if (http_get(url, &type, &data, &size) == -1)
                return NULL;

        /* Make sure the data is NUL termianted. */
        new_data = realloc(data, size + 1);
        if (new_data == NULL) {
                error(errno, "Out of memory allocating HTTP response.");
                free(data);
                return NULL;
        }
        data = new_data;
        data[size] = '\0';

/*
 * http_get is buggy at the moment, and doesn't set type correctly.
 * work around this. XXX fix this
 */
type = HTTP_TYPE_HTML;

        switch (type) {
        case HTTP_TYPE_HTML:
                if (urltrans_prefix(t) != NULL && urltrans_suffix(t) != NULL) {
                        stripped = html_strip_prefix_and_suffix(data,
                                                urltrans_prefix(t), 
                                                urltrans_suffix(t));
                        free(data);
                        data = stripped;
                }
                html_to_sms(replytext, sizeof(replytext), data);
                break;
        case HTTP_TYPE_TEXT:
                strncpy(replytext, data, sizeof(replytext) - 1);
                break;
        default:
                strcpy(replytext, "Result could not be represented as an SMS mes
sage.");
                break;
        }
        free(data);

        if (strlen(replytext)==0)
            return strdup("");
        return strdup(replytext);
}





static void *smsmessage_thread(void *arg) {
    unsigned long id;
    char *r;
    int max_msgs = 1;
    SMSMessage *msg;
    URLTranslation *url_t;
    
    msg = arg;
    
    id = (unsigned long) pthread_self();
    
    if (octstr_len(msg->text) == 0 ||
	strlen(msg->sender) == 0 ||
	strlen(msg->receiver) == 0) {
	error(0, "EMPTY: Text is <%s>, sender is <%s>, receiver is <%s>",
	      octstr_get_cstr(msg->text), msg->sender, msg->receiver);
	return NULL;
    }
    if (strcmp(msg->sender, msg->receiver) == 0) {
	info(0, "NOTE: sender and receiver same number <%s>, ignoring!",
	     msg->sender);
	return NULL;
    }
    info(0, "starting to service request <%s> from <%s>",
	 octstr_get_cstr(msg->text), msg->sender);

    /* TODO: check if the sender is approved to use this service */
    
    r = obey_request(msg, &url_t);
    if (r == NULL) {
	error(0, "request failed");
	r = strdup("Request failed");
	url_t = NULL;           /* ignore max message things */
	if (r == NULL)
	    goto error;
    }
    /* when we have received the reply text we must generate one or more
     * SMS messages from it, unless the reply is prohibited (max_msgs = 0)
     */
    if (r != NULL) {
	char smsbuffer[161], *ptr = r;
	int pos, len = 160;

	if (url_t != NULL) {
	    max_msgs = urltrans_max_messages(url_t);
	    if (*r == '\0') {
		if (urltrans_omit_empty(url_t) > 0) {
		    debug(0, "Empty reply omitted, set max_messages = 0");
		    max_msgs = 0;
		}
		else {
		    free(r);
		    max_msgs = 1;
		    r = strdup("<Empty reply from server>");
		    if (r == NULL)
			goto error;
		}	
	    }	
	}
	else
	    max_msgs = 1;

	if (max_msgs == 0)
	    info(0, "No reply sent (max_messages == 0)");
	else {
	    for(; max_msgs > 0; max_msgs--, ptr+= len) {
		
		/* if we use split-chars, a list of characters allowed to split	
		 * the message, then seek for the last one and split message
		 * from that place. */

		if (url_t != NULL &&
		    urltrans_split_chars(url_t)!=NULL &&
		    strlen(ptr) > 160) {
                        
		    pos = str_reverse_seek(ptr, 160, urltrans_split_chars(url_t));
		    if (pos > 120) 
			len = pos;
		    else
			len = 160;
		}	
		strncpy(smsbuffer, ptr, len);     	/* copy one part */
		smsbuffer[len] = '\0';
/*		msg = reply(req->sms, smsbuffer,
			    url_t ? urltrans_faked_sender(url_t) : NULL);

		if (msg != NULL) {
		    info(0, "response <%s>", octstr_get_cstr(msg->text));
		    if (smscenter_submit_smsmessage(req->smsc, msg) == -1)
			error(0, "error sending response");
		    smsmessage_destruct(msg);
		}
*/		if (strlen(ptr) <= len)     /* if there is no more stuff */
		    break;
	    }
	    free(r);
	}
    }
    free(arg);
    return NULL;
error:
    error(errno, "smsmessage_thread: failed");
    return NULL;
        
}


int main(int argc, char **argv)
{
    int fd;

    char *bb_host = "localhost";
    char linebuf[1024+1];
    int bb_port = 13001;
    
    fd = tcpip_connect_to_server(bb_host, bb_port);

    info(0, "Connected to Bearer Box at %s port %d", bb_host, bb_port);
    
    while(read_line(fd, linebuf, 1024) > 0) {
	info(0, "Read < %s >", linebuf);
	write_to_socket(fd, "A\n");
    }
    return 0;
}
