/*
 * http.c - HTTP protocol implementation
 *
 * Lars Wirzenius
 */
 
/* XXX TODO: 100 status codes. */
/* XXX TODO: Use conn. */

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
static List *started_requests_queue = NULL;


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
    int status;
    List *response_headers;
    Octstr *response_body;
    HTTPSocket *socket;
    int retrying;
    int follow_remaining;
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
static HTTPSocket *pool_allocate(Octstr *host, int port);
static void pool_free(HTTPSocket *p);
static void pool_free_and_close(HTTPSocket *p);
static void pool_kill_old_ones(void);
static int pool_socket_old_and_unused(void *a, void *b);


/*
 * Operations on HTTPSockets.
 */
static HTTPSocket *socket_create_client(Octstr *host, int port);
static HTTPSocket *socket_create_server(int port);
static void socket_close(HTTPSocket *p);
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

static Octstr *build_request(Octstr *path_or_url, Octstr *host, long port,
                             List *headers, Octstr *request_body, 
			     char* method_name);

static HTTPSocket *send_request(Octstr *url, List *request_headers,
                                Octstr *request_body, char *method_name);



static int read_status(HTTPSocket *p);
static int read_headers(HTTPSocket *p, List **headers);
static int read_body(HTTPSocket *p, List *headers, Octstr **body);
static int read_chunked_body(HTTPSocket *p, Octstr **body, List *headers);
static int read_raw_body(HTTPSocket *p, Octstr **body, long bytes);
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
    started_requests_queue = list_create();
    list_add_producer(started_requests_queue);
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


void http_header_remove_all(List *headers, char *name)
{
    long i;
    Octstr *h;

    gwlib_assert_init();
    gw_assert(headers != NULL);
    gw_assert(name != NULL);

    i = 0;
    while (i < list_len(headers)) {
	h = list_get(headers, i);
	if (header_is_called(h, name)) {
	    list_delete(headers, i, 1);
	    octstr_destroy(h);
	} else
	    i++;
    }
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


enum { POOL_MAX_IDLE = 300 };


static List *pool = NULL;


static void pool_init(void)
{
    pool = list_create();
}


static void pool_shutdown(void)
{
    HTTPSocket *p;

    while ((p = list_extract_first(pool)) != NULL)
        socket_destroy(p);
    list_destroy(pool, NULL);
}


static HTTPSocket *pool_allocate(Octstr *host, int port)
{
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
    }

    p->in_use = 1;
    list_unlock(pool);

    return p;
}


static void pool_free(HTTPSocket *p)
{
    gw_assert(p != NULL);
    gw_assert(p->in_use);
    time(&p->last_used);
    p->in_use = 0;
}


static void pool_free_and_close(HTTPSocket *p)
{
    gw_assert(p->in_use);

    list_lock(pool);
    list_delete_equal(pool, p);
    list_unlock(pool);

    socket_destroy(p);
}


/* Assume pool is locked already. */
static void pool_kill_old_ones(void)
{
    time_t now;
    List *list;
    HTTPSocket *p;

    time(&now);
    list = list_extract_matching(pool, &now, pool_socket_old_and_unused);
    if (list != NULL) {
        while ((p = list_extract_first(list)) != NULL)
            socket_destroy(p);
        list_destroy(list, NULL);
    }
}


static int pool_socket_old_and_unused(void *a, void *b)
{
    HTTPSocket *p;
    time_t now;

    p = a;
    now = *(time_t *) b;
    return !p->in_use && p->last_used != (time_t) - 1 &&
           difftime(now, p->last_used) > POOL_MAX_IDLE;
}


/***********************************************************************
 * Operations on HTTPSockets, whether from the socket pool or not.
 */


/*
 * Create a new client side HTTPSocket.
 */
static HTTPSocket *socket_create_client(Octstr *host, int port)
{
    HTTPSocket *p;

    debug("gwlib.http", 0, "HTTP: Creating a new client socket <%s:%d>.",
          octstr_get_cstr(host), port);
    p = gw_malloc(sizeof(HTTPSocket));
    p->conn = conn_open_tcp(host, port);

    if (p->conn == NULL) {
        gw_free(p);
        return NULL;
    }

    p->server_socket = -1;
    p->in_use = 0;
    p->use_version_1_0 = 0;
    p->last_used = (time_t) - 1;
    p->host = octstr_duplicate(host);
    p->port = port;

    return p;
}


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
 * Read `bytes' bytes from the socket `socket'. Use the pool buffer and
 * and fill it with new data from the socket as necessary. Return -1 for
 * error, 0 for EOF, or >0 for OK (exactly `bytes' bytes in `os'
 * returned). If OK, caller must free `*os'.
 */
static int socket_read_bytes(HTTPSocket *p, Octstr **os, long bytes)
{
    gw_assert(p->server_socket == -1);
    if (p->conn == NULL)
    	return 0;

    for (;;) {
	*os = conn_read_fixed(p->conn, bytes);
	if (*os != NULL)
	    return 1;
	if (conn_wait(p->conn, -1) == -1)
	    return -1;
    	if (conn_eof(p->conn))
	    return 0;
    }
}



/*
 * Read all remaining data from socket. Return -1 for error, 0 for OK.
 */
static int socket_read_to_eof(HTTPSocket *p, Octstr **os)
{
    gw_assert(p->server_socket == -1);
    if (p->conn == NULL)
    	return -1;
    
    do {
	if (conn_wait(p->conn, -1) == -1)
	    return -1;
    } while (!conn_eof(p->conn));
    *os = conn_read_fixed(p->conn, conn_inbuf_len(p->conn));
    return 0;
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

static HTTPSocket *send_request(Octstr *url, List *request_headers,
                                Octstr *request_body, char *method_name)
{
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
        request = build_request(url, host, port, request_headers,
                                request_body, method_name);
        p = pool_allocate(proxy_hostname, proxy_port);
    } else {
        request = build_request(path, host, port, request_headers,
                                request_body, method_name);
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
static int read_status(HTTPSocket *p)
{
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
 * Read the body of a response. Return -1 for error, 0 for OK (EOF on
 * socket reached) or 1 for OK (socket can be used for another transaction).
 */
static int read_body(HTTPSocket *p, List *headers, Octstr **body)
{
    Octstr *h;
    long body_len;

    h = http_header_find_first(headers, "Transfer-Encoding");
    if (h != NULL) {
        octstr_strip_blanks(h);
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
static int read_chunked_body(HTTPSocket *p, Octstr **body, List *headers)
{
    Octstr *line, *chunk;
    long len;
    List *trailer;

    *body = octstr_create("");
    line = NULL;

    for (; ; ) {
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
    list_destroy(trailer, NULL);

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
static int read_raw_body(HTTPSocket *p, Octstr **body, long bytes)
{
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
    trans->status = -1;
    trans->response_headers = NULL;
    trans->response_body = NULL;
    trans->socket = NULL;
    trans->retrying = 0;
    trans->follow_remaining = follow_remaining;
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

    while (run_status == running) {
	trans = list_consume(pending_requests);
	if (trans == NULL)
	    break;

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

	trans->socket = send_request(trans->url, trans->request_headers,  
	    	    	    	     trans->request_body, method);
    	if (trans->socket == NULL)
	    list_produce(trans->caller, trans);
	else
	    list_produce(started_requests_queue, trans);
    }
    list_remove_producer(started_requests_queue);
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

static void read_response_thread(void *arg)
{
    HTTPTransaction *trans;
    int ret;
    Octstr *h;
    
    while (run_status == running) {
	trans = list_consume(started_requests_queue);
	if (trans == NULL)
	    break;

	trans->status = read_status(trans->socket);
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
		pool_free_and_close(trans->socket);
		trans->retrying = 1;
		list_produce(pending_requests, trans);
		continue;
	    }
	}
    
	if (read_headers(trans->socket, &trans->response_headers) == -1)
	    goto error;
    
	ret = read_body(trans->socket, 
	    	    	trans->response_headers, 
	    	    	&trans->response_body);
	switch (ret) {
	case -1:
	    goto error;
	case 0:
	    pool_free_and_close(trans->socket);
	    break;
	default:
	    pool_free(trans->socket);
	    break;
	}

    	h = get_redirection_location(trans);
    	if (h != NULL) {
	    octstr_strip_blanks(h);
	    octstr_destroy(trans->url);
	    trans->url = h;
	    http_destroy_headers(trans->response_headers);
	    octstr_destroy(trans->response_body);
	    --trans->follow_remaining;
	    list_produce(pending_requests, trans);
	    continue;
	}

	list_produce(trans->caller, trans);
	continue;
    
    error:
    	pool_free(trans->socket);
	error(0, "Couldn't fetch <%s>", octstr_get_cstr(trans->url));
	trans->status = -1;
	list_produce(trans->caller, trans);
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
	    gwthread_create(read_response_thread, NULL);
	    background_threads_are_running = 1;
	}
	mutex_unlock(background_threads_lock);
    }
}
