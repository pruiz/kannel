/*
 * Very simple push initiator for testing push proxy gateway
 *
 * Read pap control content and push content from files, pack them into a PAP
 * protocol MIME message and invoke push services specified by an url. Use a 
 * hardcoded message boundary (asdlfkjiurwgasf), for simpler command line 
 * interface.
 * Repetitions, using multiple threads can be requested, in addition of sett-
 * ing of some MIME headers. 
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
static Octstr *content_flag = NULL;
static Octstr *appid_flag = NULL;

static void add_push_application_id(List **push_headers, Octstr *appid_flag)
{
    if (octstr_compare(appid_flag, octstr_imm("any")) == 0)
        http_header_add(*push_headers, "X-WAP-Application-Id", 
                        "http://www.wiral.com:*");
    else if (octstr_compare(appid_flag, octstr_imm("sia")) == 0)
        http_header_add(*push_headers, "X-WAP-Application-Id", 
                        "http://www.wiral.com:push.sia");
    else if (octstr_compare(appid_flag, octstr_imm("ua")) == 0)
        http_header_add(*push_headers, "X-WAP-Application-Id", 
                        "http://www.wiral.com:wml.ua");
    else if (octstr_compare(appid_flag, octstr_imm("mms")) == 0)
        http_header_add(*push_headers, "X-WAP-Application-Id", 
                        "http://www.wiral.com:push.mms");
    else if (octstr_compare(appid_flag, octstr_imm("scrap")) == 0)
        http_header_add(*push_headers, "X-WAP-Application-Id", 
                        "no appid at all");
}

static void add_content_type(Octstr *content_flag, Octstr **wap_content)
{
    if (octstr_compare(content_flag, octstr_imm("wml")) == 0)
        *wap_content = octstr_format("%s", 
            "Content-Type: text/vnd.wap.wml\r\n");
    else if (octstr_compare(content_flag, octstr_imm("si")) == 0)
	*wap_content = octstr_format("%s",
            "Content-Type: text/vnd.wap.si\r\n");
    else if (octstr_compare(content_flag, octstr_imm("multipart")) == 0)
        *wap_content = octstr_format("%s",
            "Content-Type: multipart/related; boundary=fsahgwruijkfldsa\r\n");
    else if (octstr_compare(content_flag, octstr_imm("scrap")) == 0)
        *wap_content = octstr_format("%s", "no type at all\r\n"); 
    else if (octstr_compare(content_flag, octstr_imm("nil")) == 0)
        *wap_content = octstr_create("");
}

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

    wap_content = NULL;
    push_headers  = http_create_empty_headers();
    
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
        add_content_type(content_flag, &wap_content);
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
    add_push_application_id(&push_headers, appid_flag);
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
    info(0, "Implements push initiator for wap push. Push services are ");
    info(0, "located in push_url, push content in the file content file.");
    info(0, "File pap_file contains pap control document that controls");
    info(0, "pushing");
    info(0, "If option -H is not used, command line has three arguments.");
    info(0, "These are following, in this order:");
    info(0, "      a) the url of the push proxy gateway");
    info(0, "      b) the file containing the content to be pushed");
    info(0, "      c) pap document controlling the pushing");
    info(0, "Options are:");
    info(0, "-h");
    info(0, "print this info");
    info(0, "-c content qualifier");
    info(0, "Define content type of the push content. Wml, multipart, nil,"); 
    info(0, "scrap and si accepted. Si is default, nil (no content type at");
    info(0, " all) and scrap (random string) are used for debugging");
    info(0, "-a application id");
    info(0, "Define the client application that will handle the push. Any,"); 
    info(0, "sia, ua, mms, nil and scrap accepted, default any.");
    info(0, "-v number");
    info(0, "    Set log level for stderr logging. Default 0 (debug)");
    info(0, "-q");
    info(0, "    Do not print debugging information");
    info(0, "Default: print it");
    info(0, "-r number");
    info(0, "    Make `number' requests. Default one request");
    info(0, "-H");
    info(0, "Use hardcoded MIME message, containing a pap control document");
    info(0, "Default: read components from files");
    info(0, "-t");
    info(0, "number of threads, maximum 1024, default 1");
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

    while ((opt = getopt(argc, argv, "Hhv:qr:t:c:a:")) != EOF) {
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

	    case 'c':
	        content_flag = octstr_create(optarg);
                if (octstr_compare(content_flag, octstr_imm("wml")) != 0 && 
                    octstr_compare(content_flag, octstr_imm("si")) != 0 &&
                    octstr_compare(content_flag, octstr_imm("nil")) != 0 &&
                    octstr_compare(content_flag, octstr_imm("scrap")) != 0 &&
                    octstr_compare(content_flag, 
                        octstr_imm("multipart")) != 0){
		    octstr_destroy(content_flag);
		    error(0, "TEST_PPG: Content type not known");
		    help();
                    exit(0);
                }
	    break;

	    case 'a':
	        appid_flag = octstr_create(optarg);
                if (octstr_compare(appid_flag, octstr_imm("any")) != 0 && 
                    octstr_compare(appid_flag, octstr_imm("sia")) != 0 &&
                    octstr_compare(appid_flag, octstr_imm("ua")) != 0 &&
                    octstr_compare(appid_flag, octstr_imm("mms")) != 0 &&
                    octstr_compare(appid_flag, octstr_imm("nil")) != 0 &&
                    octstr_compare(appid_flag, octstr_imm("scrap")) != 0) {
		    octstr_destroy(appid_flag);
		    error(0, "TEST_PPG: Push application id not known");
		    help();
                    exit(0);
                }
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

    if (content_flag == NULL)
        content_flag = octstr_imm("si");

    if (appid_flag == NULL)
        appid_flag = octstr_imm("any");

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

    octstr_destroy(content_flag);
    octstr_destroy(appid_flag);
    counter_destroy(counter);
    gwlib_shutdown();

    return 1;
}








