/*
 * http.h - HTTP protocol implementation
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
 * Implementation: Lars Wirzenius
 *
 * To do
 * =====
 *
 * - add functions that make it easy to check a list of headers for
 *   a valid Basic Authentication and add an Authentication header
 *   given username and password
 */


#ifndef HTTP_H
#define HTTP_H

#include "gwlib/list.h"
#include "gwlib/octstr.h"


/*
 * Default port to connect to for HTTP connections.
 */
enum { HTTP_PORT = 80 };


/*
 * Well-known return values from HTTP servers. This is not a complete
 * list, but it includes the values that Kannel needs to handle
 * specially.
 */

/*
 * Need to support some extra return values for POST support.
 *
 */

enum {
	HTTP_OK				= 200,
	HTTP_CREATED			= 201,
	HTTP_ACCEPTED			= 202,
	HTTP_NO_CONTENT			= 204,
	HTTP_RESET_CONTENT		= 205,
	HTTP_MOVED_PERMANENTLY		= 301,
	HTTP_FOUND			= 302,
	HTTP_SEE_OTHER			= 303,
	HTTP_TEMPORARY_REDIRECT 	= 307,
	HTTP_NOT_FOUND			= 404,
	HTTP_REQUEST_ENTITY_TOO_LARGE   = 413,
	HTTP_INTERNAL_SERVER_ERROR	= 500,
	HTTP_NOT_IMPLEMENTED		= 501,
	HTTP_BAD_GATEWAY		= 502
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
void http_init(void);


/*
 * Shutdown function. This MUST be called when no other function
 * declared in this header file will be called anymore.
 */
void http_shutdown(void);


/*
 * Functions for controlling proxy use. http_use_proxy sets the proxy to
 * use; if another proxy was already in use, it is closed and forgotten
 * about as soon as all existing requests via it have been served.
 *
 * http_close_proxy closes the current proxy connection, after any
 * pending requests have been served.
 */
void http_use_proxy(Octstr *hostname, int port, List *exceptions);
void http_close_proxy(void);


/*
 * Functions for doing a GET request. The difference is that _real follows
 * redirections, plain http_get does not. Return value is the status
 * code of the request as a numeric value, or -1 if a response from the
 * server was not received. If return value is not -1, reply_headers and
 * reply_body are set and MUST be destroyed by caller.
 */
int http_get(Octstr *url, List *request_headers, 
		List **reply_headers, Octstr **reply_body);
int http_get_real(Octstr *url, List *request_headers, Octstr **final_url,
		  List **reply_headers, Octstr **reply_body);


int http_post(Octstr *url, List *request_headers, Octstr *request_body,
		List **reply_headers, Octstr **reply_body);


/*
 * An identification for a caller of HTTP. This is used with http_start_get,
 * http_start_post, and http_receive_result to route results to the right
 * callers.
 *
 * Implementation note: We use a List as the type so that we can use
 * that list for communicating the results. This makes it unnecessary
 * to map the caller identifier to a List internally in the HTTP module.
 */
typedef List HTTPCaller;


/*
 * Create an HTTP caller identifier.
 */
HTTPCaller *http_caller_create(void);


/*
 * Destroy an HTTP caller identifier. Those that aren't destroyed
 * explicitly are destroyed by http_shutdown.
 */
void http_caller_destroy(HTTPCaller *caller);


/*
 * Start an HTTP request. It will be completed in the background, and
 * the result will eventually be received by http_receive_result. The
 * return value is the request identifier; http_receive_result will
 * return the same request identifier, and the caller can use this to
 * keep track of which request and which response belong together.
 *
 * If `body' is NULL, it is a GET request, otherwise as POST request.
 * If `follow' is true, HTTP redirections are followed, otherwise not.
 */
long http_start_request(HTTPCaller *caller, Octstr *url, List *headers,
    	    	    	Octstr *body, int follow);


/*
 * Get the result of a GET or a POST request.
 */
long http_receive_result(HTTPCaller *caller, int *status, Octstr **final_url,
    	    	    	 List **headers, Octstr **body);


typedef struct HTTPClient HTTPClient;
int http_open_server(int port);
HTTPClient *http_accept_request(Octstr **client_ip, Octstr **url, 
    	    	    	    	List **headers, Octstr **body, 
				List **cgivars);
void http_send_reply(HTTPClient *client, int status, List *headers, 
    	    	     Octstr *body);
void http_close_client(HTTPClient *client);
void http_close_all_servers(void);
/* XXX http_close_port(port); http_close_all_ports(); */


/*
 * Functions for controlling the well-known port of the server.
 * http_server_open sets it up, http_server_close closes it.
 */
typedef struct HTTPSocket HTTPSocket;	/* XXX move to beginning of file */
HTTPSocket *http_server_open(int port);
void http_server_close(HTTPSocket *socket);


/*
 * Functions for dealing with a connection to a single client.
 * http_server_client_accept waits for a new client, and returns a
 * new socket that corresponds to the client. http_server_client_close
 * closes that connection.
 *
 * http_server_client_get_request reads the request from a client
 * connection and http_server_client_send_reply sends the reply. Note
 * that there can be several requests made on the same client socket:
 * the server MUST respond to them in order.
 *
 * http_server_client_get_request returns, among other things, a parsed
 * list of CGI-BIN arguments/variables as a List whose elements are 
 * pointers to HTTPCGIVar structures (see beginning of file).
 */
HTTPSocket *http_server_accept_client(HTTPSocket *socket);
void http_server_close_client(HTTPSocket *client_socket);
int http_server_get_request(HTTPSocket *client_socket, Octstr **url, 
	List **headers, Octstr **body, List **cgivars);
int http_server_send_reply(HTTPSocket *client_socket, int status, 
	List *headers, Octstr *body);
int http_socket_fd(HTTPSocket *socket);
/* return reference to IP of the client */
Octstr *http_socket_ip(HTTPSocket *socket);

/*
 * destroy args given up by the get_request. Non-thread safe
 */
void http_destroy_cgiargs(List *args);

/*
 * return reference to cgi argument 'name', or NULL if not matching
 */
Octstr *http_cgi_variable(List *list, char *name);

/*
 * Functions for manipulating a list of headers. You can use a list of
 * headers returned by one of the functions above, or create an empty
 * list with http_create_empty_headers. Use http_destroy_headers to
 * destroy a list of headers (not just the list, but the headers
 * themselves). You can also use http_parse_header_string to create a list:
 * it takes a textual representation of headers as an Octstr and returns
 * the corresponding List. http_generate_header_string goes the other
 * way.
 *
 * Once you have a list of headers, you can use http_header_add and the
 * other functions to manipulate it.
 */
List *http_create_empty_headers(void);
void http_destroy_headers(List *headers);
void http_header_add(List *headers, char *name, char *contents);
void http_header_get(List *headers, long i, Octstr **name, Octstr **value);
List *http_header_duplicate(List *headers);
void http_header_pack(List *headers);
void http_append_headers(List *to, List *from);
Octstr *http_header_value(Octstr *header);

/*
 * Append all headers from new_headers to old_headers.  Headers from
 * new_headers _replace_ the ones in old_headers if they have the same
 * name.  For example, if you have:
 * old_headers
 *    Accept: text/html
 *    Accept: text/plain
 *    Accept: image/jpeg
 *    Accept-Language: en
 * new_headers
 *    Accept: text/html
 *    Accept: text/plain
 * then after the operation, old_headers will have
 *    Accept-Language: en
 *    Accept: text/html
 *    Accept: text/plain
 */
void http_header_combine(List *old_headers, List *new_headers);

/*
 * Return the length of the quoted-string (a HTTP field element)
 * starting at position pos in the header.  Return -1 if there
 * is no quoted-string at that position.
 */
long http_header_quoted_string_len(Octstr *header, long pos);

/*
 * Take the value part of a header that has a format that allows
 * multiple comma-separated elements, and split it into a list of
 * those elements.  Note that the function may have surprising
 * results for values of headers that are not in this format.
 */
List *http_header_split_value(Octstr *value);

/*
 * The same as http_header_split_value, except that it splits 
 * headers containing 'credentials' or 'challenge' lists, which
 * have a slightly different format.  It also normalizes the list
 * elements, so that parameters are introduced with ';'.
 */
List *http_header_split_auth_value(Octstr *value);

#if LIW_TODO
List *http_parse_header_string(Octstr *headers_as_string);
Octstr *http_generate_header_string(List *headers_as_list);
#endif

/*
 * Remove all headers with name 'name' from the list.  Return the
 * number of headers removed.
 */
long http_header_remove_all(List *headers, char *name);

/*
 * Remove the hop-by-hop headers from a header list.  These are the
 * headers that describe a specific connection, not anything about
 * the content.  RFC2616 section 13.5.1 defines these.
 */
void http_remove_hop_headers(List *headers);

/*
 * Update the headers to reflect that a transformation has been
 * applied to the entity body.
 */
void http_header_mark_transformation(List *headers,
Octstr *new_body, Octstr *new_type);


/*
 * Find the first header called `name' in `headers'. Returns its contents
 * as a new Octet string, which the caller must free. Return NULL for
 * not found.
 */
Octstr *http_header_find_first(List *headers, char *name);
List *http_header_find_all(List *headers, char *name);


/*
 * Find the Content-Type header and returns the type and charset.
 */
void http_header_get_content_type(List *headers, Octstr **type, 
	Octstr **charset);

#if LIW_TODO
void http_header_set_content_type(List *headers, Octstr *type, 
	Octstr *charset);
#endif


/*
 * Do the headers indicate that MIME type `type' is accepted?
 */
int http_type_accepted(List *headers, char *type);


/*
 * Dump the contents of a header list with debug.
 */
void http_header_dump(List *headers);

/*
 * Check for acceptable charset
 */
int http_charset_accepted(List *headers, char *charset);


#endif
