/*
 * Very simple push initiator for testing push proxy gateway
 *
 * Read pap control content and push content from files, pack them into a PAP
 * protocol MIME message and send this content to a specified url. Use a hard-
 * coded message boundary (asdlfkjiurwgasf), for simpler command line inter-
 * face.
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#define MAX_THREADS 1024
#define MAX_IN_QUEUE 128

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "gwlib/gwlib.h"
#include "gw/wap_push_pap_compiler.h"

static long max_pushes = 1;
static int verbose = 1,
           use_hardcoded = 0,
           num_urls = 0;
static Counter *counter = NULL;
static char **push_data = NULL;
static char *boundary = NULL;

/*
 * Add boundary value to the multipart header.
 */
static Octstr *make_multipart_value(const char *boundary)
{
    Octstr *hos;
    
    hos = octstr_format("%s", "multipart/related; boundary=");
    octstr_append(hos, octstr_imm(boundary));
    octstr_append(hos, octstr_imm("; type=\"application/xml\""));
    
    return hos;
}

static Octstr *make_part_delimiter(Octstr *boundary)
{
    Octstr *part_delimiter;

    part_delimiter = octstr_create("");
    octstr_format_append(part_delimiter, "%c", '\r');
    octstr_format_append(part_delimiter, "%c", '\n');
    octstr_format_append(part_delimiter, "%s", "--");
    octstr_append(part_delimiter, boundary);
    octstr_format_append(part_delimiter, "%c", '\r');
    octstr_format_append(part_delimiter, "%c", '\n');
    
    return part_delimiter;
}

static Octstr *make_close_delimiter(Octstr *boundary)
{
    Octstr *close_delimiter;

    close_delimiter = octstr_create("");
    octstr_format_append(close_delimiter, "%c", '\r');
    octstr_format_append(close_delimiter, "%c", '\n');
    octstr_format_append(close_delimiter, "%s", "--");
    octstr_append(close_delimiter, boundary);
    octstr_format_append(close_delimiter, "%s", "--");
    octstr_format_append(close_delimiter, "%c", '\r');
    octstr_format_append(close_delimiter, "%c", '\n');
    

    return close_delimiter;
}

static void start_push(HTTPCaller *caller, long i)   
{
    List *push_headers;
    Octstr *push_content,
           *wap_content,
           *wap_file_content,
           *pap_content,
           *pap_file_content,
           *cos,
           *bpos,
           *bcos,
           *mos,
           *push_url;
    long *id;
    char *content_file,
         *pap_file;

    push_headers  = http_create_empty_headers();
    http_header_add(push_headers, "X-WAP-Application-Id", 
                    "http://www.wiral.com:push.ua");
    push_content = NULL;
    if (use_hardcoded) {
        http_header_add(push_headers, "Content-Type", "multipart/related;" 
                    " boundary=asdlfkjiurwgasf; type=\"application/xml\"");
        push_content = octstr_create("\r\n\r\n"
                  "--asdlfkjiurwgasf\r\n"
                  "Content-Type: application/xml\r\n\r\n"
                  "<?xml version=\"1.0\"?>"
                  "<!DOCTYPE pap PUBLIC \"-//WAPFORUM//DTD PAP//EN\" "
                             "\"http://www.wapforum.org/DTD/pap_1.0.dtd\">"
                  "<pap>"
                        "<push-message push-id=\"9fjeo39jf084@pi.com\""
                          " deliver-before-timestamp=\"2000-06-28T06:45:00Z\""
                          " deliver-after-timestamp=\"2000-02-27T06:45:00Z\""
                          " progress-notes-requested=\"false\">"
			     "<address address-value=\"WAPPUSH=192.168.0.130/"
			 	"TYPE=IPv4@ppg.carrier.com\">"
                             "</address>"
                             "<quality-of-service"
                               " priority=\"low\""
                               " delivery-method=\"unconfirmed\""
                               " network-required=\"false\""
                               " bearer-required=\"false\">"
                             "</quality-of-service>"
                        "</push-message>"
                  "</pap>\r\n\r\n"         
                  "--asdlfkjiurwgasf\r\n"
                  "Content-Type: text/vnd.wap.wml\r\n\r\n"
                  "<?xml version=\"1.0\"?>"
                  "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\"" 
                  " \"http://www.wapforum.org/DTD/wml_1.1.xml\">"
                  "<wml>"
                       "<card id=\"main\" title=\"Hello, world\""
                                 " newcontext=\"true\">"
                            "<p>Hello, world.</p>"
                       "</card>"
                 "</wml>\r\n\r\n"
                 "--asdlfkjiurwgasf--\r\n\r\n"
                 "");
    } else {
        http_header_add(push_headers, "Content-Type", 
            octstr_get_cstr(mos = make_multipart_value(boundary)));
        octstr_destroy(mos);
        content_file = push_data[1];
        wap_content = octstr_format("%s", 
            "Content-Type: text/vnd.wap.wml\r\n");
        if ((wap_file_content = octstr_read_file(content_file)) == NULL)
	    panic(0, "Stopping");
        octstr_append(wap_content, wap_file_content);
        octstr_destroy(wap_file_content);

        pap_file = push_data[2];
        pap_content = octstr_format("%s", "Content-Type: application/xml\r\n");
        if ((pap_file_content = octstr_read_file(pap_file)) ==  NULL)
	    panic(0, "Stopping");
        octstr_append(pap_content, pap_file_content);
        octstr_destroy(pap_file_content);

        if (wap_content == NULL || pap_content == NULL)
	    panic(0, "Cannot open the push content files");

        push_content = octstr_create("");
        octstr_append(push_content, 
            bpos = make_part_delimiter(octstr_imm(boundary)));
        octstr_append(push_content, pap_content);
        octstr_append(push_content, bpos);
        octstr_destroy(bpos);
        octstr_append(push_content, wap_content);
        octstr_append(push_content, 
            bcos = make_close_delimiter(octstr_imm(boundary)));
        octstr_destroy(bcos);
        octstr_destroy(pap_content);
        octstr_destroy(wap_content);
    }
    
    http_header_add(push_headers, "Content-Length: ", 
                    octstr_get_cstr(cos = octstr_format("%d", 
                    octstr_len(push_content))));
    octstr_destroy(cos);
    debug("test.ppg", 0, "we have push content");
    octstr_dump(push_content, 0);

    id = gw_malloc(sizeof(long));
    *id = i;
    push_url = octstr_create(push_data[0]);
    http_start_request(caller, push_url, push_headers, push_content, 0, id,
                       NULL);
    debug("test.ppg", 0, "TEST_PPG: started pushing job %ld", i);

    octstr_destroy(push_url);
    octstr_destroy(push_content);
    http_destroy_headers(push_headers);
}

static int receive_push_reply(HTTPCaller *caller)
{
    void *id;
    int ret;
    List *reply_headers;
    Octstr *final_url,
           *reply_body,
           *os;
    WAPEvent *e;
    
    id = http_receive_result(caller, &ret, &final_url, &reply_headers, 
                             &reply_body);

    if (id == NULL || ret == -1 || final_url == NULL) {
        error(0, "push failed");
        goto push_failed;
    }
        
    debug("test.ppg", 0, "TEST_PPG: push %ld done: reply from,  %s", 
          *(long *) id, octstr_get_cstr(final_url));
    gw_free(id);
    octstr_destroy(final_url);
    if (verbose)
        debug("test.ppg", 0, "TEST_PPG: reply headers were");
    while ((os = list_extract_first(reply_headers)) != NULL) {
        if (verbose)
            octstr_dump(os, 1); 
        octstr_destroy(os);
    }

    if (verbose) {
        debug("test.ppg", 0, "TEST_PPG: reply body was");
        octstr_dump(reply_body, 0);
    }

    e = NULL;
    if ((ret = pap_compile(reply_body, &e)) < 0) {
        warning(0, "TEST_PPG: receive_push_reply: cannot compile pap message");
        goto parse_error;
    }

    switch (e->type) {
        case Push_Response:
	    debug("test.ppg", 0, "TEST_PPG: and type push response");
	break;

        case Bad_Message_Response:
	    debug("test.ppg", 0, "TEST_PPG: and type bad message response");
        break;

        default:
            warning(0, "TEST_PPG: unknown event received from %s", 
                    octstr_get_cstr(final_url));
        break;
    }

    octstr_destroy(reply_body);
    wap_event_destroy(e);
    http_destroy_headers(reply_headers);

    return 0;

push_failed:
    octstr_destroy(final_url);
    octstr_destroy(reply_body);
    http_destroy_headers(reply_headers);
    
    return -1;

parse_error:
    octstr_destroy(final_url);
    octstr_destroy(reply_body);
    http_destroy_headers(reply_headers);
    wap_event_destroy(e);
    
    return -1;
}

static void push_thread(void *arg)
{
    HTTPCaller *caller;
    long succeeded, failed, in_queue, i;

    caller = arg;
    succeeded = 0;
    failed = 0;   
    in_queue = 0;
    i = 0;

    for (;;) {
        while (in_queue < MAX_IN_QUEUE) {
	    i = counter_increase(counter);
            if (i >= max_pushes)
	        goto receive_rest;
        start_push(caller, i);
#if 0
        gwthread_sleep(0.1);
#endif
        ++in_queue;
        }

        while (in_queue >= MAX_IN_QUEUE) {
	    if (receive_push_reply(caller) == -1)
	        ++failed;
            else
	        ++succeeded;
            --in_queue;
        }
    }

receive_rest:
    while (in_queue > 0) {
        if (receive_push_reply(caller) == -1)
	    ++failed;
        else
	    ++succeeded;
        --in_queue;
    }

    http_caller_destroy(caller);
    info(0, "TEST_PPG: In thread %ld %ld succeeded, %ld failed", 
         (long) gwthread_self(), succeeded, failed);
}

static void help(void) 
{
    info(0, "Usage: test_ppg [options] push_url [content_file pap_file]");
    info(0, "push content_file using control file pap_file to push_url");
    info(0, "where options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-q");
    info(0, "    don't print the body nor headers of the HTTP response");
    info(0, "-r number");
    info(0, "    make `number' requests");
    info(0, "-H");
    info(0, "Use hardcoded MIME message, containing a pap control document");
    info(0, "-t");
    info(0, "number of threads, maximum 1024");
}

int main(int argc, char **argv)
{
    int opt,
        num_threads;
    time_t start,
           end;
    double run_time;
    long threads[MAX_THREADS];
    long i;

    gwlib_init();
    num_threads = 1;

    while ((opt = getopt(argc, argv, "Hhv:qr:t:")) != EOF) {
        switch(opt) {
	    case 'v':
	        log_set_output_level(atoi(optarg));
	    break;

	    case 'q': 
	        verbose = 0;
	    break;  

	    case 'r':
	        max_pushes = atoi(optarg);      
	    break;  

            case 't': 
	        num_threads = atoi(optarg);
                if (num_threads > MAX_THREADS)
		    num_threads = MAX_THREADS;
	    break;

	    case 'H':
	        use_hardcoded = 1;
	    break;

	    case 'h':
	        help();
                exit(0);

	    case '?':
	    default:
	        error(0, "TEST_PPG: Invalid option %c", opt);
                help();
                panic(0, "Stopping");
        }
    }

    if (optind == argc) {
        help();
        exit(0);
    }
    
    push_data = argv + optind;
    num_urls = argc - optind;

    if (push_data[0] == NULL)
        panic(0, "No ppg address specified, stopping");

    if (!use_hardcoded) {
        if (push_data[1] == 0)
            panic(0, "No push content file, stopping");
        if (push_data[2] == 0)
            panic(0, "No pap control message, stopping");
    }

    boundary = "asdlfkjiurwghasf";
    counter = counter_create();
    time(&start);
    if (num_threads == 0)
        push_thread(http_caller_create());
    else {
        for (i = 0; i < num_threads; ++i)
	    threads[i] = gwthread_create(push_thread, http_caller_create());
	for (i = 0; i < num_threads; ++i)
	    gwthread_join(threads[i]);
    }
    time(&end);
    run_time = difftime(end, start);
    info(0, "TEST_PPG: %ld requests in %f seconds, %f requests per second",
         max_pushes, run_time, max_pushes / run_time);

    counter_destroy(counter);
    gwlib_shutdown();

    return 0;
}








