/*
 * wapitlib.c - generally useful, non-application specific functions for WapIT
 *
 * Lars Wirzenius for WapIT Ltd.
 */

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

#include "gwlib.h"



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
	struct sockaddr_in addr;
	struct hostent *hostinfo;
	struct linger dontlinger;
	int s;
	
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1)
		goto error;

	dontlinger.l_onoff = 1;
	dontlinger.l_linger = 0;
#ifdef BSD
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
			p = realloc(*data, size);
			if (p == NULL)
				goto error;
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
	free(*data);
	return -1;
}


int read_available(int fd)
{
    fd_set rf;
    struct timeval to;
    int ret;

    FD_ZERO(&rf);
    FD_SET(fd, &rf);
    to.tv_sec = 0;
    to.tv_usec = 0;

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



/*
 * new datatype functions
 */



MultibyteInt get_variable_value(Octet *source, int *len)
{
    MultibyteInt retval = 0;
    
    for(*len=1;; (*len)++, source++) {
	retval = retval * 0x80 + (*source & 0x7F);
	if (*source < 0x80)	/* if the continue-bit (high bit) is not set */
	    break;
    }
    return retval;
}


int write_variable_value(MultibyteInt value, Octet *dest)
{
    int i, loc = 0;
    Octet revbuffer[20];	/* we write it backwards */
    
    for (;;) {
	revbuffer[loc++] = (value & 0x7F) + 0x80;	
	if (value >= 0x80)
	    value = value >> 7;
	else
	    break;
    }
    for(i=0; i < loc; i++)		/* reverse the buffer */
	dest[i] = revbuffer[loc-i-1];
    
    dest[loc-1] &= 0x7F;	/* remove trailer-bit from last */

    return loc;
}

Octet reverse_octet(Octet source)
{
    Octet	dest;
    dest = (source & 1) <<7;
    dest += (source & 2) <<5;
    dest += (source & 4) <<3;
    dest += (source & 8) <<1;
    dest += (source & 16) >>1;
    dest += (source & 32) >>3;
    dest += (source & 64) >>5;
    dest += (source & 128) >>7;
    
    return dest;
}



int get_and_set_debugs(int argc, char **argv,
		       int (*find_own) (int index, int argc, char **argv))
{
    int i, ret = -1;
    int debug_lvl = -1;
    int file_lvl = DEBUG;
    char *log_file = NULL;
    
    for(i=1; i < argc; i++) {
	if (strcmp(argv[i],"-v")==0 ||
	    strcmp(argv[i],"--verbosity")==0) {

	    if (i+1 < argc) {
		debug_lvl = atoi(argv[i+1]);
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if (strcmp(argv[i],"-F")==0 ||
		   strcmp(argv[i],"--logfile")==0) {
	    if (i+1 < argc && *(argv[i+1]) != '-') {
		log_file = argv[i+1];
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if (strcmp(argv[i],"-V")==0 ||
		   strcmp(argv[i],"--fileverbosity")==0) {
	    if (i+1 < argc) {
		file_lvl = atoi(argv[i+1]);
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if(*argv[i] != '-')
	    break;
	else {
	    if (find_own != NULL) {
		ret = find_own(i, argc, argv);
	    }
	    if (ret < 0)
		fprintf(stderr, "Unknown option %s, ignoring\n", argv[i]);
	    else
		i += ret;	/* advance additional args */
	}
    }
    if (debug_lvl > -1)
	set_output_level(debug_lvl);
    if (log_file != NULL)
	open_logfile(log_file, file_lvl);

    info(0, "Debug_lvl = %d, log_file = %s, log_lvl = %d",
	  debug_lvl, log_file ? log_file : "<none>", file_lvl);
    
    return i;
}

void print_std_args_usage(FILE *stream)
{
    fprintf(stream,
	   " -v <level>     set stderr output level. 0 = DEBUG, 4 = PANIC\n"
	   " -F <logfile>   set logfile name\n"
	   " -V <level>     set logfile output level. Defaults to DEBUG\n"
	   " --verbosity, --logfile, --fileverbosity   aliased arguments\n");
}


int check_ip(char *accept_string, char *ip, char *match_buffer)
{
    char *p, *t, *start;

    t = accept_string;
    
    while(1) {
	for(p = ip, start = t;;p++, t++) {
	    if ((*t == ';' || *t == '\0') && *p == '\0')
		goto found;

	    if (*t == '*') {
		t++;
		while(*p != '.' && *p != ';' && *p != '\0')
		    p++;

		if (*p == '\0')
		    goto found;
		continue;
	    }
	    if (*p == '\0' || *t == '\0' || *t != *p)
		break;		/* not matching */

	}
	for(; *t != ';'; t++)		/* seek next IP */
	    if (*t == '\0')
		goto failed;
	t++;
    }
failed:    
    debug(0, "Could not find match for <%s> in <%s>", ip, accept_string);
    return 0;
found:
    if (match_buffer != NULL) {
	for(p=match_buffer; *start != '\0' && *start != ';'; p++, start++)
	    *p = *start;
	*p = '\0';
	debug(0, "Found and copied match <%s>", match_buffer);
    }
    return 1;
}


