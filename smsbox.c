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
#include "sms_msg.h"


/* global variables */

int socket_fd;
pthread_mutex_t socket_mutex;




/* Perform the service requested by the user: translate the request into
 * a URL, fetch it, and return a string
 */
static char *obey_request(SMSMessage *sms) {
        int num_words;
        char *words[1024];
        char *url;
        char *data;
        size_t size;
        int type;
        char *new_data;
        char text[1024];
        char replytext[40*160+1];       /* ! absolute limit 40 SMSes ! */

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
        url = urltrans_get_url(t, sms);
        if (url == NULL) {
                error(0, "Oops, urltrans_get_url failed.");
                return NULL;
        }
        *url_t = t;
	*/
        url = "http://www.wapit.com";
        
        debug(0, "formatted url: <%s>", url);

        if (http_get(url, &type, &data, &size) == -1)
	    return NULL;

	debug(0, "http done.");
	
        /* Make sure the data is NUL termianted. */
        new_data = realloc(data, size + 1);
        if (new_data == NULL) {
                error(errno, "Out of memory allocating HTTP response.");
                free(data);
                return NULL;
        }
        data = new_data;
        data[160] = '\0';
/*        data[size] = '\0';*/

/*
 * http_get is buggy at the moment, and doesn't set type correctly.
 * work around this. XXX fix this
 */
type = HTTP_TYPE_HTML;

        switch (type) {
        case HTTP_TYPE_HTML:
/*                if (urltrans_prefix(t) != NULL && urltrans_suffix(t) != NULL) {
                        stripped = html_strip_prefix_and_suffix(data,
                                                urltrans_prefix(t), 
                                                urltrans_suffix(t));
                        free(data);
                        data = stripped;
                }
*/
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

	debug(0, "replytext done size= %d",size);

        if (strlen(replytext)==0)
            return strdup("");
        return strdup(replytext);
}





static void *request_thread(void *arg) {
    unsigned long id;
    SMSMessage *msg;
    char *r;
    int ret;
    
    msg = arg;
    id = (unsigned long) pthread_self();

    debug(0, "New request thread");
    
    if (octstr_len(msg->text) == 0 ||
	strlen(msg->sender) == 0 ||
	strlen(msg->receiver) == 0) {
	error(0, "EMPTY: Text is <%s>, sender is <%s>, receiver is <%s>",
	      octstr_get_cstr(msg->text), msg->sender, msg->receiver);

	/* perhaps we should return a NACK here, instead as in
	   main program... */

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
    
    r = obey_request(msg);
    if (r == NULL) {
	error(0, "request failed");
	r = strdup("Request failed");
	if (r == NULL)
	    goto error;
    }
    /* when we have received the reply text we must generate one or more
     * SMS messages from it, unless the reply is prohibited (max_msgs = 0)
     */
    if (r != NULL) {
	char buf[1024];
	sprintf(buf, "%d %s %s %s\n", msg->id, msg->receiver, msg->sender, r);

	info(0, "Answer got, locking mutex...");
	ret = pthread_mutex_lock(&socket_mutex);
	if (ret != 0)
	    goto error;

	write_to_socket(socket_fd, buf);
	
	debug(0, "write < %s >", buf);
	
	ret = pthread_mutex_unlock(&socket_mutex);
	if (ret != 0)
	    goto error;
	free(r);
    }
    smsmessage_destruct(msg);
    
    return NULL;
error:
    error(errno, "request_thread: failed");
    smsmessage_destruct(msg);
    return NULL;
        
}


static void new_request(char *buf)
{
    SMSMessage *msg;
    char *sender, *receiver, *text;
    char *p;
    int id;

    id = atoi(buf);
    p = strchr(buf, ' ');
    if (p == NULL)
	sender = receiver = text = "";
    else {
	*p++ = '\0';
	sender = p;
	p = strchr(sender, ' ');
	if (p == NULL)
	    receiver = text = "";
	else {
	    *p++ = '\0';
	    receiver = p;
	    p = strchr(receiver, ' ');
	    if (p == NULL)
		text = "";
	    else {
		*p++ = '\0';
		text = p;
	    }
	}
    }
    debug(0, "constructing...");
    
    msg = smsmessage_construct(sender, receiver, octstr_create(text));
    if (msg != NULL) {
	msg->id = id;
	debug(0, "Starting thread");
	(void)start_thread(1, request_thread, msg, 0);
    }
    debug(0, "Created a new request thread");
}



int main(int argc, char **argv)
{
    char *bb_host = "localhost";
    char linebuf[1024+1];
    int bb_port = 13001;
    int ret;
    
    pthread_mutex_init(&socket_mutex, NULL);

    socket_fd = tcpip_connect_to_server(bb_host, bb_port);

    info(0, "Connected to Bearer Box at %s port %d", bb_host, bb_port);
    
    ret = pthread_mutex_lock(&socket_mutex);
    if (ret != 0) goto error;

    while(1) {

	ret = read_available(socket_fd);
	if (ret > 0) {
	    ret = read_line(socket_fd, linebuf, 1024);
	    if (ret < 1) {
		error(0, "read line failed!");
		break;
	    }
	    debug(0, "Read < %s >", linebuf);

	    /* ignore ack/nack, TODO: do not ignore
	     */
	    if (*linebuf == 'A' || *linebuf == 'N')
		continue;

/*	    if (write_to_socket(socket_fd, "A\n")<0)
		goto error;
*/    
	    ret = pthread_mutex_unlock(&socket_mutex);
	    if (ret != 0) goto error;
	    
	    new_request(linebuf);
	}
	else {
	    ret = pthread_mutex_unlock(&socket_mutex);
	    if (ret != 0) goto error;
	}
	usleep(1000);

	ret = pthread_mutex_lock(&socket_mutex);
	if (ret != 0) goto error;
    }
    return 0;

error:
    panic(0, "Mutex error, exiting");
    return 0; /* never reached */
}
