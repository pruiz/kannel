/*
 * http2.h - HTTP protocol implementation
 *
 * This header file defines the interface to the HTTP implementation
 * in Kannel.
 * 
 * We implement both the client and the server side of the protocol.
 * We don't implement HTTP completely - only those parts that Kannel needs.
 * You may or may not be able to use this code for other projects. It has
 * not been a goal, but it might be possible, though you do need other
 * parts of Kannel's gwlib as well.
 * 
 * Initialization
 * ==============
 *
 * The library MUST be initialized by a call to http_init. Failure to
 * initialize means the library WILL NOT work. Note that the library
 * can't initialize itself implicitly, because it cannot reliably
 * create a mutex to protect the initialization. Therefore, it is the
 * caller's responsibility to call http_init exactly once (no more, no
 * less) at the beginning of the process, before any other thread makes
 * any calls to the library.
 * 
 * Client functionality
 * ====================
 * 
 * The library will invisibly keep the connections to HTTP servers open,
 * so that it is possible to make several HTTP requests over a single
 * TCP connection. This makes it much more efficient in high-load situations.
 * On the other hand, if one request takes long, the library will still
 * use several connections to the same server anyway. The maximal number of
 * concurrently open servers can be set.
 * 
 * The library user can specify an HTTP proxy to be used. There can be only
 * one proxy at a time, but it is possible to specify a list of hosts for
 * which the proxy is not used. The proxy can be changed at run time.
 * 
 * Server functionality
 * ====================
 * 
 * The library allows the implementation of a (simple) HTTP server by
 * implementing functions for creating the well-known port for the
 * server, and accepting and processing client connections. The functions
 * for handling client connections are designed so that they allow 
 * multiple requests from the same client - this is necessary for speed.
 * 
 * Header manipulation
 * ===================
 * 
 * The library additionally has some functions for manipulating lists of
 * headers. These take a `List' (see gwlib/list.h) of Octstr's. The list
 * represents a list of headers in an HTTP request or reply. The functions
 * manipulate the list by adding and removing headers by name. It is a
 * very bad idea to manipulate the list without using the header
 * manipulation functions, however.
 *
 * Basic Authentication
 * ====================
 *
 * Basic Authentication is the standard way for a client to authenticate
 * itself to a server. It is done by adding an "Authorization" header
 * to the request. The interface in this header therefore doesn't mention
 * it, but the client and the server can do it by checking the headers
 * using the generic functions provided.
 *
 * Acknowledgements
 * ================
 *
 * Design: Lars Wirzenius, Richard Braakman
 * Implementation: Sanna Seppänen
 *
 * Temporary implementation note: The prefix for this file is http2 for
 * now. When the old HTTP implementation in Kannel is retired, the prefix
 * will change to http.
 *
 * To do
 * =====
 *
 * - add functions that make it easy to check a list of headers for
 *   a valid Basic Authentication and add an Authentication header
 *   given username and password
 * - how should form variables be encoded when doing a POST? how should
 *   http2_post get them?
 *
 * Implementation plan
 * ===================
 *
 * done 1. http2_get and everything it needs (header manipulation, at least).
 *    At first a very simple system, without proxy support or a socket
 *    pool.
 * done 2. Test harness for http2_get.
 * 3. Enough server things to get rid of old http.
 * done 4. Multiple requests per tcp socket, client and server end.
 * done 5. Proxy support for client end.
 * 6. POST client and server end.
 * 7. Basic auth.
 *
 * Stuff that hasn't been implemented is marked with #if LIW_TODO.
 */


#ifndef HTTP2_H
#define HTTP2_H

#include "gwlib/list.h"
#include "gwlib/octstr.h"


/*
 * Default port to connect to for HTTP connections.
 */
enum { HTTP_PORT = 80 };


/*
 * Maximum number of HTTP redirections to follow.
 */
enum { HTTP_MAX_FOLLOW = 5 };


/*
 * Well-known return values from HTTP servers. This is not a complete
 * list, but it includes the values that Kannel needs to handle
 * specially.
 */
enum {
	HTTP_OK = 200,
	HTTP_NOT_FOUND = 404,
	HTTP_MOVED_PERMANENTLY = 301,
	HTTP_FOUND = 302,
	HTTP_SEE_OTHER = 303
};


/*
 * A structure describing a CGI-BIN argument/variable.
 */
typedef struct {
	Octstr *name;
	Octstr *value;
} HTTPCGIVar;


/*
 * Initialization function. This MUST be called before any other function
 * declared in this header file.
 */
void http2_init(void);


/*
 * Shutdown function. This MUST be called when no other function
 * declared in this header file will be called anymore.
 */
void http2_shutdown(void);


/*
 * Functions for controlling proxy use. http2_use_proxy sets the proxy to
 * use; if another proxy was already in use, it is closed and forgotten
 * about as soon as all existing requests via it have been served.
 *
 * http2_close_proxy closes the current proxy connection, after any
 * pending requests have been served.
 */
void http2_use_proxy(Octstr *hostname, int port, List *exceptions);
void http2_close_proxy(void);


/*
 * Functions for doing a GET request. The difference is that _real follows
 * redirections, plain http2_get does not. Return value is the status
 * code of the request as a numeric value, or -1 if a response from the
 * server was not received. If return value is not -1, reply_headers and
 * reply_body are set and MUST be destroyed by caller.
 */
int http2_get(Octstr *url, List *request_headers, 
		List **reply_headers, Octstr **reply_body);
int http2_get_real(Octstr *url, List *request_headers, Octstr **final_url,
		  List **reply_headers, Octstr **reply_body);


#if LIW_TODO
/*
 * Functions for doing a POST request.
 */
int http2_post(Octstr *url, List *request_headers, List *form_fields,
		List **reply_headers, Octstr **reply_body);
int http2_post_real(Octstr *url, List *request_headers, List *form_fields,
		  List **reply_headers, Octstr **reply_body);
#endif


#if LIW_TODO
/*
 * Functions for controlling the client side socket pool. http2_set_max_sockets
 * sets the maximum number of open client side sockets in the pool.
 * http2_close_all_connections closes all sockets in the pool (after the
 * requests via them have been finished) and http2_close_old_connections
 * closes such sockets that have not been used for a while.
 *
 * http2_close_old_connections SHOULD be called every now and then, if
 * there are no other client side socket functions called.
 *
 * XXX max_sockets total and per host and for the proxy?
 */
void http2_set_max_sockets(int max_sockets);
void http2_close_all_connections(void);
void http2_close_old_connections(void);
#endif


/*
 * Functions for controlling the well-known port of the server.
 * http2_server_open sets it up, http2_server_close closes it.
 */
typedef struct HTTPSocket HTTPSocket;
HTTPSocket *http2_server_open(int port);
void http2_server_close(HTTPSocket *socket);


/*
 * Functions for dealing with a connection to a single client.
 * http2_server_client_accept waits for a new client, and returns a
 * new socket that corresponds to the client. http2_server_client_close
 * closes that connection.
 *
 * http2_server_client_get_request reads the request from a client
 * connection and http2_server_client_send_reply sends the reply. Note
 * that there can be several requests made on the same client socket:
 * the server MUST respond to them in order.
 *
 * http2_server_client_get_request returns, among other things, a parsed
 * list of CGI-BIN arguments/variables as a List whose elements are 
 * pointers to HTTPCGIVar structures (see beginning of file).
 */
HTTPSocket *http2_server_accept_client(HTTPSocket *socket);
void http2_server_close_client(HTTPSocket *client_socket);
int http2_server_get_request(HTTPSocket *client_socket, Octstr **url, 
	List **headers, Octstr **body, List **cgivars);
int http2_server_send_reply(HTTPSocket *client_socket, int status, 
	List *headers, Octstr *body);


/*
 * Functions for manipulating a list of headers. You can use a list of
 * headers returned by one of the functions above, or create an empty
 * list with http2_create_empty_headers. Use http2_destroy_headers to
 * destroy a list of headers (not just the list, but the headers
 * themselves). You can also use http2_parse_header_string to create a list:
 * it takes a textual representation of headers as an Octstr and returns
 * the corresponding List. http2_generate_header_string goes the other
 * way.
 *
 * Once you have a list of headers, you can use http2_header_add and the
 * other functions to manipulate it.
 */
#if LIW_TODO
List *http2_create_empty_headers(void);
void http2_destroy_headers(List *);
List *http2_parse_header_string(Octstr *headers_as_string);
Octstr *http2_generate_header_string(List *headers_as_list);

void http2_header_add(List *headers, char *name, Octstr *contents);
void http2_header_remove_all(List *headers, char *name);
#endif

/*
 * Find the first header called `name' in `headers'. Returns its contents
 * as a new Octet string, which the caller must free. Return NULL for
 * not found.
 */
Octstr *http2_header_find_first(List *headers, char *name);

#if LIW_TODO
List *http2_header_find_all(List *headers, char *name);
#endif

/*
 * Find the Content-Type header and returns the type and charset.
 */
void http2_header_get_content_type(List *headers, Octstr **type, 
	Octstr **charset);

#if LIW_TODO
void http2_header_set_content_type(List *headers, Octstr *type, 
	Octstr *charset);
#endif


#endif
