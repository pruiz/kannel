/*
 * General useful socket functions
 */

#ifndef GW_SOCKET_H
#define GW_SOCKET_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <config.h>

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#include "octstr.h"

/* Return the official and fully qualified domain name of the host. 
   Caller should treat this as read-only. Caller MUST NOT destroy it. */
Octstr *get_official_name(void);

/* Return an official IP number for the host. Caller should treat this 
   as read-only. Caller MUST NOT destroy it. Note that there can be
   multiple official IP numbers for the host.
   */
Octstr *get_official_ip(void);

/* Open a server socket. Return -1 for error, >= 0 socket number for OK.*/
int make_server_socket(int port);

/* Open a client socket. */
int tcpip_connect_to_server(char *hostname, int port);

/* As above, but binds our end to 'our_port' */
int tcpip_connect_to_server_with_port(char *hostname, int port, int our_port);

/* Write string to socket. */
int write_to_socket(int socket, char *str);

/* Set socket to blocking or non-blocking mode.  Return -1 for error,
 * 0 for success. */
int socket_set_blocking(int socket, int blocking);

/* Check if there is something to be read in 'fd'. Return 1 if there
 * is data, 0 otherwise, -1 on error */
int read_available(int fd, long wait_usec);


/*
 * Create a UDP socket for receiving from clients. Return -1 for failure,
 * a socket file descriptor >= 0 for OK.
 */
int udp_bind(int port, const char *interface_name);


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


/*
 * Create an Octstr of character representation of an IP
 */
Octstr *host_ip(struct sockaddr_in addr);


/*
 * This must be called before sockets are used. gwlib_init does that
 */
void socket_init(void);


/*
 * Likewise, shutdown, called by gwlib_shutdown
 */
void socket_shutdown(void);

/*
 *  Converts an address of various types to an Octstr representation.
 *  Similar to host_ip, but works with more than IPv4
 */
Octstr *gw_netaddr_to_octstr(int af, void* src);


/*
 * Do an accept() system call for the given file descriptor. Return -1
 * for error (from accept or gwthread_poll, or gwthread_poll was 
 * interrupted by gwthread_wakeup) or the new file descriptor for success. 
 * Return IP number (as formatted by host_ip) via *client_addr.
 */
int gw_accept(int fd, Octstr **client_addr);



#endif
