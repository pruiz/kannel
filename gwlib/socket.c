#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <config.h>

#include "gwlib.h"


#ifndef UDP_PACKET_MAX_SIZE
#define UDP_PACKET_MAX_SIZE (64*1024)
#endif


/* configure should get this, but doesn't. XXX fix this --liw */
#if __sun && __sparc
#define HAVE_INET_ATON 1 
#endif


#if !HAVE_INET_ATON

int inet_aton(char *name, struct in_addr *ap)
{
	struct in_addr res;
	res.s_addr = inet_addr(name);
	if (res.s_addr == 0xffffffff) return 0;
	if (ap) *ap = res;
	return 1;
}

#endif

#if !HAVE_GETNAMEINFO

int getnameinfo (const struct sockaddr *sa,
		socklen_t salen,
		char *host, size_t hostlen,
		char *serv, size_t servlen,
		int flags)
{
	struct sockaddr_in *sin;

	if (flags & ~(NI_NUMERICHOST|NI_NUMERICSERV))
		panic(flags, "fake getnameinfo() only implements NI_NUMERICHOST and NI_NUMERICSERV flags\n");

	sin = (struct sockaddr_in *) sa;
	if (!sin || salen != sizeof(*sin))
		panic(0, "fake getnameinfo(): bad sa/salen (%p/%d)\n", sa, salen);
	if (sin->sin_family != AF_INET)
		panic(0, "fake getnameinfo() only supports AF_INET\n");

	if (host) {
		snprintf(host, hostlen, "%s", inet_ntoa(sin->sin_addr));
	}
	if (serv) {
		snprintf(serv, servlen, "%i", ntohs(sin->sin_port));
	}
	return 0;	/* XXX */
}

#endif


int make_server_socket(int port) {
	struct sockaddr_in addr;
	int s;
	int reuse;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1) {
		error(errno, "socket failed");
		goto error;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	reuse = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse,
		       sizeof(reuse)) == -1)
	{
		error(errno, "setsockopt failed for server address");
		goto error;
	}

	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		error(errno, "bind failed");
		goto error;
	}

	if (listen(s, 10) == -1) {
		error(errno, "listen failed");
		goto error;
	}

	return s;

error:
	if (s >= 0)
		(void) close(s);
	return -1;
}


int tcpip_connect_to_server(char *hostname, int port) {

    return tcpip_connect_to_server_with_port(hostname, port, 0);
}


int tcpip_connect_to_server_with_port(char *hostname, int port, int our_port) {
	struct sockaddr_in addr;
	struct sockaddr_in o_addr;
	struct hostent *hostinfo;
	struct linger dontlinger;
	int s;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1)
		goto error;

	dontlinger.l_onoff = 1;
	dontlinger.l_linger = 0;
#if defined(BSD) && !defined(__NetBSD__)
	setsockopt(s, SOL_TCP, SO_LINGER, &dontlinger, sizeof(dontlinger));
#else
{
	#include <netdb.h>
	/* XXX no error trapping */
	struct protoent *p = getprotobyname("tcp");
	setsockopt(s, p->p_proto, SO_LINGER, &dontlinger, sizeof(dontlinger));
}
#endif

	hostinfo = gethostbyname(hostname);
	if (hostinfo == NULL)
		goto error;

        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;

	if (our_port > 0) {
	    int reuse;

	    o_addr.sin_family = AF_INET;
	    o_addr.sin_port = htons(our_port);
	    o_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	    reuse = 1;
	    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse,
			   sizeof(reuse)) == -1) {
		error(errno, "setsockopt failed before bind");
		goto error;
	    }
	    if (bind(s, (struct sockaddr *) &o_addr, sizeof(o_addr)) == -1) {
		error(0, "bind to local port %d failed", our_port);
		goto error;
	    }
	}

	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) == -1)
		goto error;

	return s;

error:
	error(errno, "error connecting to server `%s' at port `%d'",
		hostname, port);
	if (s >= 0)
		close(s);
	return -1;
}


int write_to_socket(int socket, char *str) {
	size_t len;
	int ret;

	len = strlen(str);
	while (len > 0) {
		ret = write(socket, str, len);
		if (ret == -1) {
			if(errno==EAGAIN) continue;
			if(errno==EINTR) continue;
			error(errno, "Writing to socket failed");
			return -1;
		}
		/* ret may be less than len, if the writing was interrupted
		   by a signal. */
		len -= ret;
		str += ret;
	}
	return 0;
}


int read_line(int fd, char *line, int max) {
        char *start;
        int ret;

        start = line;
        while (max > 0) {
                ret = read(fd, line, 1);
		if (ret == -1) {
			if(errno==EAGAIN) continue;
			if(errno==EINTR) continue;
                        error(errno, "read failed");
			return -1;
		}
                if (ret == 0)
                        break;
                ++line;
                --max;
                if (line[-1] == '\n')
                        break;
        }

	if (line == start)
		return 0;

	if (line[-1] == '\n')
		--line;
        if (line[-1] == '\r')
                --line;
	*line = '\0';

	return 1;
}


int read_to_eof(int fd, char **data, size_t *len) {
	size_t size;
	int ret;
	char *p;

	*len = 0;
	size = 0;
	*data = NULL;
	for (;;) {
		if (*len == size) {
			size += 16*1024;
			p = gw_realloc(*data, size);
			*data = p;
		}
		ret = read(fd, *data + *len, size - *len);
		if (ret == -1) {
		    if(errno==EINTR) continue;
		    if(errno==EAGAIN) continue;
		    error(errno, "Error while reading");
		    goto error;
		}
		if (ret == 0)
			break;
		*len += ret;
	}

	return 0;

error:
	gw_free(*data);
	return -1;
}


int read_available(int fd, long wait_usec)
{
    fd_set rf;
    struct timeval to;
    int ret;

    FD_ZERO(&rf);
    FD_SET(fd, &rf);
    to.tv_sec = 0;
    to.tv_usec = wait_usec;

    ret = select(FD_SETSIZE, &rf, NULL, NULL, &to);
    if (ret > 0 && FD_ISSET(fd, &rf))
	return 1;
    if (ret < 0) {
	if(errno==EINTR) return 0;
	if(errno==EAGAIN) return 0;
	return -1;	/* some error */
    }
    return 0;
}



int udp_client_socket(void) {
	int s;
	
	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s == -1) {
		error(errno, "Couldn't create a UDP socket");
		return -1;
	}
	
	return s;
}


int udp_bind(int port) {
	int s;
	struct sockaddr_in sa;
	
	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s == -1) {
		error(errno, "Couldn't create a UDP socket");
		return -1;
	}
	
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(s, (struct sockaddr *) &sa, (int) sizeof(sa)) == -1) {
		error(errno, "Couldn't bind a UDP socket to port %d", port);
		(void) close(s);
		return -1;
	}
	
	return s;
}


Octstr *udp_create_address(Octstr *host_or_ip, int port) {
	struct sockaddr_in sa;
	struct hostent *h;
	
	h = gethostbyname(octstr_get_cstr(host_or_ip));
	if (h == NULL) {
		error(0, "Couldn't find the IP number of `%s'", 
			octstr_get_cstr(host_or_ip));
		return NULL;
	}

	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr = *(struct in_addr *) h->h_addr_list[0];
	
	return octstr_create_from_data(&sa, sizeof(sa));
}


int udp_get_port(Octstr *addr) {
	struct sockaddr_in sa;
	
	gw_assert(octstr_len(addr) == sizeof(sa));
	memcpy(&sa, octstr_get_cstr(addr), sizeof(sa));
	return ntohs(sa.sin_port);
}


Octstr *udp_get_ip(Octstr *addr) {
	struct sockaddr_in sa;
	
	gw_assert(octstr_len(addr) == sizeof(sa));
	memcpy(&sa, octstr_get_cstr(addr), sizeof(sa));
	return octstr_create(inet_ntoa(sa.sin_addr));
}


int udp_sendto(int s, Octstr *datagram, Octstr *addr) {
	struct sockaddr_in sa;
	
	gw_assert(octstr_len(addr) == sizeof(sa));
	memcpy(&sa, octstr_get_cstr(addr), sizeof(sa));
	if (sendto(s, octstr_get_cstr(datagram), octstr_len(datagram), 0,
		   &sa, (int) sizeof(sa)) == -1) {
		error(errno, "Couldn't send UDP packet");
		return -1;
	}
	return 0;
}


int udp_recvfrom(int s, Octstr **datagram, Octstr **addr) {
	struct sockaddr_in sa;
	int salen;
	char buf[UDP_PACKET_MAX_SIZE];
	int bytes;

	salen = sizeof(sa);
	bytes = recvfrom(s, &buf, (int) sizeof(buf), 0, &sa, &salen);
	if (bytes == -1) {
		error(errno, "Couldn't receive UDP packet");
		return -1;
	}
	
	*datagram = octstr_create_from_data(buf, bytes);
	*addr = octstr_create_from_data(&sa, salen);
	
	return 0;
}
