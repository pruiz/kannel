/*
 * http.c - HTTP protocol implementation
 *
 * Lars Wirzenius
 */
 
/* XXX 100 status codes. */
/* XXX make it possible to abort a transaction. */
/* XXX re-implement socket pools, with idle connection killing to 
    	save sockets */
/* XXX kill transactions if they don't complete in time */
/* XXX give maximum input size */

#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "gwlib.h"


/*
 * Maximum number of HTTP redirections to follow.
 */
enum { HTTP_MAX_FOLLOW = 5 };


/*
 * The definition for the HTTPSocket data type. It is a server socket
 * if conn == NULL.
 */
struct HTTPSocket {
    int in_use;
    int use_version_1_0;
    time_t last_used;
    int server_socket;
    Octstr *host;
    int port;
    Connection *conn;
};


/*
 * Internal lists of completely unhandled requests and requests for which
 * a request has been sent but response has not yet been read.
 */
static List *pending_requests = NULL;


/*
 * Counter for request identifiers.
 */
static Counter *request_id_counter = NULL;


/*
 * Have background threads been started?
 */
static Mutex *background_threads_lock = NULL;
static volatile sig_atomic_t background_threads_are_running = 0;


/*
 * Data associated with an HTTP transaction.
 */
typedef struct {
    HTTPCaller *caller;
    long request_id;
    Octstr *url;
    List *request_headers;
    Octstr *request_body;   /* NULL for GET, non-NULL for POST */
    enum {
	request_not_sent,
	reading_status,
	reading_headers,
	reading_body,
	transaction_done
    } state;
    int status;
    List *response_headers;
    Octstr *response_body;
    Connection *conn;
    int retrying;
    int follow_remaining;
    enum {
	reading_chunk_len,
	reading_chunk,
	reading_chunk_crlf,
	reading_trailer
    } chunked_body_state;
    long chunked_body_chunk_len;
} HTTPTransaction;

static HTTPTransaction *transaction_create(HTTPCaller *caller, Octstr *url, 
    	    	    	    	    	   List *headers, Octstr *body,
					   int follow_remaining);
static void transaction_destroy(void *trans);


/*
 * Status of this module.
 */
static enum { limbo, running, terminating } run_status = limbo;


/*
 * XXX
 */
static void start_background_threads(void);
static void write_request_thread(void *);


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
 * Declarations for the socket pool.
 */
static void pool_init(void);
static void pool_shutdown(void);
#if 0
static HTTPSocket *pool_get(Octstr *host, int port);
static void pool_put(HTTPSocket *p);
#endif


/*
 * Operations on HTTPSockets.
 */
static HTTPSocket *socket_create_server(int port);
static void socket_close(HTTPSocket *p);
static void socket_destroy(HTTPSocket *p);
static HTTPSocket *socket_accept(HTTPSocket *p);
static int socket_read_line(HTTPSocket *p, Octstr **line);
static int socket_write(HTTPSocket *p, Octstr *os);


/*
 * Other operations.
 */
static int parse_url(Octstr *url, Octstr **host, long *port, Octstr **path);

static Octstr *build_request(Octstr *path_or_url, Octstr *host, long port,
                             List *headers, Octstr *request_body, 
			     char* method_name);

static Connection *send_request(Octstr *url, List *request_headers,
                                Octstr *request_body, char *method_name);



static int client_read_status(Connection *p);
static int client_read_headers(Connection *p, List *headers);
static int client_read_body(HTTPTransaction *trans);
static int client_read_chunked_body(HTTPTransaction *trans);

static int read_headers(HTTPSocket *p, List **headers);

static List *parse_cgivars(Octstr *url);
static int header_is_called(Octstr *header, char *name);
static int http_something_accepted(List *headers, char *header_name, 
    	    	    	    	   char *what);



void http_init(void)
{
    gw_assert(proxy_mutex == NULL);
    proxy_mutex = mutex_create();
    proxy_exceptions = list_create();
    pool_init();
    pending_requests = list_create();
    list_add_producer(pending_requests);
    request_id_counter = counter_create();
    background_threads_lock = mutex_create();
    
    run_status = running;
}


void http_shutdown(void)
{
    gwlib_assert_init();

    run_status = terminating;
    list_remove_producer(pending_requests);
    gwthread_join_every(write_request_thread);

    http_close_proxy();
    list_destroy(proxy_exceptions, NULL);
    mutex_destroy(proxy_mutex);
    proxy_mutex = NULL;
    pool_shutdown();
    counter_destroy(request_id_counter);
    list_destroy(pending_requests, transaction_destroy);
    mutex_destroy(background_threads_lock);
    /* XXX destroy caller ids */
}


void http_use_proxy(Octstr *hostname, int port, List *exceptions)
{
    int i;

    gwlib_assert_init();
    gw_assert(hostname != NULL);
    gw_assert(octstr_len(hostname) > 0);
    gw_assert(port > 0);

    http_close_proxy();
    mutex_lock(proxy_mutex);
    proxy_hostname = octstr_duplicate(hostname);
    proxy_port = port;
    for (i = 0; i < list_len(exceptions); ++i)
        list_append(proxy_exceptions,
                    octstr_duplicate(list_get(exceptions, i)));
    debug("gwlib.http", 0, "Using proxy <%s:%d>", 
    	  octstr_get_cstr(proxy_hostname), proxy_port);
    mutex_unlock(proxy_mutex);
}


void http_close_proxy(void)
{
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


HTTPCaller *http_caller_create(void)
{
    HTTPCaller *caller;
    
    caller = list_create();
    list_add_producer(caller);
    return caller;
}


void http_caller_destroy(HTTPCaller *caller)
{
    list_destroy(caller, NULL); /* XXX destroy items */
}


long http_start_request(HTTPCaller *caller, Octstr *url, List *headers,
    	    	    	Octstr *body, int follow)
{
    HTTPTransaction *trans;
    long id;
    int follow_remaining;
    
    if (follow)
    	follow_remaining = HTTP_MAX_FOLLOW;
    else
    	follow_remaining = 0;

    trans = transaction_create(caller, url, headers, body, follow_remaining);
    id = trans->request_id;
    list_produce(pending_requests, trans);
    start_background_threads();
    return id;
}


long http_receive_result(HTTPCaller *caller, int *status, Octstr **final_url,
    	    	    	 List **headers, Octstr **body)
{
    HTTPTransaction *trans;
    long request_id;

    trans = list_consume(caller);
    if (trans == NULL)
    	return -1;

    request_id = trans->request_id;
    *status = trans->status;
    
    if (trans->status >= 0) {
	*final_url = trans->url;
	*headers = trans->response_headers;
	*body = trans->response_body;

	trans->url = NULL;
	trans->response_headers = NULL;
	trans->response_body = NULL;
    } else {
	*final_url = NULL;
	*headers = NULL;
	*body = NULL;
    }

    transaction_destroy(trans);
    return request_id;
}


int http_get(Octstr *url, List *request_headers,
             List **reply_headers, Octstr **reply_body)
{
    HTTPCaller *caller;
    int ret, status;
    Octstr *final_url;
    
    caller = http_caller_create();
    (void) http_start_request(caller, url, request_headers, NULL, 0);
    ret = http_receive_result(caller, &status, &final_url, 
    	    	    	      reply_headers, reply_body);
    octstr_destroy(final_url);
    http_caller_destroy(caller);
    if (ret == -1)
    	return -1;
    return status;
}


int http_get_real(Octstr *url, List *request_headers, Octstr **final_url,
                  List **reply_headers, Octstr **reply_body)
{
    HTTPCaller *caller;
    int ret, status;
    
    caller = http_caller_create();
    (void) http_start_request(caller, url, request_headers, NULL, 1);
    ret = http_receive_result(caller, &status, final_url, 
    	    	    	      reply_headers, reply_body);
    http_caller_destroy(caller);
    if (ret == -1)
    	return -1;
    return status;
}

int http_post(Octstr *url, List *request_headers, Octstr *request_body,
              List **reply_headers, Octstr **reply_body)
{
    HTTPCaller *caller;
    int ret, status;
    Octstr *final_url;
    
    caller = http_caller_create();
    (void) http_start_request(caller, url, request_headers, request_body, 0);
    ret = http_receive_result(caller, &status, &final_url, 
    	    	    	      reply_headers, reply_body);
    octstr_destroy(final_url);
    http_caller_destroy(caller);
    if (ret == -1)
    	return -1;
    return status;
}


HTTPSocket *http_server_open(int port)
{
    gwlib_assert_init();
    gw_assert(port > 0);
    return socket_create_server(port);
}


void http_server_close(HTTPSocket *socket)
{
    gwlib_assert_init();
    gw_assert(socket != NULL);
    socket_destroy(socket);
}


int http_socket_fd(HTTPSocket *socket)
{
    gwlib_assert_init();
    gw_assert(socket != NULL);
    return socket->server_socket;
}


Octstr *http_socket_ip(HTTPSocket *socket)
{
    gwlib_assert_init();
    gw_assert(socket != NULL);
    return socket->host;
}


HTTPSocket *http_server_accept_client(HTTPSocket *socket)
{
    gwlib_assert_init();
    gw_assert(socket != NULL);
    return socket_accept(socket);
}


void http_server_close_client(HTTPSocket *socket)
{
    gwlib_assert_init();
    gw_assert(socket != NULL);
    socket_destroy(socket);
}


int http_server_get_request(HTTPSocket *socket, Octstr **url, List **headers,
                            Octstr **body, List **cgivars)
{
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
    if (octstr_search(line, octstr_create_immutable("GET "), 0) != 0)
        goto error;
    octstr_delete(line, 0, 4);
    space = octstr_search_char(line, ' ', 0);
    if (space <= 0)
        goto error;
    *url = octstr_copy(line, 0, space);
    octstr_delete(line, 0, space + 1);

    if (octstr_str_compare(line, "HTTP/1.0") == 0)
    	socket->use_version_1_0 = 1;
    else if (octstr_str_compare(line, "HTTP/1.1") == 0)
    	socket->use_version_1_0 = 0;
    else
    	goto error;

    *cgivars = parse_cgivars(*url);

    if (read_headers(socket, headers) == -1)
        goto error;

    octstr_destroy(line);
    return 1;

error:
    octstr_destroy(line);
    octstr_destroy(*url);
    list_destroy(*headers, octstr_destroy_item);
    list_destroy(*cgivars, octstr_destroy_item);
    return -1;
}


int http_server_send_reply(HTTPSocket *socket, int status, List *headers,
                           Octstr *body)
{
    Octstr *response;
    int i, ret;
    long len;

    gwlib_assert_init();
    gw_assert(status >= 100);
    gw_assert(status < 1000);
    gw_assert(headers != NULL);
    gw_assert(body != NULL);

    if (socket->use_version_1_0)
	response = octstr_format("HTTP/1.0 %d Foo\r\n", status);
    else
        response = octstr_format("HTTP/1.1 %d Foo\r\n", status);
    if (body == NULL)
        len = 0;
    else
        len = octstr_len(body);
    octstr_format_append(response, "Content-Length: %ld\r\n", len);
    for (i = 0; headers != NULL && i < list_len(headers); ++i) {
        octstr_format_append(response, "%S\r\n",
                             list_get(headers, i));
    }
    octstr_format_append(response, "\r\n");
    if (body != NULL)
        octstr_append(response, body);
    ret = socket_write(socket, response);
    octstr_destroy(response);
    
    if (socket->use_version_1_0)
    	socket_close(socket);
    
    return ret;
}


void http_destroy_cgiargs(List *args)
{
    HTTPCGIVar *v;

    gwlib_assert_init();

    if (args == NULL)
        return ;

    while ((v = list_extract_first(args)) != NULL) {
        octstr_destroy(v->name);
        octstr_destroy(v->value);
        gw_free(v);
    }
    list_destroy(args, NULL);
}


Octstr *http_cgi_variable(List *list, char *name)
{
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


List *http_create_empty_headers(void)
{
    gwlib_assert_init();
    return list_create();
}


void http_destroy_headers(List *headers)
{
    gwlib_assert_init();
    list_destroy(headers, octstr_destroy_item);
}


void http_header_add(List *headers, char *name, char *contents)
{
    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(name != NULL);
    gw_assert(contents != NULL);

    list_append(headers, octstr_format("%s: %s", name, contents));
}


void http_header_get(List *headers, long i, Octstr **name, Octstr **value)
{
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
        colon = octstr_search_char(os, ':', 0);
    if (colon == -1) {
        error(0, "HTTP: Header does not contain a colon. BAD.");
        *name = octstr_create("X-Unknown");
        *value = octstr_duplicate(os);
    } else {
        *name = octstr_copy(os, 0, colon);
        *value = octstr_copy(os, colon + 1, octstr_len(os));
        octstr_strip_blanks(*value);
    }
}


List *http_header_duplicate(List *headers)
{
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


void http_header_pack(List *headers)
{
    gwlib_assert_init();
    gw_assert(headers != NULL);
    /* XXX not implemented yet. */
}


void http_append_headers(List *to, List *from)
{
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


void http_header_combine(List *old_headers, List *new_headers)
{
    long i;
    Octstr *name;
    Octstr *value;

    /*
     * Avoid doing this scan if old_headers is empty anyway.
     */
    if (list_len(old_headers) > 0) {
        for (i = 0; i < list_len(new_headers); i++) {
  	    http_header_get(new_headers, i, &name, &value);
	    http_header_remove_all(old_headers, octstr_get_cstr(name));
            octstr_destroy(name);
            octstr_destroy(value);
        }
    }

    http_append_headers(old_headers, new_headers);
}


Octstr *http_header_find_first(List *headers, char *name)
{
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


List *http_header_find_all(List *headers, char *name)
{
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


long http_header_remove_all(List *headers, char *name)
{
    long i;
    Octstr *h;
    long count;

    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(name != NULL);

    i = 0;
    count = 0;
    while (i < list_len(headers)) {
	h = list_get(headers, i);
	if (header_is_called(h, name)) {
	    list_delete(headers, i, 1);
	    octstr_destroy(h);
	    count++;
	} else
	    i++;
    }

    return count;
}


void http_remove_hop_headers(List *headers)
{
    Octstr *h;
    List *connection_headers;

    gwlib_assert_init();
    gw_assert(headers != NULL);

    /*
     * The hop-by-hop headers are a standard list, plus those named
     * in the Connection header(s).
     */

    connection_headers = http_header_find_all(headers, "Connection");
    while ((h = list_consume(connection_headers))) {
	List *hop_headers;
	Octstr *e;

	octstr_delete(h, 0, strlen("Connection:"));
	hop_headers = http_header_split_value(h);
	octstr_destroy(h);

	while ((e = list_consume(hop_headers))) {
	    http_header_remove_all(headers, octstr_get_cstr(e));
	    octstr_destroy(e);
	}

	list_destroy(hop_headers, NULL);
    }
    list_destroy(connection_headers, NULL);
   
    http_header_remove_all(headers, "Connection");
    http_header_remove_all(headers, "Keep-Alive");
    http_header_remove_all(headers, "Proxy-Authenticate");
    http_header_remove_all(headers, "Proxy-Authorization");
    http_header_remove_all(headers, "TE");
    http_header_remove_all(headers, "Trailers");
    http_header_remove_all(headers, "Transfer-Encoding");
    http_header_remove_all(headers, "Upgrade");
}


void http_header_mark_transformation(List *headers,
Octstr *new_body, Octstr *new_type)
{
    Octstr *new_length = NULL;

    /* Remove all headers that no longer apply to the new body. */
    http_header_remove_all(headers, "Content-Length");
    http_header_remove_all(headers, "Content-MD5");
    http_header_remove_all(headers, "Content-Type");

    /* Add headers that we need to describe the new body. */
    new_length = octstr_format("%ld", octstr_len(new_body));
    http_header_add(headers, "Content-Length", octstr_get_cstr(new_length));
    http_header_add(headers, "Content-Type", octstr_get_cstr(new_type));

    /* Perhaps we should add Warning: 214 "Transformation applied" too? */

    octstr_destroy(new_length);
}


void http_header_get_content_type(List *headers, Octstr **type,
                                  Octstr **charset)
{
    Octstr *h;
    long semicolon, equals, len;

    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(type != NULL);
    gw_assert(charset != NULL);

    h = http_header_find_first(headers, "Content-Type");
    if (h == NULL) {
        *type = octstr_create("application/octet-stream");
        *charset = octstr_create("");
    } else {
        octstr_strip_blanks(h);
        semicolon = octstr_search_char(h, ';', 0);
        if (semicolon == -1) {
            *type = h;
            *charset = octstr_create("");
        } else {
            *charset = octstr_duplicate(h);
            octstr_delete(*charset, 0, semicolon + 1);
            octstr_strip_blanks(*charset);
            equals = octstr_search_char(*charset, '=', 0);
            if (equals == -1)
                octstr_truncate(*charset, 0);
            else {
                octstr_delete(*charset, 0, equals + 1);
                if (octstr_get_char(*charset, 0) == '"')
                    octstr_delete(*charset, 0, 1);
                len = octstr_len(*charset);
                if (octstr_get_char(*charset, len - 1) == '"')
                    octstr_truncate(*charset, len - 1);
            }

            octstr_truncate(h, semicolon);
            octstr_strip_blanks(h);
            *type = h;
        }
    }
}


static void http_header_add_element(List *list, Octstr *value,
				    long start, long end)
{
    Octstr *element;

    element = octstr_copy(value, start, end - start);
    octstr_strip_blanks(element);
    if (octstr_len(element) == 0)
	octstr_destroy(element);
    else
    	list_append(list, element);
}


long http_header_quoted_string_len(Octstr *header, long start)
{
    long len;
    long pos;
    int c;

    if (octstr_get_char(header, start) != '"')
	return -1;

    len = octstr_len(header);
    for (pos = start + 1; pos < len; pos++) {
	c = octstr_get_char(header, pos);
	if (c == '\\')    /* quoted-pair */
	    pos++;
	else if (c == '"')
	    return pos - start + 1;
    }

    warning(0, "Header contains unterminated quoted-string:");
    warning(0, "%s", octstr_get_cstr(header));
    return len - start;
}


List *http_header_split_value(Octstr *value)
{
    long start;  /* start of current element */
    long pos;
    long len;
    List *result;
    int c;

    /*
     * According to RFC2616 section 4.2, a field-value is either *TEXT
     * (the caller is responsible for not feeding us one of those) or
     * combinations of token, separators, and quoted-string.  We're
     * looking for commas which are separators, and have to skip
     * commas in quoted-strings.
     */
 
    result = list_create();
    len = octstr_len(value);
    start = 0;
    for (pos = 0; pos < len; pos++) {
	c = octstr_get_char(value, pos);
	if (c == ',') {
	    http_header_add_element(result, value, start, pos);
	    start = pos + 1;
	} else if (c == '"') {
            pos += http_header_quoted_string_len(value, pos);
	    pos--; /* compensate for the loop's pos++ */
        }
    }
    http_header_add_element(result, value, start, len);
    return result;
}


List *http_header_split_auth_value(Octstr *value)
{
    List *result;
    Octstr *auth_scheme;
    Octstr *element;
    long i;

    /*
     * According to RFC2617, both "challenge" and "credentials"
     * consist of an auth-scheme followed by a list of auth-param.
     * Since we have to parse a list of challenges or credentials,
     * we have to look for auth-scheme to signal the start of
     * a new element.  (We can't just split on commas because
     * they are also used to separate the auth-params.)
     *
     * An auth-scheme is a single token, while an auth-param is
     * always a key=value pair.  So we can recognize an auth-scheme
     * as a token that is not followed by a '=' sign.
     *
     * Simple approach: First split at all commas, then recombine
     * the elements that belong to the same challenge or credential.
     * This is somewhat expensive but saves programmer thinking time.
     *
     * Richard Braakman
     */
 
    result = http_header_split_value(value);

    auth_scheme = list_get(result, 0);
    i = 1;
    while (i < list_len(result)) {
	int c;
	long pos;

	element = list_get(result, i);
	/*
	 * If the element starts with: token '='
	 * then it's just an auth_param; append it to the current
	 * auth_scheme.  If it starts with: token token '='
	 * then it's the start of a new auth scheme.
	 * 
	 * To make the scan easier, we consider anything other
	 * than whitespace or '=' to be part of a token.
	 */

	/* Skip first token */
	for (pos = 0; pos < octstr_len(element); pos++) {
	    c = octstr_get_char(element, pos);
	    if (isspace(c) || c == '=')
		break;
	}

	/* Skip whitespace, if any */
	while (isspace(octstr_get_char(element, pos)))
	    pos++;

	if (octstr_get_char(element, pos) == '=') {
		octstr_append_char(auth_scheme, ';');
		octstr_append(auth_scheme, element);
		list_delete(result, i, 1);
		octstr_destroy(element);
	} else {
		unsigned char semicolon = ';';
		octstr_insert_data(element, pos, &semicolon, 1);
		auth_scheme = element;
		i++;
	}
    }

    return result;
}


void http_header_dump(List *headers)
{
    long i;

    gwlib_assert_init();

    debug("gwlib.http", 0, "Dumping HTTP headers:");
    for (i = 0; headers != NULL && i < list_len(headers); ++i)
        octstr_dump(list_get(headers, i), 1);
    debug("gwlib.http", 0, "End of dump.");
}


static char *istrdup(char *orig)
{
    int i, len = strlen(orig);
    char *result = gw_malloc(len + 1);

    for (i = 0; i < len; i++)
        result[i] = toupper(orig[i]);

    result[i] = 0;

    return result;
}


static int http_something_accepted(List *headers, char *header_name,
                                   char *what)
{
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
        char *header_value = istrdup(octstr_get_cstr(list_get(accepts, i)));
        if (strstr(header_value, iwhat) != NULL)
            found = 1;
        gw_free(header_value);
    }

    gw_free(iwhat);
    http_destroy_headers(accepts);
    return found;
}


int http_type_accepted(List *headers, char *type)
{
    return http_something_accepted(headers, "Accept", type);
}


int http_charset_accepted(List *headers, char *charset)
{
    return http_something_accepted(headers, "Accept-Charset", charset);
}


/***********************************************************************
 * Proxy support functions.
 */


static int proxy_used_for_host(Octstr *host)
{
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


#if 1
static void pool_init(void) { }
static void pool_shutdown(void) { }
#if 0
static HTTPSocket *pool_get(Octstr *host, int port)
{
    return socket_create_client(host, port);
}
static void pool_put(HTTPSocket *p)
{
    socket_destroy(p);
}
#endif
#else


static Dict *pool = NULL;
static Mutex *pool_lock = NULL;


static void pool_destroy_item(void *list)
{
    HTTPSocket *p;
    
    while ((p = list_extract_first(list)) != NULL)
    	socket_destroy(p);
    list_destroy(list, NULL);
}


static void pool_init(void)
{
    pool = dict_create(1024, pool_destroy_item);
    pool_lock = mutex_create();
}


static void pool_shutdown(void)
{
    dict_destroy(pool);
    mutex_destroy(pool_lock);
}


static HTTPSocket *pool_get(Octstr *host, int port)
{
    Octstr *key;
    List *list;
    HTTPSocket *p;
    
    key = octstr_format("%S:%d", host, port);

    mutex_lock(pool_lock);
    list = dict_get(pool, key);
    if (list == NULL)
    	p = NULL;
    else
	p = list_extract_first(list);
    mutex_unlock(pool_lock);
	
    if (p == NULL)
	p = socket_create_client(host, port);

    octstr_destroy(key);
    return p;
}


static void pool_put(HTTPSocket *p)
{
    Octstr *key;
    List *list;

    key = octstr_format("%S:%d", p->host, p->port);

    mutex_lock(pool_lock);
    list = dict_get(pool, key);
    if (list == NULL) {
    	list = list_create();
	list_append(list, p);
	dict_put(pool, key, list);
    } else
    	list_append(list, p);
    mutex_unlock(pool_lock);
    
    octstr_destroy(key);
}
#endif


/***********************************************************************
 * Operations on HTTPSockets, whether from the socket pool or not.
 */




/*
 * Create a new server side HTTPSocket.
 */
static HTTPSocket *socket_create_server(int port)
{
    HTTPSocket *p;

    debug("gwlib.http", 0, "HTTP: Creating a new server socket <%d>.",
          port);
    p = gw_malloc(sizeof(HTTPSocket));
    p->server_socket = make_server_socket(port);

    if (p->server_socket == -1) {
        gw_free(p);
        return NULL;
    }

    p->conn = NULL;
    p->in_use = 0;
    p->use_version_1_0 = 0;
    p->last_used = (time_t) - 1;
    p->host = octstr_create("server socket");
    p->port = port;

    return p;
}


/*
 * Destroy an HTTPSocket.
 */
static void socket_destroy(HTTPSocket *p)
{
    gw_assert(p != NULL);

    debug("gwlib.http", 0, "HTTP: Closing socket <%s:%d>",
          octstr_get_cstr(p->host), p->port);
    if (p->server_socket != -1 && close(p->server_socket) == -1)
    	error(errno, "HTTP: Closing of socket failed.");
    if (p->conn != NULL)
    	conn_destroy(p->conn);
    octstr_destroy(p->host);
    gw_free(p);
}


/*
 * Close an HTTPSocket, but don't destroy it. Further I/O operations on
 * the socket will fail, but it still needs to be destroyed with
 * socket_destroy.
 */
static void socket_close(HTTPSocket *p)
{
    if (p->server_socket != -1 && close(p->server_socket) == -1)
    	error(errno, "HTTP: Closing of socket failed.");
    p->server_socket = -1;
    if (p->conn != NULL) {
    	conn_destroy(p->conn);
	p->conn = NULL;
    }
}


/*
 * Accept a new client from a server socket.
 */
static HTTPSocket *socket_accept(HTTPSocket *server)
{
    int s, addrlen;
    struct sockaddr_in addr;
    HTTPSocket *client;

    gw_assert(server->server_socket != -1);

    addrlen = sizeof(addr);
    s = accept(server->server_socket, (struct sockaddr *) & addr, &addrlen);
    if (s == -1) {
        error(errno, "HTTP: Error accepting a client.");
        return NULL;
    }
    client = gw_malloc(sizeof(HTTPSocket));
    client->in_use = 1;
    client->last_used = (time_t) - 1;
    client->server_socket = -1;
    client->conn = conn_wrap_fd(s);
    client->host = host_ip(addr);
    client->port = 0;

    debug("gwlib.http", 0, "HTTP: Accepted client from <%s>",
          octstr_get_cstr(client->host));

    return client;
}

/*
 * Read a line from a socket and/or its associated buffer. Remove the
 * line from the buffer. Fill the buffer with new data from the socket,
 * if the buffer did not already have enough.
 *
 * Return 0 for EOF, -1 for error, >0 for line. The line will will have its
 * line endings (\r\n or \n) removed.
 */
static int socket_read_line(HTTPSocket *p, Octstr **line)
{
    gw_assert(p->server_socket == -1);
    if (p->conn == NULL)
    	return 0;

    for (;;) {
	*line = conn_read_line(p->conn);
	if (*line != NULL)
	    return 1;
	if (conn_wait(p->conn, -1) == -1)
	    return -1;
	if (conn_eof(p->conn))
	    return 0;
    }
}


/*
 * Write an octet string to a socket.
 */
static int socket_write(HTTPSocket *p, Octstr *os)
{
    gw_assert(p->server_socket == -1);
    gw_assert(p->conn != NULL);
    if (conn_write(p->conn, os) == -1)
    	return -1;
    return 0;
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
static int parse_url(Octstr *url, Octstr **host, long *port, Octstr **path)
{
    Octstr *prefix;
    long prefix_len;
    int host_len, colon, slash;

    prefix = octstr_create_immutable("http://");
    prefix_len = octstr_len(prefix);

    if (octstr_case_search(url, prefix, 0) != 0) {
        error(0, "URL <%s> doesn't start with `%s'",
              octstr_get_cstr(url), octstr_get_cstr(prefix));
        return -1;
    }

    if (octstr_len(url) == prefix_len) {
        error(0, "URL <%s> is malformed.", octstr_get_cstr(url));
        return -1;
    }

    colon = octstr_search_char(url, ':', prefix_len);
    slash = octstr_search_char(url, '/', prefix_len);
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
        if (octstr_parse_long(port, url, colon + 1, 10) == -1) {
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
        if (octstr_parse_long(port, url, colon + 1, 10) == -1) {
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

static Octstr *build_request(Octstr *path_or_url, Octstr *host, long port,
                             List *headers, Octstr *request_body, 
			     char *method_name)
{

    /* XXX headers missing */
    Octstr *request;
    int i;

    request = octstr_format("%s %S HTTP/1.1\r\n",
                            method_name, path_or_url);

    octstr_format_append(request, "Host: %S", host);
    if (port != HTTP_PORT)
        octstr_format_append(request, ":%ld", port);
    octstr_append(request, octstr_create_immutable("\r\n"));

    for (i = 0; headers != NULL && i < list_len(headers); ++i) {
        octstr_append(request, list_get(headers, i));
        octstr_append(request, octstr_create_immutable("\r\n"));
    }
    octstr_append(request, octstr_create_immutable("\r\n"));

    if (request_body != NULL)
        octstr_append(request, request_body);

    return request;
}


/*
 * Parse the status line returned by an HTTP server and return the
 * status code. Return -1 if the status line was unparseable.
 */
static int parse_status(Octstr *statusline)
{
    static char *versions[] = {
        "HTTP/1.1 ",
        "HTTP/1.0 ",
    };
    static int num_versions = sizeof(versions) / sizeof(versions[0]);
    long status;
    int i;

    for (i = 0; i < num_versions; ++i) {
        if (octstr_search(statusline,
                          octstr_create_immutable(versions[i]),
                          0) == 0)
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

static Connection *send_request(Octstr *url, List *request_headers,
                                Octstr *request_body, char *method_name)
{
    Octstr *host, *path, *request;
    long port;
    Connection *conn;

    host = NULL;
    path = NULL;
    request = NULL;
    conn = NULL;

    if (parse_url(url, &host, &port, &path) == -1)
        goto error;

    if (proxy_used_for_host(host)) {
        request = build_request(url, host, port, request_headers,
                                request_body, method_name);
        conn = conn_open_tcp(proxy_hostname, proxy_port);
    } else {
        request = build_request(path, host, port, request_headers,
                                request_body, method_name);
        conn = conn_open_tcp(host, port);
    }
    if (conn == NULL)
        goto error;

    debug("wsp.http", 0, "HTTP: Sending request:");
    octstr_dump(request, 0);
    if (conn_write(conn, request) == -1)
        goto error;

    octstr_destroy(host);
    octstr_destroy(path);
    octstr_destroy(request);

    return conn;

error:
    conn_destroy(conn);
    octstr_destroy(host);
    octstr_destroy(path);
    octstr_destroy(request);
    error(0, "Couldn't send request to <%s>", octstr_get_cstr(url));
    return NULL;
}


/*
 * Read and parse the status response line from an HTTP server.
 * Return the parsed status code. Return -1 for error. Return 0 for
 * status code not yet available.
 */
static int client_read_status(Connection *conn)
{
    Octstr *line;
    long status;

    line = conn_read_line(conn);
    if (line == NULL) {
	if (conn_eof(conn))
	    return -1;
    	return 0;
    }

    status = parse_status(line);
    octstr_destroy(line);
    return status;
}


/*
 * Read some headers, i.e., until the first empty line (read and discard
 * the empty line as well). Return -1 for error, 0 for all headers read,
 * 1 for more headers to follow.
 */
static int client_read_headers(Connection *conn, List *headers)
{
    Octstr *line, *prev;

    if (list_len(headers) == 0)
        prev = NULL;
    else
    	prev = list_get(headers, list_len(headers) - 1);

    for (;;) {
	line = conn_read_line(conn);
	if (line == NULL) {
	    if (conn_eof(conn))
	    	return -1;
	    return 1;
	}
        if (octstr_len(line) == 0) {
            octstr_destroy(line);
            break;
        }
        if (isspace(octstr_get_char(line, 0)) && prev != NULL) {
            octstr_append(prev, line);
            octstr_destroy(line);
        } else {
            list_append(headers, line);
            prev = line;
        }
    }

    return 0;
}


/* XXX
 * Read the body of a response. Return -1 for error, 0 for OK (EOF on
 * socket reached) or 1 for OK (socket can be used for another transaction).
 */
static int client_read_body(HTTPTransaction *trans)
{
    Octstr *h, *os;
    long n, body_len;

    h = http_header_find_first(trans->response_headers, "Transfer-Encoding");
    if (h != NULL) {
        octstr_strip_blanks(h);
        if (octstr_str_compare(h, "chunked") != 0) {
            error(0, "HTTP: Unknown Transfer-Encoding <%s>",
                  octstr_get_cstr(h));
            goto error;
        }
        octstr_destroy(h);
        if (client_read_chunked_body(trans) == -1)
            goto error;
        return 1;
    } else {
        h = http_header_find_first(trans->response_headers, "Content-Length");
        if (h == NULL) {
	    n = conn_inbuf_len(trans->conn);
	    if (n == 0)
	    	return 1;
	    os = conn_read_fixed(trans->conn, n);
	    gw_assert(os != NULL);
	    octstr_append(trans->response_body, os);
	    if (conn_eof(trans->conn))
	    	return 0;
	    return 1;
        } else {
            if (octstr_parse_long(&body_len, h, 0, 10) == -1) {
                error(0, "HTTP: Content-Length header wrong: <%s>", 
		      octstr_get_cstr(h));
                goto error;
            }
            octstr_destroy(h);
	    os = conn_read_fixed(trans->conn, body_len);
	    if (os == NULL)
	    	return 1;
	    octstr_destroy(trans->response_body);
	    trans->response_body = os;
            return 0;
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
static int client_read_chunked_body(HTTPTransaction *trans)
{
    Octstr *os;
    long len;
    int ret;

    for (;;) {
	switch (trans->chunked_body_state) {
	case reading_chunk_len:
	    os = conn_read_line(trans->conn);
	    if (os == NULL) {
		if (conn_eof(trans->conn))
		    return -1;
		return 1;
	    }
	    if (octstr_parse_long(&len, os, 0, 16) == -1) {
		octstr_destroy(os);
		return -1;
	    }
	    octstr_destroy(os);
	    if (len == 0)
		trans->chunked_body_state = reading_trailer;
	    else {
		trans->chunked_body_state = reading_chunk;
		trans->chunked_body_chunk_len = len;
	    }
	    break;
	    
	case reading_chunk:
	    os = conn_read_fixed(trans->conn, trans->chunked_body_chunk_len);
	    if (os == NULL) {
		if (conn_eof(trans->conn))
		    return -1;
		return 1;
	    }
	    octstr_append(trans->response_body, os);
	    octstr_destroy(os);
	    trans->chunked_body_state = reading_chunk_crlf;
	    break;
	    
	case reading_chunk_crlf:
	    os = conn_read_line(trans->conn);
	    if (os == NULL) {
		if (conn_eof(trans->conn))
		    return -1;
		return 1;
	    }
	    trans->chunked_body_state = reading_chunk_len;
	    break;
	    
	case reading_trailer:
	    ret = client_read_headers(trans->conn, trans->response_headers);
	    if (ret == -1)
	    	return -1;
	    else if (ret == 0)
	    	return 0;
	    break;
	    
	default:
	    panic(0, "Internal error: "
		  "HTTPTransaction invaluded chunked body state.");
	}
    }
}


/*
 * Read the headers, i.e., until the first empty line (read and discard
 * the empty line as well). Return -1 for error, 0 for OK.
 */
static int read_headers(HTTPSocket *p, List **headers)
{
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
    list_destroy(*headers, octstr_destroy_item);
    *headers = NULL;
    return -1;
}


/*
 * Parse CGI variables from the path given in a GET. Return a list
 * of HTTPCGIvar pointers. Modify the url so that the variables are
 * removed.
 */
static List *parse_cgivars(Octstr *url)
{
    HTTPCGIVar *v;
    List *list;
    int query, et, equals;
    Octstr *arg, *args;

    query = octstr_search_char(url, '?', 0);
    if (query == -1)
        return list_create();

    args = octstr_copy(url, query + 1, octstr_len(url));
    octstr_truncate(url, query);

    list = list_create();

    while (octstr_len(args) > 0) {
        et = octstr_search_char(args, '&', 0);
        if (et == -1)
            et = octstr_len(args);
        arg = octstr_copy(args, 0, et);
        octstr_delete(args, 0, et + 1);

        equals = octstr_search_char(arg, '=', 0);
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
    octstr_destroy(args);

    return list;
}


static int header_is_called(Octstr *header, char *name)
{
    long colon;

    colon = octstr_search_char(header, ':', 0);
    if (colon == -1)
        return 0;
    if ((long) strlen(name) != colon)
        return 0;
    return strncasecmp(octstr_get_cstr(header), name, colon) == 0;
}


/***********************************************************************
 * Internal threads.
 */
 
 
static HTTPTransaction *transaction_create(HTTPCaller *caller, Octstr *url,
    	    	    	    	    	   List *headers, Octstr *body,
					   int follow_remaining)
{
    HTTPTransaction *trans;
    
    trans = gw_malloc(sizeof(*trans));
    trans->caller = caller;
    trans->request_id = counter_increase(request_id_counter);
    trans->url = octstr_duplicate(url);
    trans->request_headers = http_header_duplicate(headers);
    trans->request_body = octstr_duplicate(body);
    trans->state = request_not_sent;
    trans->status = -1;
    trans->response_headers = list_create();
    trans->response_body = octstr_create("");
    trans->conn = NULL;
    trans->retrying = 0;
    trans->follow_remaining = follow_remaining;
    trans->chunked_body_state = reading_chunk_len;
    trans->chunked_body_chunk_len = 0;
    return trans;
}


static void transaction_destroy(void *p)
{
    HTTPTransaction *trans;
    
    trans = p;
    octstr_destroy(trans->url);
    http_destroy_headers(trans->request_headers);
    octstr_destroy(trans->request_body);
    http_destroy_headers(trans->response_headers);
    octstr_destroy(trans->response_body);
    gw_free(trans);
}


static Octstr *get_redirection_location(HTTPTransaction *trans)
{
    if (trans->status < 0 || trans->follow_remaining <= 0)
    	return NULL;
    if (trans->status != HTTP_MOVED_PERMANENTLY &&
    	trans->status != HTTP_FOUND && trans->status != HTTP_SEE_OTHER)
	return NULL;
    return http_header_find_first(trans->response_headers, "Location");
}


static void handle_transaction(Connection *conn, void *data)
{
    HTTPTransaction *trans;
    int ret;
    Octstr *h;
    
    trans = data;

    if (run_status != running) {
	conn_unregister(conn);
	return;
    }

    while (trans->state != transaction_done) {
	switch (trans->state) {
	case reading_status:
	    trans->status = client_read_status(trans->conn);
	    if (trans->status < 0) {
		/*
		 * Couldn't read the status from the socket. This may mean that 
		 * the socket had been closed by the server after an idle 
		 * timeout, so we close the connection and try again, opening a 
		 * new socket, but only once.
		 */
		if (trans->retrying) {
		    goto error;
		} else {
		    conn_unregister(trans->conn);
		    conn_destroy(trans->conn);
		    trans->conn = NULL;
		    trans->retrying = 1;
		    trans->state = request_not_sent;
		    list_produce(pending_requests, trans);
		}
	    } else if (trans->status > 0) {
		/* Got the status, go read headers next. */
		trans->state = reading_headers;
	    } else
		return;
	    break;
	    
	case reading_headers:
	    ret = client_read_headers(trans->conn, 
				      trans->response_headers);
	    if (ret == -1)
		goto error;
	    else if (ret == 0)
		trans->state = reading_body;
	    else
		return;
	    break;
	    
	case reading_body:
	    ret = client_read_body(trans);
	    if (ret == -1)
		goto error;
	    else if (ret == 0) {
		conn_unregister(trans->conn);
		conn_destroy(trans->conn);
		trans->conn = NULL;
		trans->state = transaction_done;
	    } else 
		return;
	    break;
    
	default:
	    panic(0, "Internal error: Invalid HTTPTransaction state.");
	}
    }

    h = get_redirection_location(trans);
    if (h != NULL) {
	octstr_strip_blanks(h);
	octstr_destroy(trans->url);
	trans->url = h;
	trans->state = request_not_sent;
	http_destroy_headers(trans->response_headers);
	trans->response_headers = list_create();
	octstr_destroy(trans->response_body);
	trans->response_body = octstr_create("");
	--trans->follow_remaining;
	conn_unregister(trans->conn);
	conn_destroy(trans->conn);
	trans->conn = NULL;
	list_produce(pending_requests, trans);
    } else
	list_produce(trans->caller, trans);
    return;

error:
    conn_unregister(trans->conn);
    conn_destroy(trans->conn);
    trans->conn = NULL;
    error(0, "Couldn't fetch <%s>", octstr_get_cstr(trans->url));
    trans->status = -1;
    list_produce(trans->caller, trans);
}


/*
 * This thread starts the transaction: it connects to the server and sends
 * the request. It then sends the transaction to the read_response_thread
 * via started_requests_queue.
 */
static void write_request_thread(void *arg)
{
    HTTPTransaction *trans;
    char *method;
    char buf[128];    
    FDSet *fdset;

    fdset = fdset_create();

    while (run_status == running) {
	trans = list_consume(pending_requests);
	if (trans == NULL)
	    break;

    	gw_assert(trans->state == request_not_sent);

    	if (trans->request_body == NULL)
	    method = "GET";
	else {
	    method = "POST";
	    /* 
	     * Add a Content-Length header.  Override an existing one, if
	     * necessary.  We must have an accurate one in order to use the
	     * connection for more than a single request.
	     */
	    http_header_remove_all(trans->request_headers, "Content-Length");
	    sprintf(buf, "%ld", octstr_len(trans->request_body));
	    http_header_add(trans->request_headers, "Content-Length", buf);
	}

	trans->conn = send_request(trans->url, trans->request_headers,  
				   trans->request_body, method);
    	if (trans->conn == NULL)
	    list_produce(trans->caller, trans);
	else {
	    trans->state = reading_status;
	    conn_register(trans->conn, fdset, handle_transaction, trans);
	}
    }
}


static void start_background_threads(void)
{
    if (!background_threads_are_running) {
	/* 
	 * To be really certain, we must repeat the test, but use the
	 * lock first. If the test failed, however, we _know_ we've
	 * already initialized. This strategy of double testing avoids
	 * using the lock more than a few times at startup.
	 */
	mutex_lock(background_threads_lock);
	if (!background_threads_are_running) {
	    gwthread_create(write_request_thread, NULL);
	    background_threads_are_running = 1;
	}
	mutex_unlock(background_threads_lock);
    }
}
