/*
 * http.h - interface to HTTP subsystem
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

enum http_results {
	HTTP_RESULT_OK = 200,
};

enum http_transfer_encoding {
	HTTP_ENCODING_8BIT,
	HTTP_ENCODING_CHUNKED,
};

enum http_type {
	HTTP_TYPE_HTML,
	HTTP_TYPE_TEXT,
	HTTP_TYPE_UNKNOWN,
};


/* Fetch the document specified by an URL. Return -1 for error, or 0 for OK.
   If ok, return the type, contents and size via type, data. and size. */
int http_get(char *url, char **type, char **data, size_t *size);

/* Set up a HTTP server socket listening at port `port'. Return -1 for error,
or the socket file descriptor for success. */
 
int httpserver_setup(int port);
 
/* Wait until the HTTP server socket `socket' gets a client connection,
read the client's request, and return the path of the request and
the arguments to a CGI-BIN, if any, via `path' and `args' as dynamically
allocated strings which the caller will free. Return -1 for error, 
0 for OK. 
 
The caller will reply to the request by writing directly to the socket.
Implementation note: This function will do a blocking accept(2) system
call internally. It will thus block the calling thread. This is a feature
for now.

*/

int httpserver_get_request(int socket, char **client_ip, char **path, char **args);

/* 
*/
int httpserver_answer(int socket, char* text);

enum http_action {

	HTTP_CLIENT,
	HTTP_SERVER,
	HTTP_GATEWAY,
	HTTP_CACHE,

};


/*****************************************************************************
* This is where we store parsed URL information
*/
typedef struct URL {

	char *scheme;
	char *host;
	int   port;
	char *abs_path;
	char *query;
	char *username;
	char *password;

} URL;


/*****************************************************************************
* HTTP Request information
*/
typedef struct HTTPRequest {

	int action;
	int status;
	int http_version_major, http_version_minor;
	URL *url;
	char temp[1024];
	struct HTTPHeader *baseheader;
	char   *data;
	size_t data_length;

} HTTPRequest;


/*****************************************************************************
* The chain of headers are constructed out of these
*/
typedef struct HTTPHeader {

	struct HTTPHeader *next;
	char *key;
	char *value;

} HTTPHeader;

URL* internal_url_create(char *text);
int internal_url_destroy(URL *url);
URL* internal_url_relative_to_absolute(URL* baseURL, char *relativepath);

/*
int httpserver_answer_100_continue(int socket, char* text);
int httpserver_answer_101_switching_protocols(int socket, char* text);

int httpserver_answer_200_ok(int socket, char* text);
int httpserver_answer_201_created(int socket, char* text);
int httpserver_answer_202_accepted(int socket, char* text);
int httpserver_answer_203_non_authoritative_information(int socket, char* text);
int httpserver_answer_204_no_contents(int socket, char* text);
int httpserver_answer_205_reset_content(int socket, char* text);

int httpserver_answer_300_multiple_choices(int socket, char* text);
int httpserver_answer_301_moved_permanently(int socket, char* text);
int httpserver_answer_302_found(int socket, char* text);
int httpserver_answer_303_see_other(int socket, char* text);
int httpserver_answer_304_not_modified(int socket, char* text);
int httpserver_answer_305_use_proxy(int socket, char* text);
int httpserver_answer_307_temporary_redirect(int socket, char* text);

int httpserver_answer_400_bad_request(int socket, char* text);
int httpserver_answer_401_unauthorized(int socket, char* text);
int httpserver_answer_402_payment_required(int socket, char* text);
int httpserver_answer_403_forbidden(int socket, char* text);
int httpserver_answer_404_not_found(int socket, char* text);
int httpserver_answer_405_method_not_allowed(int socket, char* text);
int httpserver_answer_406_not_acceptable(int socket, char* text);
int httpserver_answer_407_proxy_authentication_required(int socket, char* text);
int httpserver_answer_408_request_timeout(int socket, char* text);
int httpserver_answer_409_conflict(int socket, char* text);
int httpserver_answer_410_gone(int socket, char* text);
int httpserver_answer_411_length_required(int socket, char* text);
int httpserver_answer_412_precondition_failed(int socket, char* text);
int httpserver_answer_413_request_entity_too_large(int socket, char* text);
int httpserver_answer_414_request_uri_too_long(int socket, char* text);
int httpserver_answer_415_unsupported_media_type(int socket, char* text);
int httpserver_answer_416_requested_range_not_satisfiable(int socket, char* text);
int httpserver_answer_417_expectation_failed(int socket, char* text);

int httpserver_answer_500_internal_server_error(int socket, char* text);
int httpserver_answer_501_not_implemented(int socket, char* text);
int httpserver_answer_502_bad_gateway(int socket, char* text);
int httpserver_answer_503_service_unavailable(int socket, char* text);
int httpserver_answer_504_gateway_timeout(int socket, char* text);
int httpserver_answer_505_http_version_not_supported(int socket, char* text);
*/

/*

tcpip_create_client_socket
tcpip_create_server_socket
tcpip_close_socket
tcpip_read_from_socket
tcpip_write_to_socket

buffer_create
buffer_destroy
buffer_append
buffer_copy
buffer_cut
buffer_insert

httpsession_create
httpsession_destroy
httpsession_lock
httpsession_unlock

httprequest_create
httprequest_destroy
httprequest_header_get
httprequest_header_put

httpclient_session_open
httpclient_session_close
httpclient_session_join
httpclient_speak
httpclient_listen

httpserver_session_open
httpserver_session_close
httpserver_session_join
httpserver_speak
httpserver_listen

*/


HTTPRequest* httprequest_create(URL *url, char *payload);
int httprequest_destroy(HTTPRequest *request);
HTTPRequest* httprequest_execute(HTTPRequest *request);
HTTPRequest* httprequest_wrap(char *from, size_t size);
char* httprequest_unwrap(HTTPRequest *from);

int httprequest_add_header(HTTPRequest *request, char *key, char *value);
int httprequest_remove_header(HTTPRequest *request, char *key);
char* httprequest_get_header_value(HTTPRequest *request, char *key);

#endif
