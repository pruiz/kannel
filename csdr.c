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
#include <netdb.h>

#include "config.h"
#include "bb_msg.h"
#include "csdr.h"

CSDRouter *csdr_open(ConfigGroup *grp)
{

	CSDRouter *router = NULL;
	char *interface_name;
	char *wap_service;

	struct sockaddr_in servaddr;

	router = malloc(sizeof(CSDRouter));
	if(router==NULL) goto error;

        interface_name = config_get(grp, "interface-name");
        wap_service    = config_get(grp, "wap-service");

	/* We need these variables. */
	if(interface_name==NULL) {
		error(0, "You need to configure a 'interface-name' for the CSD router.");
		goto error;
	}

	if(wap_service==NULL) {
		error(0, "You need to configure a 'wap-service' for the CSD router.");
		goto error;
	}

	router->fd = socket(PF_INET, SOCK_DGRAM, 0);

	/* Initialize the sockets. */
	memset(&servaddr, 0, sizeof(struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(strcmp(wap_service, "wsp") == 0) {
		servaddr.sin_port = htons(9200);
	} else if( strcmp(wap_service, "wsp/wtp") == 0 ) {
		servaddr.sin_port = htons(9201);
	} else if( strcmp(wap_service, "wsp/wtls") == 0 ) {
		servaddr.sin_port = htons(9202);
	} else if( strcmp(wap_service, "wsp/wtp/wtls") == 0 ) {
		servaddr.sin_port = htons(9203);
	} else if( strcmp(wap_service, "vcard") == 0 ) {
		servaddr.sin_port = htons(9204);
	} else if( strcmp(wap_service, "vcal") == 0 ) {
		servaddr.sin_port = htons(9205);
	} else if( strcmp(wap_service, "vcard/wtls") == 0 ) {
		servaddr.sin_port = htons(9206);
	} else if( strcmp(wap_service, "vcal/wtls") == 0 ) {
		servaddr.sin_port = htons(9207);
	} else {
		error(0, "Illegal configuration '%s' in 'wap-service'.", wap_service);
	}

	while( bind(router->fd, &servaddr, sizeof(servaddr)) != 0 ) {
		error(errno, "Could not bind to UDP port <%i> service <%s>.", 
			ntohs(servaddr.sin_port), wap_service);
		sleep(1);
	}

	return router;

error:
	free(router);
	return NULL;
}

int csdr_close(CSDRouter *router)
{
	if (router == NULL)
		return 0;

	close(router->fd);

	free(router);
	return 0;
}

RQueueItem *csdr_get_message(CSDRouter *router)
{

	fd_set rset;
	int nready, length;
	RQueueItem *item = NULL;
	char data[64*1024];
	char client_ip[16], client_port[8];
	char server_ip[16], server_port[8];

	struct sockaddr_in cliaddr, servaddr;
	socklen_t len, servlen;

	FD_ZERO(&rset);
	FD_SET(router->fd, &rset);

	for(;;) {
		nready = select(router->fd+1, &rset, NULL, NULL, NULL);
		if(nready == -1) {
			if(errno==EINTR) continue;
			if(errno==EAGAIN) continue;
			goto error;
		} else {
			break;
		}
	}

	item = rqi_new(R_MSG_CLASS_WAP, R_MSG_TYPE_MO);
	if(item==NULL) goto error;

	/* Maximum size of UDP datagram == 64*1024 bytes. */	
	length = recvfrom(router->fd, data, sizeof(data), 0, &cliaddr, &len);

	item->msg->wdp_datagram.user_data = octstr_create_from_data(data, length);

	getsockname(router->fd, (struct sockaddr*)&servaddr, &servlen);

	getnameinfo((struct sockaddr*)&cliaddr, len, 
		client_ip, sizeof(client_ip), 
		client_port, sizeof(client_port), 
		NI_NUMERICHOST | NI_NUMERICSERV);

	getnameinfo((struct sockaddr*)&servaddr, servlen, 
		server_ip, sizeof(server_ip), 
		server_port, sizeof(server_port), 
		NI_NUMERICHOST | NI_NUMERICSERV);

	item->msg->wdp_datagram.source_address = octstr_create(client_ip);
	item->msg->wdp_datagram.source_port    = atoi(client_port);
	item->msg->wdp_datagram.destination_address = octstr_create(server_ip);
	item->msg->wdp_datagram.destination_port    = atoi(server_port);

	return item;

error:
	return NULL;
}

int csdr_send_message(CSDRouter *csdr, RQueueItem *msg)
{
    return 0;
}
