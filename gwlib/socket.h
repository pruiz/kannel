/*
 * General useful socket functions
 */

#ifndef _GW_SOCKET_H
#define _GW_SOCKET_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <config.h>

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#include "getnameinfo.h"

#include "gwstr.h"
#include "octstr.h"

/* Open a server socket. Return -1 for error, >= 0 socket number for OK.*/
int make_server_socket(int port);

/* Open a client socket. */
int tcpip_connect_to_server(char *hostname, int port);

/* As above, but binds our end to 'our_port' */
int tcpip_connect_to_server_with_port(char *hostname, int port, int our_port);

/* Write string to socket. */
int write_to_socket(int socket, char *str);

/* Check if socket is in blocking or non-blocking mode.  Return -1 for
 * error, 0 for nonblocking, 1 for blocking. */
int socket_query_blocking(int socket);

/* Set socket to blocking or non-blocking mode.  Return -1 for error,
 * 0 for success. */
int socket_set_blocking(int socket, int blocking);

/* Read a line from a socket. Return -1 for error, 0 for EOF, or 1 for OK.
   Remove CRLF and LF from end of line. */
int read_line(int fd, char *line, int max);

/* Read the rest of the input file into dynamically allocated memory. */
int read_to_eof(int fd, char **data, size_t *len);

/* Check if there is something to be read in 'fd'. Return 1 if there
 * is data, 0 otherwise, -1 on error */
int read_available(int fd, long wait_usec);


/*
 * Create a UDP socket for receiving from clients. Return -1 for failure,
 * a socket file descriptor >= 0 for OK.
 */
int udp_bind(int port);


/*
 * Create the client end of a UDP socket (i.e., a UDP socket that can
 * be on any port). Return -1 for failure, a socket file descriptor >= 0 
 * for OK.
 */
int udp_client_socket(void);


/*
 * Encode a hostname or IP number and port number into a binary address,
 * and return that as an Octstr. Return NULL if the host doesn't exist
 * or the IP number is syntactically invalid, or the port is bad.
 */
Octstr *udp_create_address(Octstr *host_or_ip, int port);


/*
 * Return the IP number of an encoded binary address, as a cleartext string.
 */
Octstr *udp_get_ip(Octstr *addr);


/*
 * Return the port number of an encoded binary address, as a cleartext string.
 */
int udp_get_port(Octstr *addr);


/*
 * Send a UDP message to a given server.
 */
int udp_sendto(int s, Octstr *datagram, Octstr *addr);


/*
 * Receive a UDP message from a client.
 */
int udp_recvfrom(int s, Octstr **datagram, Octstr **addr);


#endif
