/*
 * wapbox.h - main program for WAP box
 *
 * This module contains the main program for the WAP box of the WAP gateway.
 * See the architecture documentation for details.
 *
 * Lars Wirzenius <liw@wapit.com> for WapIT Ltd.
 */

#include <stdlib.h>

#include "gwlib.h"
#include "msg.h"
#include "wtp.h"

static char *bearerbox_host = NULL;
static int bearerbox_port = -1;

static void read_config(char *filename) {
	Config *cfg;
	ConfigGroup *grp;
	char *s;

	cfg = config_create(filename);
	if (config_read(cfg) == -1)
		panic(0, "Couldn't read configuration from `%s'.", filename);
	config_dump(cfg);
	
	grp = config_first_group(cfg);
	while (grp != NULL) {
		if ((s = config_get(grp, "bearerbox-host")) != NULL)
			bearerbox_host = s;
		if ((s = config_get(grp, "bearerbox-port")) != NULL)
			bearerbox_port = atoi(s);
		grp = config_next_group(grp);
	}

	debug(0, "host: %s", bearerbox_host);
	debug(0, "port: %d", bearerbox_port);
}


static int connect_to_bearer_box(void) {
	int s;
	
	s = tcpip_connect_to_server(bearerbox_host, bearerbox_port);
	if (s == -1)
		panic(0, "Couldn't connect to bearer box %s:%d.",
			bearerbox_host, bearerbox_port);
	return s;
}


static Msg *msg_receive(int s) {
	Octstr *os;
	Msg *msg;
	
	if (octstr_recv(s, &os) < 1)
		return NULL;
	msg = msg_unpack(os);
	if (msg == NULL)
		return NULL;
	octstr_destroy(os);
	return msg;
}


int main(int argc, char **argv) {
	int bbsocket;
	Msg *msg;
	WTPEvent *wtp_event = NULL;
        WTPMachine *wtp_machine = NULL;

	info(0, "WAP box starting up.");

	if (argc > 1)
		read_config(argv[1]);
	else
		read_config("wapbox.wapconf");

	bbsocket = connect_to_bearer_box();
	for (;;) {
		msg = msg_receive(bbsocket);
		if (msg == NULL)
			break;
		wtp_event = wtp_unpack_wdp_datagram(msg);
                debug(0, "wapbox:datagram unpacked");
                if (wtp_event == NULL)
                   continue;
		wtp_machine = wtp_machine_find_or_create(msg, wtp_event);
                if (wtp_machine == NULL)
                   continue;
                debug(0, "wapbox: returning create machine");
	        wtp_handle_event(wtp_machine, wtp_event);
                debug(0,"wapbox: returning handle_event");
	}
	
	info(0, "WAP box terminating.");
	return 0;
}
