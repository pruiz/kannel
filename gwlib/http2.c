/*
 * http2.c - HTTP protocol implementation
 *
 * Lars Wirzenius
 */

#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "gwlib.h"
#include "http2.h"


/*
 * Data and functions needed to support proxy operations. If proxy_hostname 
 * is NULL, no proxy is used.
 */
static Mutex *proxy_mutex = NULL;
static Octstr *proxy_hostname = NULL;
static int proxy_port = 0;
static List *proxy_exceptions;

static int proxy_used_for_host(Octstr *host);


/*
 * Stuff that should be moved to other modules.
 */
static int octstr_str_ncompare(Octstr *os, char *cstr);
static void octstr_append(Octstr *to, Octstr *os);


/*
 * Declarations for the socket pool.
 */
typedef struct PoolSocket PoolSocket;
static void pool_init(void);
static void pool_shutdown(void);
static PoolSocket *pool_socket_create(Octstr *host, int port);
static void pool_socket_destroy(PoolSocket *p);
static int pool_allocate(Octstr *host, int port);
static void pool_free(int socket);
static void pool_free_and_close(int socket);
static Octstr *pool_get_buffer(int socket);
static int pool_same_socket(void *, void *);
static int pool_socket_is_alive(PoolSocket *p);
static int pool_socket_reopen(PoolSocket *p);
static void pool_kill_old_ones(void);
static int pool_socket_old_and_unused(void *a, void *b);


/*
 * Operations on sockets from the socket pool.
 */
static int socket_read_line(int socket, Octstr **line);
static int socket_read_bytes(int socket, Octstr **os, long bytes);
static int socket_read_to_eof(int socket, Octstr **os);


/*
 * Other operations.
 */
static int parse_url(Octstr *url, Octstr **host, long *port, Octstr **path);
static Octstr *build_request(Octstr *host, Octstr *path, List *headers);
static int parse_status(Octstr *statusline);
static int send_request(Octstr *url, List *request_headers);
static int read_status(int socket);
static int read_headers(int socket, List **headers);
static int read_body(int socket, List *headers, Octstr **body);
static int read_chunked_body(int socket, Octstr **body, List *headers);
static int read_raw_body(int socket, Octstr **body, long bytes);


void http2_init(void) {
	proxy_mutex = mutex_create();
	proxy_exceptions = list_create();
	pool_init();
}


void http2_shutdown(void) {
	http2_close_proxy();
	list_destroy(proxy_exceptions);
	mutex_destroy(proxy_mutex);
	pool_shutdown();
}


void http2_use_proxy(Octstr *hostname, int port, List *exceptions) {
	int i;

	http2_close_proxy();
	mutex_lock(proxy_mutex);
	proxy_hostname = octstr_duplicate(hostname);
	proxy_port = port;
	for (i = 0; i < list_len(exceptions); ++i)
		list_append(proxy_exceptions, 
			    octstr_duplicate(list_get(exceptions, i)));
	mutex_unlock(proxy_mutex);
}


void http2_close_proxy(void) {
	Octstr *p;

	mutex_lock(proxy_mutex);
	if (proxy_hostname != NULL) {
		octstr_destroy(proxy_hostname);
		proxy_hostname = NULL;
	}
	proxy_port = 0;
	while ((p = list_extract_first(proxy_exceptions)) != NULL)
		octstr_destroy(p);
	mutex_unlock(proxy_mutex);
}


int http2_get(Octstr *url, List *request_headers, 
List **reply_headers, Octstr **reply_body)  {
	int s, status;

	s = send_request(url, request_headers);
	if (s == -1)
		goto error;
	
	status = read_status(s);
	if (status < 0)
		goto error;
	
	if (read_headers(s, reply_headers) == -1)
		goto error;

	switch (read_body(s, *reply_headers, reply_body)) {
	case -1:
		goto error;
	case 0:
		pool_free_and_close(s);
		break;
	default:
		pool_free(s);
	}

	return status;

error:
	pool_free(s);
	error(0, "Couldn't fetch <%s>", octstr_get_cstr(url));
	return -1;
}


int http2_get_real(Octstr *url, List *request_headers, Octstr **final_url,
List **reply_headers, Octstr **reply_body) {
	int i, ret;
	Octstr *h;

	ret = -1;
	
	*final_url = octstr_duplicate(url);
	for (i = 0; i < HTTP_MAX_FOLLOW; ++i) {
		ret = http2_get(*final_url, request_headers, reply_headers, 
				reply_body);
		if (ret == -1)
			return -1;
		if (ret != HTTP_MOVED_PERMANENTLY && ret != HTTP_FOUND &&
		    ret != HTTP_SEE_OTHER)
			break;
		h = http2_header_find_first(*reply_headers, "Location");
		if (h == NULL) {
			ret = -1;
			break;
		}
		octstr_strip_blank(h);
		octstr_destroy(*final_url);
		*final_url = h;
		while ((h = list_extract_first(*reply_headers)) != NULL)
			octstr_destroy(h);
		list_destroy(*reply_headers);
		octstr_destroy(*reply_body);
	}
	if (ret == -1) {
		octstr_destroy(*final_url);
		*final_url = NULL;
	}
	
	return ret;
}


Octstr *http2_header_find_first(List *headers, char *name) {
	long i, name_len;
	Octstr *h;

	name_len = strlen(name);

	for (i = 0; i < list_len(headers); ++i) {
		h = list_get(headers, i);
		if (octstr_str_ncompare(h, name) == 0 &&
		    octstr_get_char(h, name_len) == ':')
			return octstr_copy(h, name_len + 1, octstr_len(h));
	}
	return NULL;
}


void http2_header_get_content_type(List *headers, Octstr **type, 
Octstr **charset) {
	Octstr *h;
	long semicolon;
	
	h = http2_header_find_first(headers, "Content-Type");
	if (h == NULL) {
		*type = octstr_create("application/octet-stream");
		*charset = octstr_create_empty();
	} else {
		octstr_strip_blank(h);
		semicolon = octstr_search_char(h, ';');
		if (semicolon == -1) {
			*type = h;
			*charset = octstr_create_empty();
		} else {
			/* XXX this is bogus, but just barely good enough */
			octstr_delete(h, semicolon, octstr_len(h));
			octstr_strip_blank(h);
			*type = h;
			*charset = octstr_create_empty();
		}
	}
}




/***********************************************************************
 * Functions that should be moved to other modules but which are here
 * while development happens because of simplicity.
 */
 
 
/*
 * Like octstr_str_compare, but compare only strlen(cstr) first bytes of
 * the octet string.
 */
static int octstr_str_ncompare(Octstr *os, char *cstr) {
	long i, len;
	unsigned char *p;
	
	len = strlen(cstr);
	p = (unsigned char *) cstr;
	for (i = 0; i < len && octstr_get_char(os, i) == p[i]; ++i)
		continue;
	if (i == len)
		return 0;
	if (octstr_get_char(os, i) < p[i])
		return -1;
	return 1;
}


/*
 * Append contents of `os' to `to'. `os' remains untouched.
 */
static void octstr_append(Octstr *to, Octstr *os) {
	octstr_append_data(to, octstr_get_cstr(os), octstr_len(os));
}


/***********************************************************************
 * Proxy support functions.
 */


int proxy_used_for_host(Octstr *host) {
	int i;
	
	if (proxy_hostname == NULL)
		return 0;

	for (i = 0; i < list_len(proxy_exceptions); ++i)
		if (octstr_compare(host, list_get(proxy_exceptions, i)) == 0)
			return 0;

	return 1;
}


/***********************************************************************
 * Socket pool management.
 */


enum { POOL_MAX_IDLE = 300 };


struct PoolSocket {
	int in_use;
	time_t last_used;
	int socket;
	Octstr *host;
	int port;
	Octstr *buffer;
};


static List *pool = NULL;


static void pool_init(void) {
	pool = list_create();
}


static void pool_shutdown(void) {
	PoolSocket *p;
	
	while ((p = list_extract_first(pool)) != NULL)
		pool_socket_destroy(p);
	list_destroy(pool);
}


static PoolSocket *pool_socket_create(Octstr *host, int port) {
	PoolSocket *p;

	debug("gwlib.http2", 0, "HTTP2: Creating a new socket <%s:%d>.",
		octstr_get_cstr(host), port);
	p = gw_malloc(sizeof(PoolSocket));
	p->socket = tcpip_connect_to_server(octstr_get_cstr(host), port);

	if (p->socket == -1) {
		free(p);
		return NULL;
	}

	p->in_use = 0;
	p->last_used = (time_t) -1;
	p->host = octstr_duplicate(host);
	p->port = port;
	p->buffer = octstr_create_empty();

	return p;
}


static void pool_socket_destroy(PoolSocket *p) {
	gw_assert(p != NULL);

	debug("gwlib.http2", 0, "HTTP2: Closing socket to <%s:%d>",
		octstr_get_cstr(p->host), p->port);
	(void) close(p->socket);
	octstr_destroy(p->host);
	octstr_destroy(p->buffer);
	gw_free(p);
}


static int pool_allocate(Octstr *host, int port) {
	PoolSocket *p;
	int i;
	
	list_lock(pool);
	
	p = NULL;
	for (i = 0; i < list_len(pool); ++i) {
		p = list_get(pool, i);
		if (p->in_use && p->port == port &&
		    octstr_compare(p->host, host) == 0) {
			break;
		}
	}

	if (i == list_len(pool)) {
		p = pool_socket_create(host, port);
		if (p == NULL) {
			list_unlock(pool);
			return -1;
		}
		pool_kill_old_ones();
		list_append(pool, p);
	} else {
		debug("gwlib.http2", 0, "HTTP2: Re-using old socket.");
		gw_assert(p != NULL);
		if (!pool_socket_is_alive(p) && pool_socket_reopen(p) == -1) {
			list_unlock(pool);
			return -1;
		}
	}

	p->in_use = 1;
	list_unlock(pool);

	return p->socket;
}


static void pool_free(int socket) {
	List *list;
	PoolSocket *p;
	
	list_lock(pool);
	
	list = list_search_all(pool, &socket, pool_same_socket);
	gw_assert(list_len(list) == 1);
	p = list_get(list, 0);
	list_destroy(list);
	
	gw_assert(p->in_use);
	gw_assert(p->socket == socket);
	
	time(&p->last_used);
	p->in_use = 0;
	
	list_unlock(pool);
}


static void pool_free_and_close(int socket) {
	List *list;
	PoolSocket *p;
	
	list_lock(pool);
	list = list_extract_all(pool, &socket, pool_same_socket);
	list_unlock(pool);

	gw_assert(list_len(list) == 1);
	p = list_get(list, 0);
	list_destroy(list);
	
	gw_assert(p->in_use);
	gw_assert(p->socket == socket);
	pool_socket_destroy(p);
}


static Octstr *pool_get_buffer(int socket) {
	PoolSocket *p;
	
	list_lock(pool);
	p = list_search(pool, &socket, pool_same_socket);
	list_unlock(pool);
	gw_assert(p != NULL);
	return p->buffer;
}


static int pool_same_socket(void *a, void *b) {
	int socket;
	PoolSocket *p;
	
	p = a;
	socket = *(int *) b;
	return p->socket == socket;
}
 
 
static int pool_socket_is_alive(PoolSocket *p) {
	switch (read_available(p->socket, 0)) {
	case -1:
		return 0;

	case 0:
		return 1;
		
	default:
		if (octstr_append_from_socket(p->buffer, p->socket) <= 0)
			return 0;
		return 1;
	}
}


static int pool_socket_reopen(PoolSocket *p) {
	debug("gwlib.http2", 0, "HTTP2: Re-opening socket.");
	(void) close(p->socket);
	p->socket = tcpip_connect_to_server(octstr_get_cstr(p->host), p->port);
	if (p->socket == -1)
		return -1;
	return 0;
}


/* Assume pool is locked already. */
static void pool_kill_old_ones(void) {
	time_t now;
	List *list;
	PoolSocket *p;

	time(&now);
	list = list_extract_all(pool, &now, pool_socket_old_and_unused);
	if (list != NULL) {
		while ((p = list_extract_first(list)) != NULL)
			pool_socket_destroy(p);
		list_destroy(list);
	}
}


static int pool_socket_old_and_unused(void *a, void *b) {
	PoolSocket *p;
	time_t now;
	
	p = a;
	now = *(time_t *) b;
	return !p->in_use && p->last_used != (time_t) -1 &&
		difftime(now, p->last_used) > POOL_MAX_IDLE;
}


/***********************************************************************
 * Operations on sockets from the socket pool.
 */
 
 
/*
 * Read a line from a socket and/or its associated buffer. Remove the
 * line from the buffer. Fill the buffer with new data from the socket,
 * if the buffer did not already have enough.
 *
 * Return -1 for error, 0 for EOF, >0 for line. The will will have its
 * line endings (\r\n or \n) removed.
 */
static int socket_read_line(int socket, Octstr **line) {
	int newline;
	Octstr *buffer;
	
	buffer = pool_get_buffer(socket);

	while ((newline = octstr_search_char(buffer, '\n')) == -1) {
		switch (octstr_append_from_socket(buffer, socket)) {
		case -1:
			return -1;
		case 0:
			return 0;
		}
	}

	if (newline > 0 && octstr_get_char(buffer, newline-1) == '\r')
		*line = octstr_copy(buffer, 0, newline - 1);
	else
		*line = octstr_copy(buffer, 0, newline);
	octstr_delete(buffer, 0, newline + 1);
debug("", 0, "read line: <%s>", octstr_get_cstr(*line));

	return 1;
}


/*
 * Read `bytes' bytes from the socket `socket'. Use the pool buffer and
 * and fill it with new data from the socket as necessary. Return -1 for
 * error, 0 for EOF, or >0 for OK (exactly `bytes' bytes in `os'
 * returned). If OK, caller must free `*os'.
 */
static int socket_read_bytes(int socket, Octstr **os, long bytes) {
	Octstr *buffer;
	
	buffer = pool_get_buffer(socket);
	while (octstr_len(buffer) < bytes) {
		switch (octstr_append_from_socket(buffer, socket)) {
		case -1:
			return -1;
		case 0:
			return 0;
		}
	}
	*os = octstr_copy(buffer, 0, bytes);
	octstr_delete(buffer, 0, bytes);
	return 1;
}



/*
 * Read all remaining data from socket. Return -1 for error, 0 for OK.
 */
static int socket_read_to_eof(int socket, Octstr **os) {
	Octstr *buffer;
	
	buffer = pool_get_buffer(socket);
	for (;;) {
		switch (octstr_append_from_socket(buffer, socket)) {
		case -1:
			return -1;
		case 0:
			*os = octstr_duplicate(buffer);
			octstr_delete(buffer, 0, octstr_len(buffer));
			return 0;
		}
	}
}


/***********************************************************************
 * Other local functions.
 */
 
 
/*
 * Parse the URL to get the hostname and the port to connect to and the
 * path within the host.
 *
 * Return -1 if the URL seems malformed.
 *
 * We assume HTTP URLs of the form specified in "3.2.2 http URL" in
 * RFC 2616:
 * 
 *  http_URL = "http:" "//" host [ ":" port ] [ abs_path [ "?" query ]]
 */
static int parse_url(Octstr *url, Octstr **host, long *port, Octstr **path) {
	static char prefix[] = "http://";
	static int prefix_len = sizeof(prefix) - 1;
	int host_len, colon, slash;

	if (octstr_search_cstr(url, prefix) != 0) {
		error(0, "URL <%s> doesn't start with `%s'",
			octstr_get_cstr(url), prefix);
		return -1;
	}
	
	if (octstr_len(url) == prefix_len) {
		error(0, "URL <%s> is malformed.", octstr_get_cstr(url));
		return -1;
	}

	colon = octstr_search_char_from(url, ':', prefix_len);
	slash = octstr_search_char_from(url, '/', prefix_len);
	if (colon == prefix_len || slash == prefix_len) {
		error(0, "URL <%s> is malformed.", octstr_get_cstr(url));
		return -1;
	}

	if (slash == -1 && colon == -1) {
		/* Just the hostname, no port or path. */
		host_len = octstr_len(url) - prefix_len;
		*port = HTTP_PORT;
	} else if (slash == -1) {
		/* Port, but not path. */
		host_len = colon - prefix_len;
		if (octstr_parse_long(port, url, colon+1, 10) == -1) {
			error(0, "URL <%s> has malformed port number.",
				octstr_get_cstr(url));
			return -1;
		}
	} else if (colon == -1 || colon > slash) {
		/* Path, but not port. */
		host_len = slash - prefix_len;
		*port = HTTP_PORT;
	} else if (colon < slash) {
		/* Both path and port. */
		host_len = colon - prefix_len;
		if (octstr_parse_long(port, url, colon+1, 10) == -1) {
			error(0, "URL <%s> has malformed port number.",
				octstr_get_cstr(url));
			return -1;
		}
	} else {
		error(0, "Internal error in URL parsing logic.");
		return -1;
	}
	
	*host = octstr_copy(url, prefix_len, host_len);
	if (slash == -1)
		*path = octstr_create("/");
	else
		*path = octstr_copy(url, slash, octstr_len(url) - slash);

	return 0;
}



/*
 * Build a complete HTTP request given the host, port, path and headers. 
 * Add Host: and Content-Length: headers (and others that may be necessary).
 * Return the request as an Octstr.
 */
static Octstr *build_request(Octstr *path_or_url, Octstr *host, List *headers) {
/* XXX headers missing */
	Octstr *request;
	int i;

	request = octstr_create_empty();
	octstr_append_cstr(request, "GET ");
	octstr_append_cstr(request, octstr_get_cstr(path_or_url));
	octstr_append_cstr(request, " HTTP/1.1\r\nHost: ");
	octstr_append_cstr(request, octstr_get_cstr(host));
	octstr_append_cstr(request, "\r\nContent-Length: 0\r\n");
	for (i = 0; headers != NULL && i < list_len(headers); ++i) {
		octstr_append(request, list_get(headers, i));
		octstr_append_cstr(request, "\r\n");
	}
	octstr_append_cstr(request, "\r\n");
	return request;
}


/*
 * Parse the status line returned by an HTTP server and return the
 * status code. Return -1 if the status line was unparseable.
 */
static int parse_status(Octstr *statusline) {
	static char *versions[] = {
		"HTTP/1.1 ",
		"HTTP/1.0 ",
	};
	static int num_versions = sizeof(versions) / sizeof(versions[0]);
	long status;
	int i;

	for (i = 0; i < num_versions; ++i) {
		if (octstr_str_ncompare(statusline, versions[i]) == 0)
			break;
	}
	if (i == num_versions) {
		error(0, "HTTP2: Server responds with unknown HTTP version.");
		debug("gwlib.http2", 0, "Status line: <%s>",
			octstr_get_cstr(statusline));
		return -1;
	}

	if (octstr_parse_long(&status, statusline, 
	                      strlen(versions[i]), 10) == -1) {
		error(0, "HTTP2: Malformed status line from HTTP server: <%s>",
			octstr_get_cstr(statusline));
		return -1;
	}

	return status;
}



/*
 * Build and send the HTTP request. Return socket from which the
 * response can be read or -1 for error.
 */
static int send_request(Octstr *url, List *request_headers) {
	Octstr *host, *path, *request;
	long port;
	int s;

	host = NULL;
	path = NULL;
	request = NULL;
	s = -1;

	if (parse_url(url, &host, &port, &path) == -1)
		goto error;

	mutex_lock(proxy_mutex);
	if (proxy_used_for_host(host)) {
		request = build_request(url, host, request_headers);
		s = pool_allocate(proxy_hostname, proxy_port);
	} else {
		request = build_request(path, host, request_headers);
		s = pool_allocate(host, port);
	}
	mutex_unlock(proxy_mutex);
	if (s == -1)
		goto error;
	
	if (octstr_write_to_socket(s, request) == -1)
		goto error;

	octstr_destroy(host);
	octstr_destroy(path);
	octstr_destroy(request);

	return s;

error:
	octstr_destroy(host);
	octstr_destroy(path);
	octstr_destroy(request);
	if (s != -1)
		(void) close(s);
	error(0, "Couldn't fetch <%s>", octstr_get_cstr(url));
	return -1;
}


/*
 * Read and parse the status response line from an HTTP server.
 * Return the parsed status code. Return -1 for error.
 */
static int read_status(int socket) {
	Octstr *line;
	long status;

	if (socket_read_line(socket, &line) <= 0) {
		error(0, "HTTP2: Couldn't read status line from server.");
		return -1;
	}
	status = parse_status(line);
	octstr_destroy(line);
	return status;
}


/*
 * Read the headers, i.e., until the first empty line (read and discard
 * the empty line as well). Return -1 for error, 0 for OK.
 */
static int read_headers(int socket, List **headers) {
	Octstr *line, *prev;

	*headers = list_create();
	prev = NULL;
	for (;;) {
		if (socket_read_line(socket, &line) <= 0) {
			error(0, "HTTP2: Incomplete response from server.");
			goto error;
		}
		if (octstr_len(line) == 0) {
			octstr_destroy(line);
			break;
		}
		if (isspace(octstr_get_char(line, 0)) && prev != NULL) {
			octstr_append(prev, line);
			octstr_destroy(line);
		} else {
			list_append(*headers, line);
			prev = line;
		}
	}

	return 0;

error:
	while ((line = list_extract_first(*headers)) != NULL)
		octstr_destroy(line);
	list_destroy(*headers);
	return -1;
}


/*
 * Read the body of a response. Return -1 for error, 0 for OK (EOF on
 * socket reached) or 1 for OK (socket can be used for another transaction).
 */
static int read_body(int socket, List *headers, Octstr **body)
{
	Octstr *h;
	long body_len;

	h = http2_header_find_first(headers, "Transfer-Encoding");
	if (h != NULL) {
		octstr_strip_blank(h);
		if (octstr_str_compare(h, "chunked") != 0) {
			error(0, "HTTP2: Unknown Transfer-Encoding <%s>",
				octstr_get_cstr(h));
			goto error;
		}
		octstr_destroy(h);
		h = NULL;
		if (read_chunked_body(socket, body, headers) == -1)
			goto error;
		return 1;
	} else {
		h = http2_header_find_first(headers, "Content-Length");
		if (h == NULL) {
			if (socket_read_to_eof(socket, body) == -1)
				return -1;
			return 0;
		} else {
			if (octstr_parse_long(&body_len, h, 0, 10) 
			    == -1) {
				error(0, "HTTP2: Content-Length header "
				         "wrong: <%s>", octstr_get_cstr(h));
				goto error;
			}
			octstr_destroy(h);
			h = NULL;
			if (read_raw_body(socket, body, body_len) == -1)
				goto error;
			return 1;
		}
		
	}

	panic(0, "This location in code must never be reached.");

error:
	octstr_destroy(h);
	return -1;
}


/*
 * Read body that has been Transfer-Encoded as "chunked". Return -1 for
 * error, 0 for OK. If there are any trailing headers (see RFC 2616, 3.6.1)
 * they are appended to `headers'.
 */
static int read_chunked_body(int socket, Octstr **body, List *headers) {
	Octstr *line, *chunk;
	long len;
	List *trailer;

	*body = octstr_create_empty();
	line = NULL;

	for (;;) {
		if (socket_read_line(socket, &line) <= 0)
			goto error;
		if (octstr_parse_long(&len, line, 0, 16) == -1)
			goto error;
		octstr_destroy(line);
		line = NULL;
		if (len == 0)
			break;
		if (socket_read_bytes(socket, &chunk, len) <= 0)
			goto error;
		octstr_append(*body, chunk);
		octstr_destroy(chunk);
		if (socket_read_line(socket, &line) <= 0)
			goto error;
		if (octstr_len(line) != 0)
			goto error;
		octstr_destroy(line);
	}

	if (read_headers(socket, &trailer) == -1)
		goto error;
	while ((line = list_extract_first(trailer)) != NULL)
		list_append(headers, line);
	list_destroy(trailer);
	
	return 0;

error:
	octstr_destroy(line);
	octstr_destroy(*body);
	error(0, "HTTP2: Error reading chunked body.");
	return -1;
}


/*
 * Read a body whose length is know beforehand and which is not
 * encoded in any way. Return -1 for error, 0 for OK.
 */
static int read_raw_body(int socket, Octstr **body, long bytes) {
	if (socket_read_bytes(socket, body, bytes) <= 0) {
		error(0, "HTTP2: Error reading response body.");
		return -1;
	}
	return 0;
}
