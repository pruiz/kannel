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
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "config.h"
#include "bb_msg.h"
#include "csdr.h"

CSDRouter *csdr_open(ConfigGroup *grp)
{

	CSDRouter *router = NULL;
	char *interface_name;
	char *wap_service;
	int fl;
	int i = 0;

	struct in_addr bindaddr;
	struct sockaddr_in servaddr;
	char server_ip[16], server_port[8];

	memset(server_ip, 0, sizeof(server_ip));
	memset(server_port, 0, sizeof(server_port));

	router = malloc(sizeof(CSDRouter));
	if(router==NULL) goto error;
	memset(router, 0, sizeof(CSDRouter));

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

	/* Select which network interface (IP) to bind to. */
	if(inet_aton(interface_name, &bindaddr) != 0) {
		servaddr.sin_addr = bindaddr;
		router->ip = strdup(interface_name);
	} else if(strcmp(interface_name, "*") == 0) {
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		router->ip = strdup("0.0.0.0");
	} else {
		error(0, "csdr_open: could not resolve interface <%s>",
			interface_name);
		goto error;
	}

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
		error(0, "Illegal configuration '%s' in 'wap-service'.",
			wap_service);
		goto error;
	}

	router->port = ntohs(servaddr.sin_port);

	while( bind(router->fd, &servaddr, sizeof(servaddr)) != 0 ) {

		error(errno, "Could not bind to UDP port <%i> service <%s>.", 
			ntohs(servaddr.sin_port), wap_service);

		if(errno==EACCES) {
			error(0, "csdr_open: could not bind to interface <%s:%d>",
				router->ip, router->port);
			goto error;
		}
		
		if(i++==10) {
			error(0, "csdr_open: could not bind to the interface");
			goto error;
		}

		sleep(1);

	}

	fl = fcntl(router->fd, F_GETFL);
	fcntl(router->fd, F_SETFL, fl | O_NONBLOCK);

	debug(0, "csdr_open: Bound to UDP <%s:%d> service <%s>.",
	      router->ip, router->port, wap_service);

	return router;

error:
	error(errno, "CSDR: csdr_open: could not open, aborting");
	free(router);
	return NULL;
}

int csdr_close(CSDRouter *router)
{
	if (router == NULL)
		return 0;

	close(router->fd);

	free(router->ip);
	free(router);
	
	return 0;
}

RQueueItem *csdr_get_message(CSDRouter *router)
{

	int length;
	RQueueItem *item = NULL;
	char data[64*1024];
	char client_ip[16], client_port[8];

	struct sockaddr_in cliaddr;
	socklen_t clilen;

	/* Initialize datasets. */
	memset(data, 0, sizeof(data));
	memset(client_ip, 0, sizeof(client_ip));
	memset(client_port, 0, sizeof(client_port));

	/* Maximum size of UDP datagram == 64*1024 bytes. */
	clilen = sizeof(cliaddr);
	length = recvfrom(router->fd, data, sizeof(data), 0, &cliaddr, &clilen);
	if(length==-1) {
		if(errno==EAGAIN) {
			/* No datagram available, don't block. */
			goto no_msg;
		}
		error(errno, "Error receiving datagram.");
		goto error;
	}

/*
 * XXX This is just temporary, the compatibility clause is
 * moving to wapitlib.c.
 */
#if defined(NI_NUMERICHOST)
	getnameinfo((struct sockaddr*)&cliaddr, clilen, 
		client_ip, sizeof(client_ip), 
		client_port, sizeof(client_port), 
		NI_NUMERICHOST | NI_NUMERICSERV);
#else
	{
		char *ptr = NULL;
		ptr = inet_ntoa(cliaddr.sin_addr);
		if(ptr==NULL) goto error;
		strncpy(client_ip, ptr, sizeof(client_ip));
		sprintf(client_port, "%i", ntohs(cliaddr.sin_port));
	}
#endif

	item = rqi_new(R_MSG_CLASS_WAP, R_MSG_TYPE_MO);
	if(item==NULL) goto error;

	item->msg = msg_create(wdp_datagram);
	if(item->msg==NULL) goto error;

	item->msg->wdp_datagram.source_address = octstr_create(client_ip);
	item->msg->wdp_datagram.source_port    = atoi(client_port);
	item->msg->wdp_datagram.destination_address = octstr_create(router->ip);
	item->msg->wdp_datagram.destination_port    = router->port;
	item->msg->wdp_datagram.user_data = octstr_create_from_data(data, length);
	debug(0, "csdr_get_message: message dump follows");
	msg_dump(item->msg);

	/* set routing info: use client IP and port
	 */
	item->routing_info = malloc(strlen(client_ip)+strlen(client_port)+2);
	if (item->routing_info == NULL)
	    goto error;
	sprintf(item->routing_info, "%s:%s", client_ip, client_port);
	
	return item;

no_msg:
	return NULL;
error:
	error(errno, "csdr_get_message: could not receive UDP datagram");
	rqi_delete(item);
	return NULL;
}

int csdr_send_message(CSDRouter *router, RQueueItem *item)
{

	char data[64*1024];
	size_t  datalen;
	struct sockaddr_in cliaddr;
	struct hostent *hostinfo;
	socklen_t clilen;

	memset(&cliaddr, 0, sizeof(struct sockaddr_in));

	debug(0, "CSDR: Time to send a message");

	/* Can only do 64k of data... have to def a MIN macro one of these times... -MG */
	datalen = (sizeof(data)<octstr_len(item->msg->wdp_datagram.user_data)) ? 
		sizeof(data) : octstr_len(item->msg->wdp_datagram.user_data);

	octstr_get_many_chars(data, item->msg->wdp_datagram.user_data, 0, datalen);

	hostinfo = gethostbyname(octstr_get_cstr(item->msg->wdp_datagram.destination_address));
	if (hostinfo == NULL) goto error;

        cliaddr.sin_family = AF_INET;
        cliaddr.sin_port = htons(item->msg->wdp_datagram.destination_port);
        cliaddr.sin_addr = *(struct in_addr *) hostinfo->h_addr;

	clilen = sizeof(cliaddr);

	sendto(router->fd, data, datalen, 0, &cliaddr, clilen);

	debug(0, "CSDR: Done.");

	return 0;
error:
	error(errno, "csdr_send_message: could not send UDP datagram");
	return -1;
}

int csdr_is_to_us(CSDRouter *router, Msg *msg) {

    if (router == NULL || msg == NULL) goto error;
    if (msg_type(msg) != wdp_datagram) goto error;

    debug(0, "<%s %d> vs <%s %d>", router->ip, router->port,
	  octstr_get_cstr(msg->wdp_datagram.source_address),
	  (int)msg->wdp_datagram.source_port);

    if (router->port != msg->wdp_datagram.source_port ||
	(strcmp(router->ip,
		octstr_get_cstr(msg->wdp_datagram.source_address)) != 0))
	{
		return 0;
	}

    return 1;

error:
    error(0, "csdr_is_to_us: illegal parameters");
    return -1;
}
