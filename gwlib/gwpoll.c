/* gwpoll.c - implement poll() for systems that don't have it */

#include "gwlib/gwlib.h"

#ifndef HAVE_SYS_POLL_H

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

int gw_poll(struct pollfd *fdarray, unsigned int numfds, int timeout)
{
    struct timeval tv, *tvp;
    unsigned int i;
    int maxfd;
    fd_set readfds, *rfdp;
    fd_set writefds, *wfdp;
    fd_set exceptfds, *xfdp;
    int ret;
    int result;

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    maxfd = -1;
    /* These are the pointers we will pass to select().  We use them because
     * we may want to pass NULL for some of them. */
    tvp = NULL;
    rfdp = NULL;
    wfdp = NULL;
    xfdp = NULL;

    /* Deal with timeout.  We get it in milliseconds.  If it's negative,
     * block indefinitely, which we do in select() by passing a NULL
     * timeval pointer. */
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        tvp = &tv;
    }

    /* Deal with fdarray, and convert it to the three fd_sets used by select. */
    for (i = 0; i < numfds; i++) {
        int fd = fdarray[i].fd;
        int events = fdarray[i].events;
        if (fd < 0)
            continue;
        if (events & POLLIN) {
            FD_SET(fd, &readfds);
            rfdp = &readfds;
	}
        if (events & POLLOUT) {
            FD_SET(fd, &writefds);
            wfdp = &writefds;
	}
        if (events & POLLPRI) {
            FD_SET(fd, &exceptfds);
            xfdp = &exceptfds;
	}
        if (fd > maxfd && events & (POLLIN | POLLOUT | POLLPRI))
	    maxfd = fd;
    }

    ret = select(maxfd + 1, rfdp, wfdp, xfdp, tvp);
    if (ret < 0)
        return ret;

    /* Move the returned data from the fd sets to the revents fields
     * in fdarray.  We can't detect POLLNVAL except for obviously
     * invalid fd's, and detecting POLLHUP or POLLERR would require
     * an extra read() call per fd which is too expensive. */
    result = 0;
    for (i = 0; i < numfds; i++) {
        if (fdarray[i].fd < 0) {
	    fdarray[i].revents = POLLNVAL;
            continue;
        }
        fdarray[i].revents = 0;
        if (rfdp && FD_ISSET(fdarray[i].fd, &readfds))
	    fdarray[i].revents |= POLLIN;
        if (wfdp && FD_ISSET(fdarray[i].fd, &writefds))
	    fdarray[i].revents |= POLLOUT;
        if (xfdp && FD_ISSET(fdarray[i].fd, &exceptfds))
	    fdarray[i].revents |= POLLPRI;
	if (fdarray[i].revents != 0)
	    result++;
    }

    return result;
}

#endif
