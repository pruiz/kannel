/*
 * test_http2.c - a simple program to test the http2 library, server end
 *
 * Lars Wirzenius
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "gwlib/http2.h"

static void client_thread(void *arg) {
	HTTPSocket *client_socket;
	Octstr *os, *body, *url;
	List *headers, *resph, *cgivars;
	int ret;
	HTTPCGIVar *v;
	
	client_socket = arg;

	for (;;) {
		ret = http2_server_get_request(client_socket, &url, &headers, 
				&body, &cgivars);
		if (ret == -1) {
			error(0, "http2_server_get_request failed");
			goto error;
		}
		if (ret == 0)
			break;
		debug("test.http2", 0, "Request for <%s>", 
			octstr_get_cstr(url));
		while ((v = list_extract_first(cgivars)) != NULL) {
			debug("test.http2", 0, "Var: <%s>=<%s>",
				octstr_get_cstr(v->name), 
				octstr_get_cstr(v->value));
			octstr_destroy(v->name);
			octstr_destroy(v->value);
			gw_free(v);
		}
		list_destroy(cgivars);

		octstr_destroy(url);
		octstr_destroy(body);
		while ((os = list_extract_first(headers)) != NULL)
			octstr_destroy(os);
		list_destroy(headers);

		resph = list_create();
		list_append(resph, octstr_create("Content-Type: text/plain"));
		body = octstr_create("hello, world\n");
		ret = http2_server_send_reply(client_socket, HTTP_OK, 
			resph, body);

		while ((os = list_extract_first(resph)) != NULL)
			octstr_destroy(os);
		list_destroy(resph);
		octstr_destroy(body);

		if (ret == -1) {
			error(0, "http2_server_send_reply failed");
			goto error;
		}
	}


	info(0, "Done with client.");
error:
	http2_server_close_client(client_socket);
}

static void help(void) {
	info(0, "Usage: test_http2_server [-p port]\n");
}

int main(int argc, char **argv) {
	int opt, port, use_threads;
	HTTPSocket *httpd_socket, *client_socket;
	
	gwlib_init();

	port = 8080;
	use_threads = 0;

	while ((opt = getopt(argc, argv, "hv:p:t")) != EOF) {
		switch (opt) {
		case 'v':
			set_output_level(atoi(optarg));
			break;

		case 'h':
			help();
			exit(0);
			
		case 'p':
			port = atoi(optarg);
			break;
		
		case 't':
			use_threads = 1;
			break;

		case '?':
		default:
			error(0, "Invalid option %c", opt);
			help();
			panic(0, "Stopping.");
		}
	}

	httpd_socket = http2_server_open(port);
	if (httpd_socket == NULL)
		panic(0, "http2_server_open failed");
	for (;;) {
		client_socket = http2_server_accept_client(httpd_socket);
		if (client_socket == NULL)
			panic(0, "http2_server_accept_client failed");
		if (use_threads)
			gwthread_create(client_thread, client_socket);
		else
			client_thread(client_socket);
	}
	http2_server_close(httpd_socket);
	
	gwlib_shutdown();
	return 0;
}
