/*
 * fakesmsc.c - simulate an SMS centers, using a trivial protocol
 *
 * The protocol:
 *
 *	client writes a line (terminated with \r\n or \n) with three space
 *	separated fields: sender's phone number, receiver's phone number,
 *	and text of message
 *
 *	server writes similar lines when it wants to send messages; at the
 *	moment, it sends them after the client has been quiet for a while,
 *	see macros MSG_SECS and MSG_USECS below.
 *
 * The server terminates when the client terminates. It only accepts one
 * client.
 *
 * Lars Wirzenius, later edition by Kalle Marjola
 */
 
#define MAX_SEND (0)

static char usage[] = "\nUsage: fakesmsc [-p port] [-i interval] [-m max] <msg> ... \n\
\n\
where 'port' is the port to listen to (default 10000), 'interval' is the \n\
interval (default 1.0), in seconds (floating point allowed), between \n\
automatically generated messages, 'max' is the maximum number of messages \n\
to send (0, default, means infinitum), and <msg> is the message to be sent. \n\
If there are several messages, they are sent in random order.";


#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <sys/param.h>

#include "gwlib/gwlib.h"

static int port = 10000;
static int max_send = 0;
static double interval = 1.0;

/* Write a line to a socket. Also write the terminating newline (i.e., the
   input string does not have to include it. */
static void write_line(int fd, char *line) {
    int n;
	
    n = strlen(line);
    if (write(fd, line, n) != n)
	panic(errno, "write failed or truncated");
    if (write(fd, "\n", 1) != 1)
	panic(errno, "write failed or truncated");
}


/* Choose a random message from a table of messages. */
static char *choose_message(char **msgs, int num_msgs) {
    /* the following doesn't give an even distribution, but who cares */
    return msgs[rand() % num_msgs];
}


/* Get current time, as double. */
static double get_current_time(void) {
    struct timezone tz;
    struct timeval now;

    gettimeofday(&now, &tz);
    return (double) now.tv_sec + now.tv_usec / 1e6;
}

/* our arguments */
static int check_args(int i, int argc, char **argv) {
    if (strcmp(argv[i], "-p")==0 || strcmp(argv[i], "--port")==0)
        port = atoi(argv[i+1]);
    else if (strcmp(argv[i], "-m")==0 || strcmp(argv[i], "--messages")==0)
	max_send = atoi(argv[i+1]);
    else if (strcmp(argv[i], "-i")==0 || strcmp(argv[i], "--interval")==0)
	interval = atof(argv[i+1]);
    else if (strcmp(argv[i], "-h")==0 || strcmp(argv[i], "--help")==0) {
	panic(0, "%s", usage);
	return 0;
    } else 
	return -1;

    return 1;
} 

/* The main program. */
int main(int argc, char **argv) {
    int s, client, ret;
    struct sockaddr client_addr;
    socklen_t client_addr_len;
    char line[1024];
    fd_set readset;
    struct timeval tv, tvinit;
    char **msgs;
    int mptr, num_msgs;
    int num_received, num_sent;
    double first_received_at, last_received_at;
    double first_sent_at, last_sent_at;
    double start_time, end_time;
    double delta;

    gwlib_init();
    start_time = get_current_time();

    mptr = get_and_set_debugs(argc, argv, check_args);
    num_msgs = argc - mptr;
    if (num_msgs <= 0)
	panic(0, "%s", usage);
    msgs = argv + mptr;
    info(0, "Port %d interval %.3f max-messages %d",
	 port, interval, max_send);
	
    tvinit.tv_sec = (long) interval;
    tvinit.tv_usec = (long) ((interval - tvinit.tv_sec) * 1e6);
    debug("xxx", 0, "sec=%ld usec=%ld",
	  (long) tvinit.tv_sec, (long) tvinit.tv_usec);
	

    srand((unsigned int) time(NULL));

    info(0, "fakesmsc starting");
    s = make_server_socket(port);
    if (s == -1)
	panic(0, "couldn't create server socket");
    client_addr_len = sizeof(client_addr);
    client = accept(s, &client_addr, &client_addr_len);
    if (client == -1)
	panic(errno, "accept failed");
    close(s);
	
    num_sent = 0;
    num_received = 0;

    first_received_at = 0;
    first_sent_at = 0;
    last_received_at = 0;
    last_sent_at = 0;

    num_sent = 0;
    while (client >= 0) {
	if (max_send == 0 || num_sent < max_send) {
	    write_line(client, choose_message(msgs, num_msgs));
	    ++num_sent;
	    debug("send", 0, "fakesmsc: sent message %d", num_sent);
	    last_sent_at = get_current_time();
	    if (first_sent_at == 0)
		first_sent_at = last_sent_at;
	}

	do {
	    FD_ZERO(&readset);
	    FD_SET(client, &readset);
	    tv = tvinit;
	    ret = select(client+1, &readset, NULL, NULL, &tv);
	    if (ret == -1)
		panic(errno, "select failed");
	    if (ret == 1 && FD_ISSET(client, &readset)) {
		if (read_line(client, line, 
			      sizeof(line)) <= 0) {
		    close(client);
		    client = -1;
		    break;
		}
		++num_received;
		if (num_received == max_send) {
		    info(0, "fakesmsc: got message %d: <%s>",
			 num_received, line);
		} else {
		    debug("receive", 0, "fakesmsc: got message %d: <%s>",
			  num_received, line);
		}
		last_received_at = get_current_time();
		if (first_received_at == 0)
		    first_received_at = last_received_at;
	    }
	} while (get_current_time() - last_sent_at < interval ||
		 !(max_send == 0 || num_sent < max_send));
    }
	
    close(client);

    end_time = get_current_time();

    info(0, "fakesmsc: %d messages sent and %d received",
	 num_sent, num_received);
    info(0, "fakesmsc: total running time %.1f seconds", 
	 end_time - start_time);
    delta = last_sent_at - first_sent_at;
    info(0, "fakesmsc: from first to last sent message %.1f s, "
	 "%.1f msgs/s", delta, num_sent / delta);
    delta = last_received_at - first_received_at;
    info(0, "fakesmsc: from first to last received message %.1f s, "
	 "%.1f msgs/s", delta, num_received / delta);
    info(0, "fakesmsc: terminating");
    return 0;
}
