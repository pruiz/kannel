/* TODO: file header */
/* TODO: unlocked_close() on error */
/* TODO: have I/O functions check if connection is open */
/* TODO: handle eof when reading */

#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "gwlib/gwlib.h"

struct Connection {
	Mutex *mutex;
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

static void lock(Connection *conn) {
	gw_assert(conn != NULL);

	if (conn->claimed)
		gw_assert(pthread_equal(conn->claimer, pthread_self()));
	else
		mutex_lock(conn->mutex);
}

static void unlock(Connection *conn) {
	gw_assert(conn != NULL);

	if (!conn->claimed)
		mutex_unlock(conn->mutex);
}

static long unlocked_write(Connection *conn) {
	long ret;

	ret = octstr_write_data(conn->outbuf, conn->fd, conn->outbufpos);

	if (ret < 0)
		return -1;

	conn->outbufpos += ret;

	/* Heuristic: Discard the already-written data if it's more than
	 * half of the total.  This should keep the buffer size small
	 * without wasting too much cycles on moving data around. */
	if (conn->outbufpos > octstr_len(conn->outbuf) / 2) {
		octstr_delete(conn->outbuf, 0, conn->outbufpos);
		conn->outbufpos = 0;
	}

	return ret;
}

static void unlocked_read(Connection *conn) {
	unsigned char buf[4096];
	long len;

	if (conn->inbufpos > 0) {
		octstr_delete(conn->inbuf, 0, conn->inbufpos);
		conn->inbufpos = 0;
	}

	len = read(conn->fd, buf, sizeof(buf));
	if (len < 0) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		error(errno, "Error reading from fd %d:", conn->fd);
		return;
	}

	octstr_append_data(conn->inbuf, buf, len);
}

static Octstr *unlocked_get(Connection *conn, long length) {
	Octstr *result = NULL;

	gw_assert(octstr_len(conn->outbuf) - conn->outbufpos >= length);
	result = octstr_copy(conn->outbuf, conn->outbufpos, length);
	conn->outbufpos += length;

	return result;
}

Connection *conn_open_tcp(Octstr *host, int port) {
	panic(0, "conn_open_tcp not implemented");
	return NULL;
}

Connection *conn_wrap_fd(int fd) {
	Connection *conn;

	conn = gw_malloc(sizeof(*conn));
	conn->mutex = mutex_create();
	conn->claimed = 0;

	conn->outbuf = octstr_create_empty();
	conn->outbufpos = 0;
	conn->inbuf = octstr_create_empty();
	conn->inbufpos = 0;

	conn->fd = fd;

	return conn;
}

void conn_destroy(Connection *conn) {
	int ret;

	if (conn == NULL)
		return;

	/* No locking done here.  conn_destroy should not be called
	 * if any thread might still be interested in the connection. */

	if (conn->fd >= 0) {
		ret = close(conn->fd);
		if (ret < 0)
			error(errno, "conn_destroy: error on close");
		conn->fd = -1;
	}

	octstr_destroy(conn->outbuf);
	octstr_destroy(conn->inbuf);
	mutex_destroy(conn->mutex);

	gw_free(conn);
}

void conn_claim(Connection *conn) {
	if (conn->claimed)
		panic(0, "Connection is being claimed twice!");
	conn->claimed = 1;
#ifndef NDEBUG
	conn->claimer = pthread_self();
#endif
}

long conn_outbuf_len(Connection *conn) {
	long len;

	lock(conn);
	len = octstr_len(conn->outbuf) - conn->outbufpos;
	unlock(conn);

	return len;
}

long conn_inbuf_len(Connection *conn) {
	long len;

	lock(conn);
	len = octstr_len(conn->inbuf) - conn->inbufpos;
	unlock(conn);

	return len;
}

int conn_wait(Connection *conn, double seconds) {
	panic(0, "conn_write not implemented");
	return -1;
}

int conn_write(Connection *conn, Octstr *data) {
	long ret;

	lock(conn);
	octstr_append(conn->outbuf, data);
	ret = unlocked_write(conn);
	unlock(conn);

	return ret;
}

int conn_write_data(Connection *conn, unsigned char *data, long length) {
	long ret;

	lock(conn);
	octstr_append_data(conn->outbuf, data, length);
	ret = unlocked_write(conn);
	unlock(conn);

	return ret;
}

Octstr *conn_read_fixed(Connection *conn, long length) {
	Octstr *result = NULL;

	/* See if the data is already available.  If not, try a read(),
	 * then see if we have enough data after that.  If not, give up. */
	lock(conn);
	if (octstr_len(conn->inbuf) - conn->inbufpos < length) {
		unlocked_read(conn);
		if (octstr_len(conn->inbuf) - conn->inbufpos < length) {
			unlock(conn);
			return NULL;
		}
	}
	result = unlocked_get(conn, length);
	unlock(conn);

	return result;
}

Octstr *conn_read_line(Connection *conn) {
	Octstr *result = NULL;
	long pos;

	lock(conn);
	/* 10 is the code for linefeed.  We don't rely on \n because that
	 * might be a different value on some (strange) systems. */
	pos = octstr_search_char_from(conn->inbuf, 10, conn->inbufpos);
	if (pos < 0) {
		unlocked_read(conn);
		pos = octstr_search_char_from(conn->inbuf,
					10, conn->inbufpos);
		if (pos < 0) {
			unlock(conn);
			return NULL;
		}
	}
		
	result = unlocked_get(conn, pos - conn->inbufpos);
	/* Check if it was a CR LF */
	if (octstr_len(result) > 0 &&
	    octstr_get_char(result, octstr_len(result) - 1) == 13)
		octstr_delete(result, octstr_len(result) - 1, 1);

	return result;
}

Octstr *conn_read_packet(Connection *conn, int startmark, int endmark) {
	panic(0, "conn_read_packet not implemented");
	return NULL;
}
