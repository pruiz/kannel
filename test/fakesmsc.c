/*
 * fakesmsc.c - simulate an SMS center, using a trivial protocol
 *
 * The protocol:
 *
 *	Client sends each message on its own line (terminated with \r\n or \n).
 *	The line begins with 3 space-separated fields:
 *	sender's phone number, receiver's phone number,
 *	type of message. Type of message can be one of "text", "data", or
 *	"udh". If type == "text", the rest of the line is taken as the message.
 *	If type == "data", the next field is taken to be the text of the
 *	message in urlcoded form. Space is coded as '+'. If type == "udh",
 *	the following 2 fields are taken to be the UDH and normal portions
 *	in urlcoded form. Space is again coded as '+'.
 *	The server sends replies back in the same format.
 *
 * Lars Wirzenius, later edition by Kalle Marjola
 * Largely rewritten by Uoti Urpala
 */

static char usage[] = "\n\
Usage: fakesmsc [-H host] [-p port] [-i interval] [-m max] [-r <type>] <msg> ... \n\
\n\
* 'host' and 'port' define bearerbox connection (default localhost:10000),\n\
* 'interval' is time in seconds (floats allowed) between generated messages,\n\
* 'max' is the total number sent (-1, default, means unlimited),\n\
* <type> which numbers to randomize for MO messages, 1: src, 2: recv, 3: both\n\
*        where the specified numbers in <msg> are used as constant prefix,\n\
* <msg> is message to send, if several are given, they are sent randomly.\n\
\n\
msg format: \"sender receiver type(text/data/udh/route) [udhdata|route] msgdata\"\n\
\n\
Type \"text\" means plaintext msgdata, \"data\" urlcoded, \"udh\" urlcoded udh+msg\n\
and \"route\" means smsbox-id routed plaintext msgdata\n\
Examples: \n\
\n\
fakesmsc -m 1 \"123 345 udh %04udh%3f message+data+here\"\n\
fakesmsc -m 1 \"123 345 route smsbox1 message+data+here\"\n\
fakesmsc -i 0.01 -m 1000 \"123 345 text nop\" \"1 2 text another message here\"\n\
fakesmsc -r 3 -m 1000 \"123<rand> 345<rand> text nop\"\n\
\n\
Server replies are shown in the same message format.\n";

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <limits.h>
#include <signal.h>

#include <sys/param.h>

#include "gwlib/gwlib.h"

static int port = 10000;
static Octstr *host;
static long max_send = LONG_MAX;
static double interval = 1.0;
static int sigint_received;
static int rnd = 0;

static void signal_handler(int signum) 
{
    if (signum == SIGINT)
        sigint_received = 1;
    else
        panic(0, "Caught signal with no handler?!");
}


static void setup_signal_handlers(void) 
{
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
}


/* Choose a random message from a table of messages. */
static Octstr *choose_message(Octstr **msgs, int num_msgs) 
{
    /* the following doesn't give an even distribution, but who cares */
    return msgs[gw_rand() % num_msgs];
}


/* Get current time, as double. */
static double get_current_time(void) 
{
    struct timezone tz;
    struct timeval now;

    gettimeofday(&now, &tz);
    return (double) now.tv_sec + now.tv_usec / 1e6;
}

/* our arguments */
static int check_args(int i, int argc, char **argv) 
{
    if (strcmp(argv[i], "-p")==0 || strcmp(argv[i], "--port")==0)
        port = atoi(argv[i+1]);
    else if (!strcmp(argv[i], "-H") || !strcmp(argv[i], "--host"))
        host = octstr_create(argv[i+1]);
    else if (strcmp(argv[i], "-m")==0 || strcmp(argv[i], "--messages")==0) {
        max_send = atoi(argv[i+1]);
        if (max_send < 0)
            max_send = LONG_MAX;
    }
    else if (strcmp(argv[i], "-i")==0 || strcmp(argv[i], "--interval")==0)
        interval = atof(argv[i+1]);
    else if (strcmp(argv[i], "-r")==0 || strcmp(argv[i], "--randomize")==0) {
        rnd = atoi(argv[i+1]);
        if (rnd < 0 || rnd > 3)
            rnd = 0;
    }
    else {
        panic(0, "%s", usage);
        return 0;
    }

    return 1;
}


/* randomize the source and/or receiver numnber(s) */
static Octstr *randomize(Octstr *os)
{
    Octstr *msg = octstr_create("");
    List *words = octstr_split_words(os);
    int i;
    
    switch (rnd) {
        case 1:
            octstr_format_append(msg, "%S%d %S", list_get(words, 0), gw_rand(),
                                 list_get(words, 1));
            break;
        case 2:
            octstr_format_append(msg, "%S %S%d", list_get(words, 0), list_get(words, 1), 
                                 gw_rand());
            break;
        case 3:
            octstr_format_append(msg, "%S%d %S%d", list_get(words, 0), gw_rand(), 
                                 list_get(words, 1), gw_rand());
            break;
    }

    for (i = 2; i < list_len(words); i++) 
        octstr_format_append(msg, " %S", list_get(words, i));

    octstr_append_char(msg, 10); /* End of line */

    list_destroy(words, octstr_destroy_item);

    return msg;
}


/* The main program. */
int main(int argc, char **argv) 
{
    Connection *server;
    Octstr *line;
    Octstr **msgs;
    int i;
    int mptr, num_msgs;
    long num_received, num_sent;
    double first_received_at, last_received_at;
    double first_sent_at, last_sent_at;
    double start_time, end_time;
    double delta;

    gwlib_init();
    setup_signal_handlers();
    host = octstr_create("localhost");
    start_time = get_current_time();

    mptr = get_and_set_debugs(argc, argv, check_args);
    num_msgs = argc - mptr;
    if (num_msgs <= 0)
        panic(0, "%s", usage);

    /* pre-allocate array */
    msgs = gw_malloc(sizeof(Octstr *) * num_msgs);
    for (i = 0; i < num_msgs; i ++) {
        msgs[i] = octstr_create(argv[mptr + i]);
        octstr_append_char(msgs[i], 10); /* End of line */
    }
    info(0, "Host %s Port %d interval %.3f max-messages %ld",
           octstr_get_cstr(host), port, interval, max_send);

    srand((unsigned int) time(NULL));

    info(0, "fakesmsc starting");
    server = conn_open_tcp(host, port, NULL);
    if (server == NULL)
	panic(0, "Failed to open connection");

    num_sent = 0;
    num_received = 0;

    first_received_at = 0;
    first_sent_at = 0;
    last_received_at = 0;
    last_sent_at = 0;

    num_sent = 0;
    
    /* infinetly loop */
    while (1) {

        /* if we still have something to send as MO message */
        if (num_sent < max_send) {
            Octstr *os = choose_message(msgs, num_msgs);
            Octstr *msg = rnd > 0 ? randomize(os) : os;
 
            if (conn_write(server, msg) == -1)
                panic(0, "write failed");

            ++num_sent;
            if (num_sent == max_send)
                info(0, "fakesmsc: sent message %ld", num_sent);
            else
                debug("send", 0, "fakesmsc: sent message %ld", num_sent);
      
            if (rnd > 0)
                octstr_destroy(msg);

            last_sent_at = get_current_time();
            if (first_sent_at == 0)    
                first_sent_at = last_sent_at;
        }

        do {
            delta = interval * num_sent - (get_current_time() - first_sent_at);
            if (delta < 0)
                delta = 0;
            if (num_sent >= max_send)
                delta = -1;
            conn_wait(server, delta);
            if (conn_read_error(server) || conn_eof(server) || sigint_received)
                goto over;

            /* read as much as the smsc module provides us */
            while ((line = conn_read_line(server))) {
                last_received_at = get_current_time();
                if (first_received_at == 0)
                    first_received_at = last_received_at;
                ++num_received;
                if (num_received == max_send) {
                    info(0, "Got message %ld: <%s>", num_received,
                           octstr_get_cstr(line));
                } else {
                    debug("receive", 0, "Got message %ld: <%s>", num_received,
                          octstr_get_cstr(line));
                }
                octstr_destroy(line);
            }
        } while (delta > 0 || num_sent >= max_send);

    }

over:
    conn_destroy(server);

    /* destroy the MO messages */
    for (i = 0; i < num_msgs; i ++)
        octstr_destroy(msgs[i]);
    gw_free(msgs);

    end_time = get_current_time();

    info(0, "fakesmsc: %ld messages sent and %ld received", num_sent, num_received);
    info(0, "fakesmsc: total running time %.1f seconds", end_time - start_time);
    delta = last_sent_at - first_sent_at;
    if (delta == 0)
        delta = .01;
    if (num_sent > 1)
        info(0, "fakesmsc: from first to last sent message %.1f s, "
                "%.1f msgs/s", delta, (num_sent - 1) / delta);
    delta = last_received_at - first_received_at;
    if (delta == 0)
        delta = .01;
    if (num_received > 1)
        info(0, "fakesmsc: from first to last received message %.1f s, "
                "%.1f msgs/s", delta, (num_received - 1) / delta);
    info(0, "fakesmsc: terminating");
    
    return 0;
}

