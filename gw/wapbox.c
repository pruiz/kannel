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


static void msg_send(int s, Msg *msg) {
	Octstr *os;

	debug(0, "msg_send: sending message:");
	msg_dump(msg);
	os = msg_pack(msg);
	if (os == NULL)
		panic(0, "msg_pack failed");
	if (octstr_send(s, os) == -1)
		error(0, "wapbox: octstr_send failed");
	octstr_destroy(os);
	debug(0, "wapbox: wrote msg %p", (void *) msg);
}


#define MAX_MSGS_IN_QUEUE 1024
static Msg *queue_tab[MAX_MSGS_IN_QUEUE];
static int queue_start = 0;
static int queue_len = 0;
static Mutex *queue_mutex;


void init_queue(void) {
	queue_mutex = mutex_create();
}


void put_msg_in_queue(Msg *msg) {
	mutex_lock(queue_mutex);
	debug(0, "wapbox: putting msg %p in queue", (void *) msg);
	if (queue_len == MAX_MSGS_IN_QUEUE)
		error(0, "wapbox: message queue full, dropping message");
	else {
		queue_tab[(queue_start + queue_len) % MAX_MSGS_IN_QUEUE] = msg;
		++queue_len;
	}
	mutex_unlock(queue_mutex);
}


Msg *remove_msg_from_queue(void) {
	Msg *msg;
	
	mutex_lock(queue_mutex);
	if (queue_len == 0)
		msg = NULL;
	else {
		msg = queue_tab[queue_start];
		queue_start = (queue_start + 1) % MAX_MSGS_IN_QUEUE;
		--queue_len;
		debug(0, "wapbox: removed msg %p in queue", (void *) msg);
	}
	mutex_unlock(queue_mutex);
	return msg;
}


int main(int argc, char **argv) {
	int bbsocket;
	Msg *msg;
	WTPEvent *wtp_event = NULL;
        WTPMachine *wtp_machine = NULL;

	open_logfile("wapbox.log", DEBUG);

	info(0, "------------------------------------------------------------");
	info(0, "WAP box starting up.");

	if (argc > 1)
		read_config(argv[1]);
	else
		read_config("wapbox.wapconf");
		
	bbsocket = connect_to_bearer_box();
	init_queue();
	for (;;) {
		msg = msg_receive(bbsocket);
		if (msg == NULL)
			break;
		debug(0, "wapbox: received datagram, unpacking it...");
		wtp_event = wtp_unpack_wdp_datagram(msg);
                if (wtp_event == NULL)
                   continue;
#if 0
                debug(0, "wapbox: datagram unpacked:");
		wtp_event_dump(wtp_event);
#endif
		wtp_machine = wtp_machine_find_or_create(msg, wtp_event);
                if (wtp_machine == NULL)
                   continue;
                debug(0, "wapbox: returning create machine");
	        wtp_handle_event(wtp_machine, wtp_event);

		for (;;) {
			msg = remove_msg_from_queue();
			if (msg == NULL)
				break;
			msg_send(bbsocket, msg);
		}
	}
	
	info(0, "WAP box terminating.");
	return 0;
}
