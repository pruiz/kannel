/*
 * http.c - HTTP protocol implementation
 *
 * Lars Wirzenius
 */

#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "gwlib.h"
#include "http.h"


/*++++
 * The definition for the HTTPSocket data type.
 ++++*/
struct HTTPSocket {
	int in_use;
	time_t last_used;
	int socket;
	Octstr *host;
	int port;
	Octstr *buffer;
};


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


/*
 * Declarations for the socket pool.
 */
static void pool_init(void);
static void pool_shutdown(void);
static HTTPSocket *pool_allocate(Octstr *host, int port);
static void pool_free(HTTPSocket *p);
static void pool_free_and_close(HTTPSocket *p);
static int pool_socket_is_alive(HTTPSocket *p);
static int pool_socket_reopen(HTTPSocket *p);
static void pool_kill_old_ones(void);
static int pool_socket_old_and_unused(void *a, void *b);


/*
 * Operations on HTTPSockets.
 */
static HTTPSocket *socket_create_client(Octstr *host, int port);
static HTTPSocket *socket_create_server(int port);
static void socket_destroy(HTTPSocket *p);
static HTTPSocket *socket_accept(HTTPSocket *p);
static int socket_read_line(HTTPSocket *p, Octstr **line);
static int socket_read_bytes(HTTPSocket *p, Octstr **os, long bytes);
static int socket_read_to_eof(HTTPSocket *p, Octstr **os);
static int socket_write(HTTPSocket *p, Octstr *os);


/*
 * Other operations.
 */
static int parse_url(Octstr *url, Octstr **host, long *port, Octstr **path);

#ifdef POST_SUPPORT

/*
 * 
 * These are the build request operations as defined by Lars.
 *

static Octstr *build_request(Octstr *path_or_url, Octstr *host, long port, 
	List *headers, List *form_fields, char* method_name);
static HTTPSocket *send_request(Octstr *url, List *request_headers, List *form_fields,
	char* method_name);
 *
 *
 * The build request specified below use an octet string as opposed to a 
 * list for passing the message body. A list may be a better alternative 
 * for future use, but for now all that is required is passing the octet 
 * string to the http server.
 *
 * Extra parameters:
 * Octstr *form_fields: This contains the message body which is passed in from the 
 *						wap client.
 *
 * char *method_name:	This is the name of the request, whether it is a "GET" request
 *						or a "POST" request.
 *
 */

static Octstr *build_request(Octstr *path_or_url, Octstr *host, long port, 
	List *headers, Octstr *request_body, char* method_name);

static HTTPSocket *send_request(Octstr *url, List *request_headers, Octstr *request_body,
	char* method_name);

#else	/* POST_SUPPORT */

static Octstr *build_request(Octstr *path_or_url, Octstr *host, long port, 
	List *headers);
static int parse_status(Octstr *statusline);
static HTTPSocket *send_request(Octstr *url, List *request_headers);

#endif	/* POST_SUPPORT */




static int read_status(HTTPSocket *p);
static int read_headers(HTTPSocket *p, List **headers);
static int read_body(HTTPSocket *p, List *headers, Octstr **body);
static int read_chunked_body(HTTPSocket *p, Octstr **body, List *headers);
static int read_raw_body(HTTPSocket *p, Octstr **body, long bytes);
static List *parse_cgivars(Octstr *url);
static int header_is_called(Octstr *header, char *name);
static int http_something_accepted(List *headers, char *header_name, char *what);



void http_init(void) {
	gw_assert(proxy_mutex == NULL);
	proxy_mutex = mutex_create();
	proxy_exceptions = list_create();
	pool_init();
}


void http_shutdown(void) {
	gwlib_assert_init();
	http_close_proxy();
	list_destroy(proxy_exceptions);
	mutex_destroy(proxy_mutex);
	proxy_mutex = NULL;
	pool_shutdown();
}


void http_use_proxy(Octstr *hostname, int port, List *exceptions) {
	int i;

	gwlib_assert_init();
	gw_assert(hostname != NULL);
	gw_assert(octstr_len(hostname) > 0);
	gw_assert(port > 0);
	gw_assert(exceptions != NULL);

	http_close_proxy();
	mutex_lock(proxy_mutex);
	proxy_hostname = octstr_duplicate(hostname);
	proxy_port = port;
	for (i = 0; i < list_len(exceptions); ++i)
		list_append(proxy_exceptions, 
			    octstr_duplicate(list_get(exceptions, i)));
	mutex_unlock(proxy_mutex);
}


void http_close_proxy(void) {
	Octstr *p;

	gwlib_assert_init();

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


int http_get(Octstr *url, List *request_headers, 
List **reply_headers, Octstr **reply_body)  {
	int status;
	HTTPSocket *p;

	gwlib_assert_init();
	gw_assert(url != NULL);
	gw_assert(request_headers != NULL);
	gw_assert(reply_headers != NULL);
	gw_assert(reply_body != NULL);

	*reply_headers = NULL;
	*reply_body = NULL;

#ifdef POST_SUPPORT

/*
	As it is a GET message, can pass NULL for the message body.
*/
	p = send_request(url, request_headers, NULL, "GET");

#else	/* POST_SUPPORT */

	p = send_request(url, request_headers);

#endif	/* POST_SUPPORT */

	if (p == NULL)
		goto error;
	
	status = read_status(p);
	if (status < 0) {
		pool_free_and_close(p);

#ifdef POST_SUPPORT

			p = send_request(url, request_headers, NULL, "GET");

#else	/* POST_SUPPORT */

	p = send_request(url, request_headers);

#endif	/* POST_SUPPORT */

		if (p == NULL)
			goto error;
		status = read_status(p);
		if (status < 0)
			goto error;
	}
	
	if (read_headers(p, reply_headers) == -1)
		goto error;

	switch (read_body(p, *reply_headers, reply_body)) {
	case -1:
		goto error;
	case 0:
		pool_free_and_close(p);
		break;
	default:
		pool_free(p);
	}

	return status;

error:
	if (p != NULL)
		pool_free(p);
	error(0, "Couldn't fetch <%s>", octstr_get_cstr(url));
	return -1;
}


int http_get_real(Octstr *url, List *request_headers, Octstr **final_url,
List **reply_headers, Octstr **reply_body) {
	int i, ret;
	Octstr *h;

	gwlib_assert_init();
	gw_assert(url != NULL);
	gw_assert(final_url != NULL);
	gw_assert(request_headers != NULL);
	gw_assert(reply_headers != NULL);
	gw_assert(reply_body != NULL);

	ret = -1;
#ifdef POST_SUPPORT
	http_header_add(request_headers, "Content-Length", "0");
#endif

	*final_url = octstr_duplicate(url);
	for (i = 0; i < HTTP_MAX_FOLLOW; ++i) {
		ret = http_get(*final_url, request_headers, reply_headers, 
				reply_body);
		if (ret != HTTP_MOVED_PERMANENTLY && ret != HTTP_FOUND &&
		    ret != HTTP_SEE_OTHER)
			break;
		h = http_header_find_first(*reply_headers, "Location");
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

#ifdef POST_SUPPORT


	/*
	 * 
	 * The POST functionality is implemented by sending the request headers first with
	 * an expect clause to wait for the http 100 response from the server before sending 
	 * the request body on.
	 *
	 */


int http_post(Octstr *url, List *request_headers, Octstr *request_body,
			  List **reply_headers, Octstr **reply_body) {
	int status;
	HTTPSocket *p;
	List *tmp_headers;
	
	gwlib_assert_init();
	gw_assert(url != NULL);
	gw_assert(request_headers != NULL);
	gw_assert(request_body != NULL);
	gw_assert(reply_headers != NULL);
	gw_assert(reply_body != NULL);

	*reply_headers = NULL;
	*reply_body = NULL;

	p = send_request(url, request_headers, NULL, "POST");
	
	if (p == NULL)
		goto error;
	
	status = read_status(p);
	debug("gwlib.http", 0, "Status of Send: %d", status);

	if (status < 0) {
		pool_free_and_close(p);

		p = send_request(url, request_headers, NULL, "POST");
		if (p == NULL)
			goto error;

		status = read_status(p);
		debug("gwlib.http", 0, "Status of Send: %d", status);
		if (status < 0)
			goto error;

	}

	if (status == 100) {

		/* 
		 * This is to remove header information in the network buffer. 
		 */
		if (read_headers(p, &tmp_headers) == -1) {
			goto error;
		}
		
		/* 
		 * send the request_body to the http server
		 */
		debug("gwlib.http", 0, "Dumping HTTP Request Body:");
		octstr_dump(request_body, 0);
		if (socket_write(p, request_body) == -1)
			goto error;

		status = read_status(p);
		if (status < 0)
			goto error;
	}

	if (read_headers(p, reply_headers) == -1)
		goto error;

	switch (read_body(p, *reply_headers, reply_body)) {
	case -1:
		goto error;
	case 0:
		pool_free_and_close(p);
		break;
	default:
		pool_free(p);
	}

	if( tmp_headers != NULL)
		list_destroy(tmp_headers);

	return status;

error:
	if (p != NULL)
		pool_free(p);

	if( tmp_headers != NULL)
		list_destroy(tmp_headers);

	error(0, "Couldn't fetch <%s>", octstr_get_cstr(url));
	return -1;
}


int http_post_real(Octstr *url, List *request_headers, Octstr *request_body,
				   Octstr **final_url, List **reply_headers, Octstr **reply_body) {
	int i, ret, len;
	Octstr *h;
	char buf[16];

	gwlib_assert_init();
	gw_assert(url != NULL);
	gw_assert(final_url != NULL);
	gw_assert(request_headers != NULL);
	gw_assert(request_body != NULL);
	gw_assert(reply_headers != NULL);
	gw_assert(reply_body != NULL);


	/* 
	 * it is necessary to add some headers to support the Post request.
	 * Having it here instead of the http_post function means it is 
	 * only called once.
	 * 
	 * The Content-Length is added to the Post request so that the
	 * receiver will be calculate the length of the request body.
	 *  
	 * The Expect header is added to check whether the server can handle this 
	 * request or not. 
	 */

	if (NULL != request_body) {
		len = octstr_len(request_body);	
	} else {
		len = 0;
	}
	sprintf(buf, "%ld", len);
	http_header_add(request_headers, "Content-Length", buf);
	http_header_add(request_headers, "Expect", "100-continue");

	ret = -1;
	
	*final_url = octstr_duplicate(url);
	for (i = 0; i < HTTP_MAX_FOLLOW; ++i) {
		ret = http_post(*final_url, request_headers, request_body, reply_headers, 
				reply_body);
		/*
		 * POST_SUPPORT
		 * Added the HTTP_CREATED and HTTP_TEMPORARY_REDIRECT return values 
		 * If the return value is any of these values then the url to return to
		 * the caller is in the Location field and must be extracted.
		 */
		if (ret != HTTP_MOVED_PERMANENTLY && 
			ret != HTTP_FOUND &&
		    ret != HTTP_SEE_OTHER
			)
			break;
		h = http_header_find_first(*reply_headers, "Location");
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

#endif	/* POST_SUPPORT */



HTTPSocket *http_server_open(int port) {
	gwlib_assert_init();
	gw_assert(port > 0);
	return socket_create_server(port);
}


void http_server_close(HTTPSocket *socket) {
	gwlib_assert_init();
	gw_assert(socket != NULL);
	socket_destroy(socket);
}


int http_socket_fd(HTTPSocket *socket) {
	gwlib_assert_init();
	gw_assert(socket != NULL);
	return socket->socket;
}


Octstr *http_socket_ip(HTTPSocket *socket) {
	gwlib_assert_init();
	gw_assert(socket != NULL);
        return socket->host;
}


HTTPSocket *http_server_accept_client(HTTPSocket *socket) {
	gwlib_assert_init();
	gw_assert(socket != NULL);
	return socket_accept(socket);
}


void http_server_close_client(HTTPSocket *socket) {
	gwlib_assert_init();
	gw_assert(socket != NULL);
	socket_destroy(socket);
}


int http_server_get_request(HTTPSocket *socket, Octstr **url, List **headers,
Octstr **body, List **cgivars) {
	Octstr *line;
	long space;

	gwlib_assert_init();
	gw_assert(socket != NULL);
	gw_assert(url != NULL);
	gw_assert(headers != NULL);
	gw_assert(body != NULL);
	gw_assert(cgivars != NULL);

	line = NULL;
	*url = NULL;
	*headers = NULL;
	*body = NULL;
	*cgivars = NULL;

	switch (socket_read_line(socket, &line)) {
	case -1:
		goto error;
	case 0:
		return 0;
	}
	if (octstr_str_ncompare(line, "GET ") != 0)
		goto error;
	octstr_delete(line, 0, 4);
	space = octstr_search_char(line, ' ');
	if (space <= 0)
		goto error;
	*url = octstr_copy(line, 0, space);
	octstr_delete(line, 0, space + 1);
	if (octstr_str_compare(line, "HTTP/1.0") != 0 &&
	    octstr_str_compare(line, "HTTP/1.1") != 0)
		goto error;

	*cgivars = parse_cgivars(*url);

	if (read_headers(socket, headers) == -1)
		goto error;

	octstr_destroy(line);
	return 1;

error:
	octstr_destroy(line);
	octstr_destroy(*url);
	if (*headers != NULL) {
		while ((line = list_extract_first(*headers)) != NULL)
			octstr_destroy(line);
		list_destroy(*headers);
	}
	if (*cgivars != NULL) {
		while ((line = list_extract_first(*cgivars)) != NULL)
			octstr_destroy(line);
		list_destroy(*cgivars);
	}
	return -1;
}


int http_server_send_reply(HTTPSocket *socket, int status, List *headers, 
Octstr *body) {
	Octstr *response;
	char buf[1024];
	int i, ret;
	long len;
	
	gwlib_assert_init();
	gw_assert(status >= 100);
	gw_assert(status < 1000);
	gw_assert(headers != NULL);
	gw_assert(body != NULL);

	sprintf(buf, "HTTP/1.1 %d Foo\r\n", status);
	response = octstr_create(buf);
	if (body == NULL)
		len = 0;
	else
		len = octstr_len(body);
	sprintf(buf, "Content-Length: %ld\r\n", len);
	octstr_append_cstr(response, buf);
	for (i = 0; headers != NULL && i < list_len(headers); ++i) {
		octstr_append(response, list_get(headers, i));
		octstr_append_cstr(response, "\r\n");
	}
	octstr_append_cstr(response, "\r\n");
	if (body != NULL)
		octstr_append(response, body);
	ret = socket_write(socket, response);
	octstr_destroy(response);
	return ret;
}


void http_destroy_cgiargs(List *args) {
        HTTPCGIVar *v;

	gwlib_assert_init();

	if (args == NULL)
		return;

	while ((v = list_extract_first(args)) != NULL) {
		octstr_destroy(v->name);
		octstr_destroy(v->value);
		gw_free(v);
	}
	list_destroy(args);
}


Octstr *http_cgi_variable(List *list, char *name) {
	int i;
	HTTPCGIVar *v;
	
	gwlib_assert_init();
	gw_assert(list != NULL);
	gw_assert(name != NULL);

	for (i = 0; i < list_len(list); ++i) {
		v = list_get(list, i);
		if (octstr_str_compare(v->name, name) == 0)
			return v->value;
	}
	return NULL;
}


List *http_create_empty_headers(void) {
	gwlib_assert_init();
	return list_create();
}


void http_destroy_headers(List *headers) {
	Octstr *h;

	gwlib_assert_init();

	if (headers == NULL)
		return;

	while ((h = list_extract_first(headers)) != NULL)
		octstr_destroy(h);

	list_destroy(headers);
}


void http_header_add(List *headers, char *name, char *contents) {
	Octstr *h;
	
	gwlib_assert_init();
	gw_assert(headers != NULL);
	gw_assert(name != NULL);
	gw_assert(contents != NULL);

	h = octstr_create(name);
	octstr_append_cstr(h, ": ");
	octstr_append_cstr(h, contents);
	list_append(headers, h);
}


void http_header_get(List *headers, long i, Octstr **name, Octstr **value) {
	Octstr *os;
	long colon;
	
	gwlib_assert_init();
	gw_assert(i >= 0);
	gw_assert(name != NULL);
	gw_assert(value != NULL);

	os = list_get(headers, i);
	if (os == NULL)
		colon = -1;
	else
		colon = octstr_search_char(os, ':');
	if (colon == -1) {
		error(0, "HTTP: Header does not contain a colon. BAD.");
		*name = octstr_create("X-Unknown");
		*value = octstr_duplicate(os);
	} else {
		*name = octstr_copy(os, 0, colon);
		*value = octstr_copy(os, colon+1, octstr_len(os));
		octstr_strip_blank(*value);
	}
}


List *http_header_duplicate(List *headers) {
	List *new;
	long i;
	
	gwlib_assert_init();

	if (headers == NULL)
		return NULL;

	new = http_create_empty_headers();
	for (i = 0; i < list_len(headers); ++i)
		list_append(new, octstr_duplicate(list_get(headers, i)));
	return new;
}


void http_header_pack(List *headers) {
	gwlib_assert_init();
	gw_assert(headers != NULL);
	/* XXX not implemented yet. */
}


void http_append_headers(List *to, List *from) {
	Octstr *header;
	long i;
	
	gwlib_assert_init();
	gw_assert(to != NULL);
	gw_assert(from != NULL);

	for (i = 0; i < list_len(from); ++i) {
		header = list_get(from, i);
		list_append(to, octstr_duplicate(header));
	}
}


Octstr *http_header_find_first(List *headers, char *name) {
	long i, name_len;
	Octstr *h;

	gwlib_assert_init();
	gw_assert(headers != NULL);
	gw_assert(name != NULL);

	name_len = strlen(name);

	for (i = 0; i < list_len(headers); ++i) {
		h = list_get(headers, i);
		if (header_is_called(h, name))
			return octstr_copy(h, name_len + 1, octstr_len(h));
	}
	return NULL;
}


List *http_header_find_all(List *headers, char *name) {
	List *list;
	long i;
	Octstr *h;
	
	gwlib_assert_init();
	gw_assert(headers != NULL);
	gw_assert(name != NULL);

	list = list_create();
	for (i = 0; i < list_len(headers); ++i) {
		h = list_get(headers, i);
		if (header_is_called(h, name))
			list_append(list, octstr_duplicate(h));
	}
	return list;
}


void http_header_get_content_type(List *headers, Octstr **type, 
Octstr **charset) {
	Octstr *h;
	long semicolon;
	
	gwlib_assert_init();
	gw_assert(headers != NULL);
	gw_assert(type != NULL);
	gw_assert(charset != NULL);

	h = http_header_find_first(headers, "Content-Type");
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
			*charset = octstr_copy(h, semicolon + 1, 
					octstr_len(h));
			octstr_strip_blank(*charset);

			octstr_delete(h, semicolon, octstr_len(h));
			octstr_strip_blank(h);
			*type = h;
		}
	}
}


void http_header_dump(List *headers) {
	long i;
	
	gwlib_assert_init();

	debug("gwlib.http", 0, "Dumping HTTP headers:");
	for (i = 0; headers != NULL && i < list_len(headers); ++i)
		octstr_dump(list_get(headers, i), 1);
	debug("gwlib.http", 0, "End of dump.");
}

static char *istrdup (char *orig) {
    int i, len = strlen (orig);
    char *result = gw_malloc (len + 1);

    for (i = 0; i < len; i++)
	result[i] = toupper (orig [i]);

    result[i] = 0;

    return result;
}

static int http_something_accepted(List *headers, char *header_name, char *what) {
	int found;
	long i;
	List *accepts;
	char *iwhat;
	
	gwlib_assert_init();
	gw_assert(headers != NULL);
	gw_assert(what != NULL);

	iwhat = istrdup(what);
	accepts = http_header_find_all(headers, header_name);
	
	found = 0;
	for (i = 0; !found && i < list_len(accepts); ++i) {
		char *header_value = istrdup(octstr_get_cstr(
						list_get(accepts, i)));
		if (strstr(header_value, iwhat) != NULL)
			found = 1;
		gw_free (header_value);
	}

	gw_free(iwhat);
	http_destroy_headers(accepts);
	return found;
}


int http_type_accepted(List *headers, char *type) {
    return http_something_accepted(headers, "Accept", type);
}

int http_charset_accepted(List *headers, char *charset) {
    return http_something_accepted(headers, "Accept-Charset", charset);
}

/***********************************************************************
 * Functions that should be moved to other modules but which are here
 * while development happens because of simplicity.
 */
 
 
/*++++
 * Like octstr_str_compare, but compare only strlen(cstr) first bytes of
 * the octet string.
 ++++*/
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


/***********************************************************************
 * Proxy support functions.
 */


int proxy_used_for_host(Octstr *host) {
	int i;
	
	mutex_lock(proxy_mutex);

	if (proxy_hostname == NULL) {
		mutex_unlock(proxy_mutex);
		return 0;
	}

	for (i = 0; i < list_len(proxy_exceptions); ++i) {
		if (octstr_compare(host, list_get(proxy_exceptions, i)) == 0) {
			mutex_unlock(proxy_mutex);
			return 0;
		}
	}

	mutex_unlock(proxy_mutex);
	return 1;
}


/***********************************************************************
 * Socket pool management.
 */


enum { POOL_MAX_IDLE = 300 };


static List *pool = NULL;


static void pool_init(void) {
	pool = list_create();
}


static void pool_shutdown(void) {
	HTTPSocket *p;
	
	while ((p = list_extract_first(pool)) != NULL)
		socket_destroy(p);
	list_destroy(pool);
}


static HTTPSocket *pool_allocate(Octstr *host, int port) {
	HTTPSocket *p;
	int i;
	long pool_len;
	
	list_lock(pool);
	
	p = NULL;
	pool_len = list_len(pool);
	for (i = 0; i < pool_len; ++i) {
		p = list_get(pool, i);
		if (!p->in_use && p->port == port &&
		    octstr_compare(p->host, host) == 0) {
			break;
		}
	}

	if (i == pool_len) {
		/* Temporarily unlock pool, because socket_create_client
		 * can block for a long time.  We can do this safely because
		 * we don't rely on the values i and p, and pool_len
		 * currently have. */
		list_unlock(pool);
		p = socket_create_client(host, port);
		if (p == NULL)
			return NULL;
		list_lock(pool);
		pool_kill_old_ones();
		list_append(pool, p);
	} else {
		gw_assert(p != NULL);
		if (!pool_socket_is_alive(p) && pool_socket_reopen(p) == -1) {
			list_unlock(pool);
			return NULL;
		}
	}

	p->in_use = 1;
	list_unlock(pool);

	return p;
}


static void pool_free(HTTPSocket *p) {
	gw_assert(p != NULL);
	gw_assert(p->in_use);
	time(&p->last_used);
	p->in_use = 0;
}


static void pool_free_and_close(HTTPSocket *p) {
	gw_assert(p->in_use);

	list_lock(pool);
	list_delete_equal(pool, p);
	list_unlock(pool);

	socket_destroy(p);
}


static int pool_socket_is_alive(HTTPSocket *p) {
	if (p->socket < 0)
		return 0;

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


static int pool_socket_reopen(HTTPSocket *p) {
	debug("gwlib.http", 0, "HTTP: Re-opening socket.");
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
	HTTPSocket *p;

	time(&now);
	list = list_extract_all(pool, &now, pool_socket_old_and_unused);
	if (list != NULL) {
		while ((p = list_extract_first(list)) != NULL)
			socket_destroy(p);
		list_destroy(list);
	}
}


static int pool_socket_old_and_unused(void *a, void *b) {
	HTTPSocket *p;
	time_t now;
	
	p = a;
	now = *(time_t *) b;
	return !p->in_use && p->last_used != (time_t) -1 &&
		difftime(now, p->last_used) > POOL_MAX_IDLE;
}


/***********************************************************************
 * Operations on HTTPSockets, whether from the socket pool or not.
 */
 
 
/*
 * Create a new client side HTTPSocket.
 */
static HTTPSocket *socket_create_client(Octstr *host, int port) {
	HTTPSocket *p;

	debug("gwlib.http", 0, "HTTP: Creating a new client socket <%s:%d>.",
		octstr_get_cstr(host), port);
	p = gw_malloc(sizeof(HTTPSocket));
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


/*
 * Create a new server side HTTPSocket.
 */
static HTTPSocket *socket_create_server(int port) {
	HTTPSocket *p;

	debug("gwlib.http", 0, "HTTP: Creating a new server socket <%d>.",
		port);
	p = gw_malloc(sizeof(HTTPSocket));
	p->socket = make_server_socket(port);

	if (p->socket == -1) {
		free(p);
		return NULL;
	}

	p->in_use = 0;
	p->last_used = (time_t) -1;
	p->host = octstr_create("server socket");
	p->port = port;
	p->buffer = octstr_create_empty();

	return p;
}


/*
 * Destroy a HTTPSocket.
 */
static void socket_destroy(HTTPSocket *p) {
	gw_assert(p != NULL);

	debug("gwlib.http", 0, "HTTP: Closing socket <%s:%d>",
		octstr_get_cstr(p->host), p->port);
	(void) close(p->socket);
	octstr_destroy(p->host);
	octstr_destroy(p->buffer);
	gw_free(p);
}


/*
 * Accept a new client from a server socket.
 */
static HTTPSocket *socket_accept(HTTPSocket *server) {
	int s, addrlen;
	struct sockaddr_in addr;
	HTTPSocket *client;

	addrlen = sizeof(addr);
	s = accept(server->socket, (struct sockaddr *)&addr, &addrlen);
	if (s == -1) {
		error(errno, "HTTP: Error accepting a client.");
		return NULL;
	}
	client = gw_malloc(sizeof(HTTPSocket));
	client->in_use = 1;
	client->last_used = (time_t) -1;
	client->socket = s;
	client->host = host_ip(addr);
	client->port = 0;
	client->buffer = octstr_create_empty();

	debug("gwlib.http", 0, "HTTP: Accepted client from <%s>",
	      octstr_get_cstr(client->host));

	return client;
}

/*
 * Read a line from a socket and/or its associated buffer. Remove the
 * line from the buffer. Fill the buffer with new data from the socket,
 * if the buffer did not already have enough.
 *
 * Return -1 for error, 0 for EOF, >0 for line. The will will have its
 * line endings (\r\n or \n) removed.
 */
static int socket_read_line(HTTPSocket *p, Octstr **line) {
	int newline;
	
	while ((newline = octstr_search_char(p->buffer, '\n')) == -1) {
		switch (octstr_append_from_socket(p->buffer, p->socket)) {
		case -1:
			return -1;
		case 0:
			return 0;
		}
	}

	if (newline > 0 && octstr_get_char(p->buffer, newline-1) == '\r')
		*line = octstr_copy(p->buffer, 0, newline - 1);
	else
		*line = octstr_copy(p->buffer, 0, newline);
	octstr_delete(p->buffer, 0, newline + 1);

	return 1;
}


/*
 * Read `bytes' bytes from the socket `socket'. Use the pool buffer and
 * and fill it with new data from the socket as necessary. Return -1 for
 * error, 0 for EOF, or >0 for OK (exactly `bytes' bytes in `os'
 * returned). If OK, caller must free `*os'.
 */
static int socket_read_bytes(HTTPSocket *p, Octstr **os, long bytes) {
	while (octstr_len(p->buffer) < bytes) {
		switch (octstr_append_from_socket(p->buffer, p->socket)) {
		case -1:
			return -1;
		case 0:
			return 0;
		}
	}
	*os = octstr_copy(p->buffer, 0, bytes);
	octstr_delete(p->buffer, 0, bytes);
	return 1;
}



/*
 * Read all remaining data from socket. Return -1 for error, 0 for OK.
 */
static int socket_read_to_eof(HTTPSocket *p, Octstr **os) {
	for (;;) {
		switch (octstr_append_from_socket(p->buffer, p->socket)) {
		case -1:
			return -1;
		case 0:
			*os = octstr_duplicate(p->buffer);
			octstr_delete(p->buffer, 0, octstr_len(p->buffer));
			return 0;
		}
	}
}


/*
 * Write an octet string to a socket.
 */
static int socket_write(HTTPSocket *p, Octstr *os) {
	return octstr_write_to_socket(p->socket, os);
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

	/* XXX: we change http to lowercase, but is that non-wanted
	 *      operation? I mean, we could just compare it incasensitively,
	 *      but as there are no operators to that, it would need some
	 *      extra work (copying octstrs) so I put it this way, althought it
	 *      means that URL is modified...
	 */
	octstr_convert_range(url, 0, 4, tolower);
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

#ifdef POST_SUPPORT

static Octstr *build_request(Octstr *path_or_url, Octstr *host, long port,
List *headers, Octstr *request_body, char *method_name) {

#else	/* POST_SUPPORT */

static Octstr *build_request(Octstr *path_or_url, Octstr *host, long port,
List *headers) {

#endif	/* POST_SUPPORT */

/* XXX headers missing */
	Octstr *request;
	char buf[1024];
	int i;

	request = octstr_create_empty();

#ifdef POST_SUPPORT

	octstr_append_cstr(request, method_name);
	octstr_append_cstr(request, " ");

#else	/* POST_SUPPORT */

	octstr_append_cstr(request, "GET ");

#endif	/* POST_SUPPORT */
	
	octstr_append_cstr(request, octstr_get_cstr(path_or_url));
	octstr_append_cstr(request, " HTTP/1.1\r\nHost: ");
	octstr_append_cstr(request, octstr_get_cstr(host));
	if (port != HTTP_PORT) {
		sprintf(buf, ":%ld", port);
		octstr_append_cstr(request, buf);
	}

#ifdef POST_SUPPORT

	octstr_append_cstr(request, "\r\n");

#else	/* POST_SUPPORT */

	octstr_append_cstr(request, "\r\nContent-Length: 0\r\n");

#endif /* POST_SUPPORT */

	for (i = 0; headers != NULL && i < list_len(headers); ++i) {
		octstr_append(request, list_get(headers, i));
		octstr_append_cstr(request, "\r\n");
	}
	octstr_append_cstr(request, "\r\n");

#ifdef POST_SUPPORT

	if (NULL!=request_body) {
		octstr_append(request, request_body);
	}

#endif	/* POST_SUPPORT */


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
		error(0, "HTTP: Server responds with unknown HTTP version.");
		debug("gwlib.http", 0, "Status line: <%s>",
			octstr_get_cstr(statusline));
		return -1;
	}

	if (octstr_parse_long(&status, statusline, 
	                      strlen(versions[i]), 10) == -1) {
		error(0, "HTTP: Malformed status line from HTTP server: <%s>",
			octstr_get_cstr(statusline));
		return -1;
	}

	return status;
}



/*
 * Build and send the HTTP request. Return socket from which the
 * response can be read or -1 for error.
 */

#ifdef POST_SUPPORT

static HTTPSocket *send_request(Octstr *url, List *request_headers, 
						Octstr *request_body, char *method_name) {

#else	/* POST_SUPPORT */

static HTTPSocket *send_request(Octstr *url, List *request_headers) {

#endif	/* POST_SUPPORT */

	Octstr *host, *path, *request;
	long port;
	HTTPSocket *p;

	host = NULL;
	path = NULL;
	request = NULL;
	p = NULL;

	if (parse_url(url, &host, &port, &path) == -1)
		goto error;

	if (proxy_used_for_host(host)) {

#ifdef POST_SUPPORT
		request = build_request(url, host, port, request_headers, request_body, method_name);
#else	/* POST_SUPPORT */
		request = build_request(url, host, port, request_headers);
#endif	/* POST_SUPPORT */

		p = pool_allocate(proxy_hostname, proxy_port);
	} else {
#ifdef POST_SUPPORT
		request = build_request(path, host, port, request_headers, request_body, method_name);
#else	/* POST_SUPPORT */
		request = build_request(path, host, port, request_headers);
#endif	/* POST_SUPPORT */
		p = pool_allocate(host, port);
	}
	if (p == NULL)
		goto error;
	
	debug("wsp.http", 0, "HTTP: Sending request:");
	octstr_dump(request, 0);
	if (socket_write(p, request) == -1)
		goto error;

	octstr_destroy(host);
	octstr_destroy(path);
	octstr_destroy(request);

	return p;

error:
	octstr_destroy(host);
	octstr_destroy(path);
	octstr_destroy(request);
	if (p != NULL)
		pool_free(p);
	error(0, "Couldn't send request to <%s>", octstr_get_cstr(url));
	return NULL;
}


/*
 * Read and parse the status response line from an HTTP server.
 * Return the parsed status code. Return -1 for error.
 */
static int read_status(HTTPSocket *p) {
	Octstr *line;
	long status;

	if (socket_read_line(p, &line) <= 0) {
		warning(0, "HTTP: Couldn't read status line from server.");
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
static int read_headers(HTTPSocket *p, List **headers) {
	Octstr *line, *prev;

	*headers = list_create();
	prev = NULL;
	for (;;) {
		if (socket_read_line(p, &line) <= 0) {
			error(0, "HTTP: Incomplete response from server.");
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
static int read_body(HTTPSocket *p, List *headers, Octstr **body)
{
	Octstr *h;
	long body_len;

	h = http_header_find_first(headers, "Transfer-Encoding");
	if (h != NULL) {
		octstr_strip_blank(h);
		if (octstr_str_compare(h, "chunked") != 0) {
			error(0, "HTTP: Unknown Transfer-Encoding <%s>",
				octstr_get_cstr(h));
			goto error;
		}
		octstr_destroy(h);
		h = NULL;
		if (read_chunked_body(p, body, headers) == -1)
			goto error;
		return 1;
	} else {
		h = http_header_find_first(headers, "Content-Length");
		if (h == NULL) {
			if (socket_read_to_eof(p, body) == -1)
				return -1;
			return 0;
		} else {
			if (octstr_parse_long(&body_len, h, 0, 10) 
			    == -1) {
				error(0, "HTTP: Content-Length header "
				         "wrong: <%s>", octstr_get_cstr(h));
				goto error;
			}
			octstr_destroy(h);
			h = NULL;
			if (read_raw_body(p, body, body_len) == -1)
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
static int read_chunked_body(HTTPSocket *p, Octstr **body, List *headers) {
	Octstr *line, *chunk;
	long len;
	List *trailer;

	*body = octstr_create_empty();
	line = NULL;

	for (;;) {
		if (socket_read_line(p, &line) <= 0)
			goto error;
		if (octstr_parse_long(&len, line, 0, 16) == -1)
			goto error;
		octstr_destroy(line);
		line = NULL;
		if (len == 0)
			break;
		if (socket_read_bytes(p, &chunk, len) <= 0)
			goto error;
		octstr_append(*body, chunk);
		octstr_destroy(chunk);
		if (socket_read_line(p, &line) <= 0)
			goto error;
		if (octstr_len(line) != 0)
			goto error;
		octstr_destroy(line);
	}

	if (read_headers(p, &trailer) == -1)
		goto error;
	while ((line = list_extract_first(trailer)) != NULL)
		list_append(headers, line);
	list_destroy(trailer);
	
	return 0;

error:
	octstr_destroy(line);
	octstr_destroy(*body);
	error(0, "HTTP: Error reading chunked body.");
	return -1;
}


/*
 * Read a body whose length is know beforehand and which is not
 * encoded in any way. Return -1 for error, 0 for OK.
 */
static int read_raw_body(HTTPSocket *p, Octstr **body, long bytes) {
	if (socket_read_bytes(p, body, bytes) <= 0) {
		error(0, "HTTP: Error reading response body.");
		return -1;
	}
	return 0;
}



/*
 * Parse CGI variables from the path given in a GET. Return a list
 * of HTTPCGIvar pointers. Modify the url so that the variables are
 * removed.
 */
static List *parse_cgivars(Octstr *url) {
	HTTPCGIVar *v;
	List *list;
	int query, et, equals;
	Octstr *arg, *args;

	query = octstr_search_char(url, '?');
	if (query == -1)
		return list_create();
	
	args = octstr_copy(url, query + 1, octstr_len(url));
	octstr_truncate(url, query);
	
	list = list_create();

	while (octstr_len(args) > 0) {
		et = octstr_search_char(args, '&');
		if (et == -1)
			et = octstr_len(args);
		arg = octstr_copy(args, 0, et);
		octstr_delete(args, 0, et + 1);

		equals = octstr_search_char(arg, '=');
		if (equals == -1)
			equals = octstr_len(arg);

		v = gw_malloc(sizeof(HTTPCGIVar));
		v->name = octstr_copy(arg, 0, equals);
		v->value = octstr_copy(arg, equals + 1, octstr_len(arg));
		octstr_url_decode(v->name);
		octstr_url_decode(v->value);

		octstr_destroy(arg);
		
		list_append(list, v);
	}
	
	return list;
}


static int header_is_called(Octstr *header, char *name) {
	long colon;
	
	colon = octstr_search_char(header, ':');
	if (colon == -1)
		return 0;
	if ((long) strlen(name) != colon)
		return 0;
	return strncasecmp(octstr_get_cstr(header), name, colon) == 0;
}
