/*
 * test_udp.c - program to test UDP packet functions
 *
 * This program implements a simple ping-pong server.
 *
 * Lars Wirzenius
 */

#include "gwlib/gwlib.h"

static char usage[] = "\
Usage: test_udp client server_port\n\
       test_udp server server_port\n\
";

#define PING "ping"
#define PONG "pong"
#define TIMES 10

static void client(int port) {
	int i, s;
	Octstr *ping, *pong, *addr, *from;
	
	s = udp_client_socket();
	ping = octstr_create(PING);
	addr = udp_create_address(octstr_create("localhost"), port);
	if (s == -1 || addr == NULL)
		panic(0, "Couldn't set up client socket.");

	for (i = 0; i < TIMES; ++i) {
		if (udp_sendto(s, ping, addr) == -1)
			panic(0, "Couldn't send ping.");
		if (udp_recvfrom(s, &pong, &from) == -1)
			panic(0, "Couldn't receive pong");
		info(0, "Got <%s> from <%s:%d>", octstr_get_cstr(pong),
			octstr_get_cstr(udp_get_ip(from)), udp_get_port(from));
	}
}

static void server(int port) {
	int i, s;
	Octstr *ping, *pong, *from;
	
	s = udp_bind(port);
	pong = octstr_create(PONG);
	if (s == -1)
		panic(0, "Couldn't set up client socket.");

	for (i = 0; i < TIMES; ++i) {
		if (udp_recvfrom(s, &ping, &from) == -1)
			panic(0, "Couldn't receive ping");
		info(0, "Got <%s> from <%s:%d>", octstr_get_cstr(ping),
			octstr_get_cstr(udp_get_ip(from)), udp_get_port(from));
		if (udp_sendto(s, pong, from) == -1)
			panic(0, "Couldn't send pong.");
	}
}

int main(int argc, char **argv) {
	int port;
	
	gw_init_mem();

	if (argc != 3)
		panic(0, "Bad argument list\n%s", usage);
	
	port = atoi(argv[2]);

	if (strcmp(argv[1], "client") == 0)
		client(port);
	else
		server(port);
	return 0;
}
