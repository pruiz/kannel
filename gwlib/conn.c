#include <signal.h>

#include "gwlib/gwlib.h"

struct Connection {
	volatile sig_atomic_t claimed;
#ifndef NDEBUG
	pthread_t claimer;
#endif

	int fd;

	Octstr *outbuf;
	long outbufpos;  /* start of unwritten data in outbuf */

	Octstr *inbuf;
	long inbufpos;   /* start of unread data in inbuf */
};

Connection *conn_open_tcp(Octstr *host, int port) {
	panic(0, "conn_open_tcp not implemented");
	return NULL;
}

Connection *conn_wrap_fd(int fd) {
	panic(0, "conn_wrap_fd not implemented");
	return NULL;
}

void conn_destroy(Connection *conn) {
	panic(0, "conn_destroy not implemented");
}

void conn_claim(Connection *conn) {
	panic(0, "conn_claim not implemented");
}

long conn_outbuf_len(Connection *conn) {
	gw_assert(conn != NULL);

	return octstr_len(conn->outbuf) - conn->outbufpos;
}

long conn_inbuf_len(Connection *conn) {
	gw_assert(conn != NULL);

	return octstr_len(conn->inbuf) - conn->inbufpos;
}

int conn_wait(Connection *conn, double seconds) {
	panic(0, "conn_wait not implemented");
	return -1;
}

int conn_write(Connection *conn, Octstr *data) {
	panic(0, "conn_write not implemented");
	return -1;
}

int conn_write_network_long(Connection *conn, long value) {
	panic(0, "conn_write not implemented");
	return -1;
}

int conn_write_data(Connection *conn, unsigned char *data, long length) {
	panic(0, "conn_write not implemented");
	return -1;
}

Octstr *conn_read(Connection *conn, int minimum) {
	panic(0, "conn_write not implemented");
	return NULL;
}

int conn_read_network_long(Connection *conn, long *valp) {
	panic(0, "conn_write not implemented");
	return -1;
}

Octstr *conn_read_line(Connection *conn) {
	panic(0, "conn_write not implemented");
	return NULL;
}

Octstr *conn_read_packet(Connection *conn, int startmark, int endmark) {
	panic(0, "conn_write not implemented");
	return NULL;
}
