/*
 * test_http.c - a simple program to test the http library, server end
 *
 * Lars Wirzenius
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gwlib/http.h"

static volatile sig_atomic_t run = 1;

static void client_thread(void *arg) 
{
    HTTPClient *client;
    Octstr *body, *url, *ip;
    List *headers, *resph, *cgivars;
    HTTPCGIVar *v;
    Octstr *reply_body, *reply_type;
    
    if (arg == NULL) {
	reply_body = octstr_create("hello, world");
	reply_type = octstr_create("Content-Type: text/plain; "
	    	    	    	   "charset=\"UTF-8\"");
    } else {
	reply_body = arg;
	reply_type = octstr_create("Content-Type: text/vnd.wap.wml");
    }
    
    resph = list_create();
    list_append(resph, reply_type);

    while (run) {
	client = http_accept_request(&ip, &url, &headers, &body, &cgivars);
	if (client == NULL)
	    break;

	debug("test.http", 0, "Request for <%s>", octstr_get_cstr(url));
	while ((v = list_extract_first(cgivars)) != NULL) {
	    debug("test.http", 0, "Var: <%s>=<%s>",
		  octstr_get_cstr(v->name), 
		  octstr_get_cstr(v->value));
	    octstr_destroy(v->name);
	    octstr_destroy(v->value);
	    gw_free(v);
	}
	list_destroy(cgivars, NULL);
    
    	if (octstr_compare(url, octstr_imm("/quit")) == 0)
	    run = 0;

	octstr_destroy(ip);
	octstr_destroy(url);
	octstr_destroy(body);
	list_destroy(headers, octstr_destroy_item);
    
	http_send_reply(client, HTTP_OK, resph, reply_body);
    }

    list_destroy(resph, octstr_destroy_item);
    octstr_destroy(reply_body);
    debug("test.http", 0, "client_thread terminates");
}

static void help(void) {
    info(0, "Usage: test_http_server [-p port]\n");
}

static void sigterm(int signo) {
    run = 0;
    http_close_all_servers();
    debug("test.gwlib", 0, "Signal %d received, quitting.", signo);
}

int main(int argc, char **argv) {
    int opt, port, use_threads;
    struct sigaction act;
    char *filename;
    Octstr *file_contents;

    gwlib_init();

    act.sa_handler = sigterm;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGTERM, &act, NULL);

    port = 8080;
    use_threads = 0;
    filename = NULL;

    while ((opt = getopt(argc, argv, "hv:p:tf:")) != EOF) {
	switch (opt) {
	case 'v':
	    log_set_output_level(atoi(optarg));
	    break;

	case 'h':
	    help();
	    exit(0);

	case 'p':
	    port = atoi(optarg);
	    break;

	case 't':
	    use_threads = 1; /* XXX unimplemented as of now */
	    break;

	case 'f':
	    filename = optarg;
	    break;

	case '?':
	default:
	    error(0, "Invalid option %c", opt);
	    help();
	    panic(0, "Stopping.");
	}
    }

    if (filename == NULL)
    	file_contents = NULL;
    else
    	file_contents = octstr_read_file(filename);

    if (http_open_server(port) == -1)
	panic(0, "http_open_server failed");

    client_thread(file_contents);

    debug("test.http", 0, "Program exiting normally.");
    gwlib_shutdown();
    return 0;
}
