#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#include "gwlib/gwlib.h"
#include "gw/msg.h"

int main(void) {
	int i, s, ss, addrlen;
	struct sockaddr addr;
	Msg *msg;
	Octstr *os;
	
	gw_init_mem();

	s = make_server_socket(13002);
	if (s == -1)
		panic(0, "no server socket");
	ss = accept(s, &addr, &addrlen);
	debug("test", 0, "accept: %d", ss);
	
	msg = msg_create(wdp_datagram);
	if (msg == NULL)
		panic(0, "msg_create");
	msg->wdp_datagram.source_address = octstr_create("123");
	msg->wdp_datagram.source_port = 1;
	msg->wdp_datagram.destination_address = octstr_create("456");
	msg->wdp_datagram.destination_port = 2;
	msg->wdp_datagram.user_data = octstr_create("userdata");
	
	os = msg_pack(msg);
	
	for (i = 0; i < 10; ++i) {
		octstr_send(ss, os);
		sleep(1);
	}
	
	return 0;
}
