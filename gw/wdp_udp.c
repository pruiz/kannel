/*
 * wdp_udp.c - implement UDP bearer for WDP
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
#include "wdp_udp.h"

WDP_UDPBearer *wdp_udp_open(ConfigGroup *grp) {
	WDP_UDPBearer *udp;
	char *interface_name;
	char *wap_service;
	int fl;
	int port;
	Octstr *os;

	udp = gw_malloc(sizeof(WDP_UDPBearer));

        interface_name = config_get(grp, "interface-name");
        wap_service = config_get(grp, "wap-service");

	if (interface_name == NULL) {
		error(0, "You need to configure 'interface-name' for the "
			 "UDP port.");
		goto error;
	}

	if (wap_service == NULL) {
		error(0, "You need to configure a 'wap-service' for the "
			 "UDP port.");
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
	udp->addr = udp_create_address(os, port);
	octstr_destroy(os);
	if (udp->addr == NULL) {
		error(0, "wdp_udp_open: could not resolve interface <%s>",
			interface_name);
		goto error;
	}
	
	udp->fd = udp_bind(port);

	fl = fcntl(udp->fd, F_GETFL);
	fcntl(udp->fd, F_SETFL, fl | O_NONBLOCK);

	os = udp_get_ip(udp->addr);
	debug("bb.udp", 0, "wdp_udp_open: Bound to UDP <%s:%d> service <%s>.",
	      octstr_get_cstr(os),
	      udp_get_port(udp->addr), 
	      wap_service);
	octstr_destroy(os);

	return udp;

error:
	error(errno, "WDP/UDP: wdp_udp_open: could not open, aborting");
	gw_free(udp);
	return NULL;
}

int wdp_udp_close(WDP_UDPBearer *udp) {
	if (udp != NULL) {
		(void) close(udp->fd);
		octstr_destroy(udp->addr);
		gw_free(udp);
	}
	return 0;
}

RQueueItem *wdp_udp_get_message(WDP_UDPBearer *udp) {
	int ret;
	RQueueItem *item = NULL;
	Octstr *datagram, *cliaddr;

	ret = udp_recvfrom(udp->fd, &datagram, &cliaddr);
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
	item->msg->wdp_datagram.destination_address = udp_get_ip(udp->addr);
	item->msg->wdp_datagram.destination_port    = udp_get_port(udp->addr);
	item->msg->wdp_datagram.user_data = datagram;

	/* set routing info: use client IP and port
	 */
	item->routing_info = gw_malloc(10*1024);
	sprintf(item->routing_info, "%s:%d", 
		octstr_get_cstr(item->msg->wdp_datagram.source_address),
		(int) item->msg->wdp_datagram.source_port);

	octstr_destroy(cliaddr);
	
	return item;

no_msg:
	return NULL;

error:
	error(0, "WDP/UDP: could not receive UDP datagram");
	rqi_delete(item);
	return NULL;
}

int wdp_udp_send_message(WDP_UDPBearer *udp, RQueueItem *item) {
	Octstr *cliaddr;
	int ret;

	cliaddr = udp_create_address(item->msg->wdp_datagram.destination_address,
				item->msg->wdp_datagram.destination_port);
	ret = udp_sendto(udp->fd, item->msg->wdp_datagram.user_data,
			 cliaddr);
	if (ret == -1)
		error(errno, "WDP/UDP: could not send UDP datagram");
	octstr_destroy(cliaddr);
	return ret;
}

int wdp_udp_is_to_us(WDP_UDPBearer *udp, Msg *msg) {
	Octstr *addr;
	int same;
	
	gw_assert(udp != NULL);
	gw_assert(msg != NULL);
	gw_assert(msg_type(msg) == wdp_datagram);
	
	addr = udp_create_address(msg->wdp_datagram.source_address,
				  msg->wdp_datagram.source_port);
	if (addr == NULL)
		return 0;

	same = (octstr_compare(udp->addr, addr) == 0);
	octstr_destroy(addr);
	return same;
}
