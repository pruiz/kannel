/*
 * CSD Router thread for bearer box (WAP/SMS Gateway)
 *
 * Interface by Kalle Marjola
 * Implementation by Mikael Gueck
 * ... for Wapit ltd.
 * 
 */

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "config.h"

#include "gwlib/gwlib.h"
#include "bb_msg.h"
#include "csdr.h"

CSDRouter *csdr_open(ConfigGroup *grp) {
	CSDRouter *router;
	char *interface_name;
	char *wap_service;
	int fl;
	int port;
	Octstr *os;

	router = gw_malloc(sizeof(CSDRouter));

        interface_name = config_get(grp, "interface-name");
        wap_service = config_get(grp, "wap-service");

	if (interface_name == NULL) {
		error(0, "You need to configure 'interface-name' for the "
			 "CSD router.");
		goto error;
	}

	if (wap_service == NULL) {
		error(0, "You need to configure a 'wap-service' for the "
			 "CSD router.");
		goto error;
	}

	if (strcmp(wap_service, "wsp") == 0) {
		port = 9200;
	} else if (strcmp(wap_service, "wsp/wtp") == 0) {
		port = 9201;
	} else if( strcmp(wap_service, "wsp/wtls") == 0 ) {
		port = 9202;
	} else if( strcmp(wap_service, "wsp/wtp/wtls") == 0 ) {
		port = 9203;
	} else if( strcmp(wap_service, "vcard") == 0 ) {
		port = 9204;
	} else if( strcmp(wap_service, "vcal") == 0 ) {
		port = 9205;
	} else if( strcmp(wap_service, "vcard/wtls") == 0 ) {
		port = 9206;
	} else if( strcmp(wap_service, "vcal/wtls") == 0 ) {
		port = 9207;
	} else {
		error(0, "Illegal configuration '%s' in 'wap-service'.",
			wap_service);
		goto error;
	}

	os = octstr_create(interface_name);
	router->addr = udp_create_address(os, port);
	octstr_destroy(os);
	if (router->addr == NULL) {
		error(0, "csdr_open: could not resolve interface <%s>",
			interface_name);
		goto error;
	}
	
	router->fd = udp_bind(port);

	fl = fcntl(router->fd, F_GETFL);
	fcntl(router->fd, F_SETFL, fl | O_NONBLOCK);

	os = udp_get_ip(router->addr);
	debug("bb.csdr", 0, "csdr_open: Bound to UDP <%s:%d> service <%s>.",
	      octstr_get_cstr(os),
	      udp_get_port(router->addr), 
	      wap_service);
	octstr_destroy(os);

	return router;

error:
	error(errno, "CSDR: csdr_open: could not open, aborting");
	gw_free(router);
	return NULL;
}

int csdr_close(CSDRouter *router) {
	if (router != NULL) {
		(void) close(router->fd);
		octstr_destroy(router->addr);
		gw_free(router);
	}
	return 0;
}

RQueueItem *csdr_get_message(CSDRouter *router) {
	int ret;
	RQueueItem *item = NULL;
	Octstr *datagram, *cliaddr;

	ret = udp_recvfrom(router->fd, &datagram, &cliaddr);
	if (ret == -1) {
		if (errno == EAGAIN) {
			/* No datagram available, don't block. */
			goto no_msg;
		}
		goto error;
	}
	
	item = rqi_new(R_MSG_CLASS_WAP, R_MSG_TYPE_MO);
	if(item==NULL) goto error;

	item->msg = msg_create(wdp_datagram);

	item->msg->wdp_datagram.source_address = udp_get_ip(cliaddr);
	item->msg->wdp_datagram.source_port    = udp_get_port(cliaddr);
	item->msg->wdp_datagram.destination_address = udp_get_ip(router->addr);
	item->msg->wdp_datagram.destination_port    = udp_get_port(router->addr);
	item->msg->wdp_datagram.user_data = datagram;

	/* set routing info: use client IP and port
	 */
	item->routing_info = gw_malloc(10*1024);
	sprintf(item->routing_info, "%s:%d", 
		octstr_get_cstr(item->msg->wdp_datagram.source_address),
		(int) item->msg->wdp_datagram.source_port);
	
	return item;

no_msg:
	return NULL;

error:
	error(0, "CSDR: could not receive UDP datagram");
	rqi_delete(item);
	return NULL;
}

int csdr_send_message(CSDRouter *router, RQueueItem *item) {
	Octstr *cliaddr;
	int ret;

	cliaddr = udp_create_address(item->msg->wdp_datagram.destination_address,
				item->msg->wdp_datagram.destination_port);
	ret = udp_sendto(router->fd, item->msg->wdp_datagram.user_data,
			 cliaddr);
	if (ret == -1)
		error(errno, "CSDR: could not send UDP datagram");
	octstr_destroy(cliaddr);
	return ret;
}

int csdr_is_to_us(CSDRouter *router, Msg *msg) {
	Octstr *addr;
	int same;
	
	gw_assert(router != NULL);
	gw_assert(msg != NULL);
	gw_assert(msg_type(msg) == wdp_datagram);
	
	addr = udp_create_address(msg->wdp_datagram.source_address,
				  msg->wdp_datagram.source_port);
	if (addr == NULL)
		return 0;

	same = (octstr_compare(router->addr, addr) == 0);
	octstr_destroy(addr);
	return same;
}
