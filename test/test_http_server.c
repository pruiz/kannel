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

#define MAX_THREADS 1024

Octstr *whitelist,
       *blacklist;

int verbose, run, port;
int ssl = 0;   /* indicate if SSL-enabled server should be used */

static void client_thread(void *arg) 
{
    HTTPClient *client;
    Octstr *body, *url, *ip;
    List *headers, *resph, *cgivars;
    HTTPCGIVar *v;
    Octstr *reply_body, *reply_type;
    unsigned long n = 0;
    int status, i;

    while (run) {
        client = http_accept_request(port, &ip, &url, &headers, &body, &cgivars);

        n++;
        if (client == NULL)
            break;

        debug("test.http", 0, "Request for <%s> from <%s>", 
              octstr_get_cstr(url), octstr_get_cstr(ip));
        if (verbose)
            debug("test.http", 0, "CGI vars were");

        /*
         * Don't use list_extract() here, otherwise we don't have a chance
         * to re-use the cgivars later on.
         */
        for (i = 0; i < list_len(cgivars); i++) {
            if ((v = list_get(cgivars, i)) != NULL && verbose) {
                octstr_dump(v->name, 0);
                octstr_dump(v->value, 0);
            }
        }
    
        if (arg == NULL) {
            reply_body = octstr_create("Sent.");
            reply_type = octstr_create("Content-Type: text/plain; "
                                       "charset=\"UTF-8\"");
        } else {
            reply_body = arg;
            reply_type = octstr_create("Content-Type: text/vnd.wap.wml");
        }

        resph = list_create();
        list_append(resph, reply_type);

        status = HTTP_OK;

        /* check for special URIs and handle those */
        if (octstr_compare(url, octstr_imm("/quit")) == 0) {
	       run = 0;
        } else if (octstr_compare(url, octstr_imm("/whitelist")) == 0) {
	       octstr_destroy(reply_body);
            if (whitelist != NULL) {
                if (verbose) {
                    debug("test.http.server", 0, "we send a white list");
                    octstr_dump(whitelist, 0);
                }
                reply_body = octstr_duplicate(whitelist);
            } else {
	           reply_body = octstr_imm("");
	       }
        } else if (octstr_compare(url, octstr_imm("/blacklist")) == 0) {
            octstr_destroy(reply_body);
            if (blacklist != NULL) {
                if (verbose) {
                    debug("test.http.server", 0, "we send a blacklist");
                    octstr_dump(blacklist, 0);
                }
                reply_body = octstr_duplicate(blacklist);
            } else {
                reply_body = octstr_imm("");
            } 
        } else if (octstr_compare(url, octstr_imm("/save")) == 0) {
            /* safe the body into a temporary file */
            pid_t pid = getpid();
            FILE *f = fopen(octstr_get_cstr(octstr_format("/tmp/body.%ld.%ld", pid, n)), "w");
            octstr_print(f, body);
            fclose(f);
        } else if (octstr_compare(url, octstr_imm("/redirect/")) == 0) {
            /* provide us with a HTTP 302 redirection response
             * will return /redirect/<pid> for the location header 
             * and will return /redirect/ if cgivar loop is set to allow looping
             */
            Octstr *redirect_header, *scheme, *uri, *l;
            pid_t pid = getpid();

            uri = ((l = http_cgi_variable(cgivars, "loop")) != NULL) ?
                octstr_format("%s?loop=%s", octstr_get_cstr(url), 
                              octstr_get_cstr(l)) : 
                octstr_format("%s%ld", octstr_get_cstr(url), pid);

            octstr_destroy(reply_body);
            reply_body = octstr_imm("Here you got a redirection URL that you should follow.");
            scheme = ssl ? octstr_imm("https://") : octstr_imm("http://");
            redirect_header = octstr_format("Location: %s%s%s", 
                octstr_get_cstr(scheme),
                octstr_get_cstr(http_header_value(headers, octstr_imm("Host"))),
                octstr_get_cstr(uri));
            list_append(resph, redirect_header);
            status = HTTP_FOUND; /* will provide 302 */
            octstr_destroy(uri);
        } 
            
        if (verbose) {
            debug("test.http", 0, "request headers were");
            http_header_dump(headers);
            if (body != NULL) {
                debug("test.http", 0, "request body was");
                octstr_dump(body, 0);
            }
        }

        /* return response to client */
        http_send_reply(client, status, resph, reply_body);

        octstr_destroy(ip);
        octstr_destroy(url);
        octstr_destroy(body);
        octstr_destroy(reply_body);
        http_destroy_cgiargs(cgivars);
        list_destroy(headers, octstr_destroy_item);
        list_destroy(resph, octstr_destroy_item);
    }

    octstr_destroy(whitelist);
    octstr_destroy(blacklist);
    debug("test.http", 0, "client_thread terminates");
    http_close_all_ports();
}

static void help(void) {
    info(0, "Usage: test_http_server [-v loglevel][-l logfile][-f file][-h][-q]"
            "[-p port][-s][-c ssl_cert][-k ssl_key][-w white_list][b blacklist]\n");
}

static void sigterm(int signo) {
    run = 0;
    http_close_all_ports();
    debug("test.gwlib", 0, "Signal %d received, quitting.", signo);
}

int main(int argc, char **argv) {
    int i, opt, use_threads;
    struct sigaction act;
    char *filename;
    Octstr *log_filename;
    Octstr *file_contents;
#ifdef HAVE_LIBSSL
    Octstr *ssl_server_cert_file = NULL;
    Octstr *ssl_server_key_file = NULL;
#endif
    char *whitelist_name;
    char *blacklist_name;
    int white_asked,
        black_asked;
    long threads[MAX_THREADS];

    gwlib_init();

    act.sa_handler = sigterm;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGTERM, &act, NULL);

    port = 8080;
    use_threads = 1;
    verbose = 1;
    run = 1;
    filename = NULL;
    log_filename = NULL;
    blacklist_name = NULL;
    whitelist_name = NULL;
    white_asked = 0;
    black_asked = 0;

    while ((opt = getopt(argc, argv, "hqv:p:t:f:l:sc:k:b:w:")) != EOF) {
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
	    use_threads = atoi(optarg);
	    if (use_threads > MAX_THREADS)
            use_threads = MAX_THREADS;
	    break;

        case 'c':
#ifdef HAVE_LIBSSL
	    octstr_destroy(ssl_server_cert_file);
	    ssl_server_cert_file = octstr_create(optarg);
#endif
        break;

        case 'k':
#ifdef HAVE_LIBSSL
	    octstr_destroy(ssl_server_key_file);
	    ssl_server_key_file = octstr_create(optarg);
#endif
        break;

	case 's':
#ifdef HAVE_LIBSSL
        ssl = 1;
#endif   
        break;

	case 'f':
	    filename = optarg;
	    break;

	case 'l':
	    octstr_destroy(log_filename);
	    log_filename = octstr_create(optarg);
	break;

    case 'w':
        whitelist_name = optarg;
        if (whitelist_name == NULL)
            whitelist_name = "";
        white_asked = 1;
	break;

    case 'b':
        blacklist_name = optarg;
        if (blacklist_name == NULL)
            blacklist_name = "";
        black_asked = 1;
	break;

	case '?':
	default:
	    error(0, "Invalid option %c", opt);
	    help();
	    panic(0, "Stopping.");
	}
    }

    if (log_filename != NULL) {
    	log_open(octstr_get_cstr(log_filename), GW_DEBUG, GW_NON_EXCL);
	    octstr_destroy(log_filename);
    }

    if (filename == NULL)
    	file_contents = NULL;
    else
    	file_contents = octstr_read_file(filename);

    if (white_asked) {
        whitelist = octstr_read_file(whitelist_name);
        if (whitelist == NULL)
            panic(0, "Cannot read the whitelist");
    }
    
    if (black_asked) {
        blacklist = octstr_read_file(blacklist_name);
        if (blacklist == NULL)
            panic(0, "Cannot read the blacklist");
    }

#ifdef HAVE_LIBSSL
    /*
     * check if we are doing a SSL-enabled server version here
     * load the required cert and key file
     */
    if (ssl) {
        if (ssl_server_cert_file != NULL && ssl_server_key_file != NULL) {
            use_global_server_certkey_file(ssl_server_cert_file, ssl_server_key_file);
            octstr_destroy(ssl_server_cert_file);
            octstr_destroy(ssl_server_key_file);
        } else {
            panic(0, "certificate and public key need to be given!");
        }
    }
#endif
     
    if (http_open_port(port, ssl) == -1)
	panic(0, "http_open_server failed");

    /*
     * Do the real work in a separate thread so that the main
     * thread can catch signals safely.
     */
    for (i = 0; i < use_threads; ++i) 
        threads[i] = gwthread_create(client_thread, file_contents);
    for (i = 0; i < use_threads; ++i)
        gwthread_join(threads[i]);

    debug("test.http", 0, "Program exiting normally.");
    gwlib_shutdown();
    return 0;
}




