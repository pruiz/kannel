/* gwpoll.h - define poll() for systems that don't have it */

#ifndef GWPOLL_H
#define GWPOLL_H

/* If the system supplies it, we're done.  Assume that if the header file
 * exists, it will define poll() and struct pollfd for us. */
#ifdef HAVE_SYS_POLL_H

#include <sys/poll.h>

/* Most systems accept any negative timeout value as meaning infinite
 * timeout.  FreeBSD explicitly wants INFTIM, however.  Other systems
 * don't define INFTIM.  So we use it if it's defined, and -1 otherwise.
 */

#ifdef INFTIM
#define POLL_NOTIMEOUT INFTIM
#else
#define POLL_NOTIMEOUT (-1)
#endif

#else

/* Define struct pollfd and the event bits, and declare a function poll()
 * that uses them and is a wrapper around select(). */

struct pollfd {
    int fd;	   /* file descriptor */
    short events;  /* requested events */
    short revents; /* returned events */
};

/* Bits for events and revents */
#define POLLIN   1    /* Reading will not block */
#define POLLPRI  2    /* Urgent data available for reading */
#define POLLOUT  4    /* Writing will not block */

/* Bits only used in revents */
#define POLLERR  8    /* Error condition */
#define POLLHUP  16   /* Hung up: fd was closed by other side */
#define POLLNVAL 32   /* Invalid: fd not open or not valid */

#define POLL_NOTIMEOUT (-1)

/* Implement the function as gw_poll, in case the system does have a poll()
 * function in its libraries but just fails to define it in sys/poll.h. */
#define poll(fdarray, numfds, timeout) gw_poll(fdarray, numfds, timeout)
int gw_poll(struct pollfd *fdarray, unsigned int numfds, int timeout);

#endif  /* !HAVE_SYS_POLL_H */

#endif
