/*
 * General useful socket functions
 */

#ifndef _GW_SOCKET_H
#define _GW_SOCKET_H

#include <stddef.h>
#include <stdio.h>
#include <netinet/in.h>

#include <config.h>

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#include "getnameinfo.h"

#include "gwstr.h"

int gw_getnameinfo(struct sockaddr_in *addr, char** hostname, int* port);

/* Open a server socket. Return -1 for error, >= 0 socket number for OK.*/
int make_server_socket(int port);

/* Open a client socket. */
int tcpip_connect_to_server(char *hostname, int port);

/* As above, but binds our end to 'our_port' */
int tcpip_connect_to_server_with_port(char *hostname, int port, int our_port);

/* Write string to socket. */
int write_to_socket(int socket, char *str);

/* Read a line from a socket. Return -1 for error, 0 for EOF, or 1 for OK.
   Remove CRLF and LF from end of line. */
int read_line(int fd, char *line, int max);

/* Read the rest of the input file into dynamically allocated memory. */
int read_to_eof(int fd, char **data, size_t *len);

/* Check if there is something to be read in 'fd'. Return 1 if there
 * is data, 0 otherwise, -1 on error */
int read_available(int fd);



#endif
