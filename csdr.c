/*
 * CSD Router thread for bearer box (WAP/SMS Gateway)
 *
 * Interface by Kalle Marjola
 * Implementation by Mikael Gueck
 * ... for Wapit ltd.
 * 
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "config.h"
#include "bb_msg.h"
#include "csdr.h"

CSDRouter *csdr_open(ConfigGroup *grp)
{

	CSDRouter *router = NULL;
	char *interface_name;
	char *ip_version;

	struct sockaddr_in servaddr;

	router = malloc(sizeof(CSDRouter));
	if(router==NULL) goto error;
	FD_ZERO(&router->rset);

        interface_name = config_get(grp, "interface-name");
        ip_version = config_get(grp, "ip-version");

	/* Initialize the sockets. */
	router->wsp          = socket(PF_INET, SOCK_DGRAM, 0);
	router->wsp_wtls     = socket(PF_INET, SOCK_DGRAM, 0);
	router->wsp_wtp      = socket(PF_INET, SOCK_DGRAM, 0);
	router->wsp_wtp_wtls = socket(PF_INET, SOCK_DGRAM, 0);
	router->vcard        = socket(PF_INET, SOCK_DGRAM, 0);
	router->vcard_wtls   = socket(PF_INET, SOCK_DGRAM, 0);
	router->vcal         = socket(PF_INET, SOCK_DGRAM, 0);
	router->vcal_wtls    = socket(PF_INET, SOCK_DGRAM, 0);

	memset(&servaddr, 0, sizeof(struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	servaddr.sin_port = htons(9200);
	while( bind(router->wsp, &servaddr, sizeof(servaddr)) != 0 );

	servaddr.sin_port = htons(9201);
	while( bind(router->wsp_wtp, &servaddr, sizeof(servaddr)) != 0 );

	servaddr.sin_port = htons(9202);
	while( bind(router->wsp_wtls, &servaddr, sizeof(servaddr)) != 0 );

	servaddr.sin_port = htons(9203);
	while( bind(router->wsp_wtp_wtls, &servaddr, sizeof(servaddr)) != 0 );

	servaddr.sin_port = htons(9204);
	while( bind(router->vcard, &servaddr, sizeof(servaddr)) != 0 );

	servaddr.sin_port = htons(9205);
	while( bind(router->vcal, &servaddr, sizeof(servaddr)) != 0 );

	servaddr.sin_port = htons(9206);
	while( bind(router->vcard_wtls, &servaddr, sizeof(servaddr)) != 0 );

	servaddr.sin_port = htons(9207);
	while( bind(router->vcal_wtls, &servaddr, sizeof(servaddr)) != 0 );

	FD_SET(router->wsp, &router->rset);
	FD_SET(router->wsp_wtp, &router->rset);
	FD_SET(router->wsp_wtls, &router->rset);
	FD_SET(router->wsp_wtp_wtls, &router->rset);
	FD_SET(router->vcard, &router->rset);
	FD_SET(router->vcal, &router->rset);
	FD_SET(router->vcard_wtls, &router->rset);
	FD_SET(router->vcal_wtls, &router->rset);

#if 0
	for(;;) {

		FD_SET(udpfd, &rset);
		if( (nready = select(udpfd+1, &rset, NULL, NULL, NULL)) < 0) {
			if(errno==EINTR) continue;
			else return -1;
		}


			/* Receive the datagram from the UDP socket */
			len = sizeof(cliaddr);
			request_data_length = recvfrom(udpfd, request_data, 
				sizeof(request_data), 0, &cliaddr, &len);

#endif
	return router;

error:
	free(router);
	return NULL;
}

int csdr_close(CSDRouter *csdr)
{
	if (csdr == NULL)
		return 0;
	free(csdr);
	return 0;
}

RQueueItem *csdr_get_message(CSDRouter *csdr)
{
    return NULL;
}

int csdr_send_message(CSDRouter *csdr, RQueueItem *msg)
{
    return 0;
}
