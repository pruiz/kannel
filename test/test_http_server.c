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
static int port,
           verbose;

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
	client = http_accept_request(port, &ip, &url, &headers, &body, 
	    	    	    	     &cgivars);
	if (client == NULL)
	    break;

	debug("test.http", 0, "Request for <%s> from <%s>", 
              octstr_get_cstr(url), octstr_get_cstr(ip));
        if (verbose)
            debug("test.http", 0, "Cgivars were");
	while ((v = list_extract_first(cgivars)) != NULL) {
	    if (verbose) {
	        octstr_dump(v->name, 0);
	        octstr_dump(v->value, 0);
            }
	    octstr_destroy(v->name);
	    octstr_destroy(v->value);
	    gw_free(v);
	}
	list_destroy(cgivars, NULL);
    
    	if (octstr_compare(url, octstr_imm("/quit")) == 0) {
	    run = 0;
        } else if (octstr_compare(url, octstr_imm("/whitelist")) == 0) {
	    octstr_destroy(reply_body);
            reply_body = octstr_imm("+358408676001;+358408676005;\n"
                                    "+358408676006\n");
	} else if (octstr_compare(url, octstr_imm("/blacklist")) == 0) {
            octstr_destroy(reply_body);
            reply_body = octstr_imm("+358408676004\n"
                                    "+358408676002;+358408676003\n");
        }
        
        if (verbose) {
            debug("test.http", 0, "request headers were");
            http_header_dump(headers);
            if (body != NULL) {
               debug("test.http", 0, "request body was");
               octstr_dump(body, 0);
            }
        }

	octstr_destroy(ip);
	octstr_destroy(url);
	octstr_destroy(body);
	list_destroy(headers, octstr_destroy_item);
    
	http_send_reply(client, HTTP_OK, resph, reply_body);
    }

    list_destroy(resph, octstr_destroy_item);
    octstr_destroy(reply_body);
    debug("test.http", 0, "client_thread terminates");
    http_close_all_ports();
}

static void help(void) {
    info(0, "Usage: test_http_server [-v loglevel][-l logfile][-f file][-h][-q][-p port]\n");
}

static void sigterm(int signo) {
    run = 0;
    http_close_all_ports();
    debug("test.gwlib", 0, "Signal %d received, quitting.", signo);
}

int main(int argc, char **argv) {
    int opt, use_threads;
    struct sigaction act;
    char *filename;
    Octstr *log_filename;
    Octstr *file_contents;
    int ssl = 0;   /* indicate if SSL-enabled server should be used */

    gwlib_init();

    act.sa_handler = sigterm;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGTERM, &act, NULL);

    port = 8080;
    use_threads = 0;
    verbose = 1;
    filename = NULL;
    log_filename = NULL;

    while ((opt = getopt(argc, argv, "hqv:p:tf:l:")) != EOF) {
	switch (opt) {
	case 'v':
	    log_set_output_level(atoi(optarg));
	    break;

        case 'q':
	    verbose = 0;
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

	case 'l':
	    octstr_destroy(log_filename);
	    log_filename = octstr_create(optarg);
	    break;

	case '?':
	default:
	    error(0, "Invalid option %c", opt);
	    help();
	    panic(0, "Stopping.");
	}
    }

    if (log_filename != NULL) {
    	log_open(octstr_get_cstr(log_filename), GW_DEBUG);
	octstr_destroy(log_filename);
    }

    if (filename == NULL)
    	file_contents = NULL;
    else
    	file_contents = octstr_read_file(filename);

    if (http_open_port(port, ssl) == -1)
	panic(0, "http_open_server failed");

    /*
     * Do the real work in a separate thread so that the main
     * thread can catch signals safely.
     */
    if (gwthread_create(client_thread, file_contents) < 0)
	panic(0, "Cannot create client thread.");
    gwthread_join_every(client_thread);

    debug("test.http", 0, "Program exiting normally.");
    gwlib_shutdown();
    return 0;
}
