/*
 * wapbox.h - main program for WAP box
 *
 * This module contains the main program for the WAP box of the WAP gateway.
 * See the architecture documentation for details.
 *
 * Lars Wirzenius <liw@wapit.com> for WapIT Ltd.
 */

#include <stdlib.h>

#include "wapitlib.h"
#include "octstr.h"
#include "msg.h"
#include "config.h"
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
	
	if (octstr_recv(s, &os) == -1)
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
	WTPEvent *event;

	info(0, "WAP box starting up.");

	if (argc > 1)
		read_config(argv[1]);
	else
		read_config("wapbox.conf");

	bbsocket = connect_to_bearer_box();
	for (;;) {
		msg = msg_receive(bbsocket);
		if (msg == NULL)
			break;
		msg_dump(msg);
		event = wtp_unpack_wdp_datagram(msg);
		wtp_event_dump(event);
#if 0
		machine = create_or_find_wtp_machine(event);
		debug(0, "Ignoring stuff since implementation isn't done.");
#endif
	}
	
	info(0, "WAP box terminating.");
	return 0;
}
