/*
 * UDP send test
 * MG for WAPIT 
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "gwlib.h"

int main(int argc, char *argv[]) {

	struct sockaddr_in cliaddr, servaddr;
	struct hostent *hostinfo;
	socklen_t clilen;
	int fd;

	if(argc < 3) {
		printf("usage: udp_send hostname port data\n");
		exit(1);
	}

	for(fd=1;fd<argc;fd++) {
		debug("test.udp_send", 0, "argv[%i] = <%s>", fd, argv[fd]);
	}

        fd = socket(PF_INET, SOCK_DGRAM, 0);
        /* Initialize the sockets. */
        memset(&servaddr, 0, sizeof(struct sockaddr_in));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(32323);

        while( bind(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) != 0 ) 
	{
                error(errno, "Could not bind to UDP port <%i>.",
                        ntohs(servaddr.sin_port));
                sleep(1);
        }

	memset(&cliaddr, 0, sizeof(struct sockaddr_in));

	hostinfo = gethostbyname(argv[1]);
	if (hostinfo == NULL) goto error;

	clilen = sizeof(cliaddr);
        cliaddr.sin_family = AF_INET;
        cliaddr.sin_port = htons(atoi(argv[2]));
        cliaddr.sin_addr = *(struct in_addr *) hostinfo->h_addr;

	sendto(fd, argv[3], strlen(argv[3]), 0, (struct sockaddr *) &cliaddr, 
		clilen);

	close(fd);

	return 0;
error:
	close(fd);
	error(errno, "csdr_get_message: could not send UDP datagram");
	return -1;
}

