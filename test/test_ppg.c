/*
 * A very simple push initiator for testing a push proxy gateway
 *
 * Read pap control content and push content from files, pack them into a PAP
 * protocol MIME message and invoke push services specified by an url. Use a 
 * hardcoded message boundary (asdlfkjiurwgasf), for simpler command line 
 * interface.
 * Repetitions and use of multiple threads can be requested, in addition of 
 * setting of some headers. 
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
           num_urls = 0,
           wait = 0,
           use_headers = 0,
           use_config = 0;
static double wait_seconds = 0.0;
static Counter *counter = NULL;
static char **push_data = NULL;
static char *boundary = NULL;
static Octstr *content_flag = NULL;
static Octstr *appid_flag = NULL;
static Octstr *content_transfer_encoding = NULL;

enum { SSL_CONNECTION_OFF = 0,
       DEFAULT_NUMBER_OF_RELOGS = 2};

/*
 * Configuration variables
 */
static int pi_ssl = SSL_CONNECTION_OFF;
static long retries = DEFAULT_NUMBER_OF_RELOGS;
static Octstr *ssl_client_certkey_file = NULL;
static Octstr *push_url = NULL;
static Octstr *pap_file = NULL;
static Octstr *content_file = NULL;
static Octstr *username = NULL;
static Octstr *password = NULL;

static void read_test_ppg_config(Octstr *name)
{
    Cfg *cfg;
    CfgGroup *grp;

    cfg = cfg_create(name);
    if (cfg_read(cfg) == -1)
        panic(0, "Cannot read a configuration file %s, exiting",
              octstr_get_cstr(name));
    cfg_dump(cfg);
    grp = cfg_get_single_group(cfg, octstr_imm("test-ppg"));
    cfg_get_integer(&retries, grp, octstr_imm("retries"));
    cfg_get_bool(&pi_ssl, grp, octstr_imm("pi-ssl"));
#ifdef HAVE_LIBSSL    
    if (pi_ssl) {
        ssl_client_certkey_file = cfg_get(grp, 
            octstr_imm("ssl-client-certkey-file"));
        if (ssl_client_certkey_file != NULL) {
            use_global_client_certkey_file(ssl_client_certkey_file);
        } else { 
             error(0, "cannot set up SSL without client certkey file");
             exit(1);
        }
    }
#endif

    grp = cfg_get_single_group(cfg, octstr_imm("configuration"));
    push_url = cfg_get(grp, octstr_imm("push-url"));
    pap_file =  cfg_get(grp, octstr_imm("pap-file"));
    content_file =  cfg_get(grp, octstr_imm("content-file"));
    if (!use_hardcoded) {
        username = cfg_get(grp, octstr_imm("username"));
        password = cfg_get(grp, octstr_imm("password"));
    }

    cfg_destroy(cfg);
}

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
    else if (octstr_compare(content_flag, octstr_imm("sl")) == 0)
	*wap_content = octstr_format("%s",
            "Content-Type: text/vnd.wap.sl\r\n");
    else if (octstr_compare(content_flag, octstr_imm("multipart")) == 0)
        *wap_content = octstr_format("%s",
            "Content-Type: multipart/related; boundary=fsahgwruijkfldsa\r\n");
    else if (octstr_compare(content_flag, octstr_imm("sia")) == 0)
	*wap_content = octstr_format("%s",
            "Content-Type: application/vnd.wap.sia\r\n");
    else if (octstr_compare(content_flag, octstr_imm("scrap")) == 0)
        *wap_content = octstr_format("%s", "no type at all\r\n"); 
    else if (octstr_compare(content_flag, octstr_imm("nil")) == 0)
        *wap_content = octstr_create("");
}

static void add_content_transfer_encoding_type(Octstr *content_flag, 
                                               Octstr *wap_content)
{
    if (!content_flag)
	return;

    if (octstr_compare(content_flag, octstr_imm("base64")) == 0)
	octstr_append_cstr(wap_content, "Content-transfer-encoding: base64\r\n");
}

static void transfer_encode (Octstr *cte, Octstr *content)
{
    if (!cte)
	return;
    
    if (octstr_compare(cte, octstr_imm("base64")) == 0) {
	octstr_binary_to_base64(content);
    }
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

static List *push_headers_create(size_t content_len)
{
    List *push_headers;
    Octstr *cos,
           *mos;

    mos = NULL;
    push_headers  = http_create_empty_headers();
    if (use_hardcoded)
        http_header_add(push_headers, "Content-Type", "multipart/related;" 
                        " boundary=asdlfkjiurwgasf; type=\"application/xml\"");
    else
        http_header_add(push_headers, "Content-Type", 
                        octstr_get_cstr(mos = make_multipart_value(boundary)));
    if (use_headers)
        http_add_basic_auth(push_headers, username, password);
    add_push_application_id(&push_headers, appid_flag);
    http_header_add(push_headers, "Content-Length: ", 
                    octstr_get_cstr(cos = octstr_format("%d", content_len)));
    octstr_destroy(cos);
    octstr_destroy(mos);

    return push_headers;
}

static Octstr *push_content_create(void)
{
    Octstr *push_content, 
           *wap_content;
    Octstr *wap_file_content,
           *pap_content,
           *pap_file_content,
           *bpos,
           *bcos;

    wap_content = NULL;
    push_content = NULL;
    if (use_hardcoded) {
        push_content = octstr_create("\r\n\r\n"
                  "--asdlfkjiurwgasf\r\n"
                  "Content-Type: application/xml\r\n\r\n"
                  "<?xml version=\"1.0\"?>"
                  "<!DOCTYPE pap PUBLIC \"-//WAPFORUM//DTD PAP//EN\""
                             " \"http://www.wapforum.org/DTD/pap_1.0.dtd\">"
                  "<pap>"
                        "<push-message push-id=\"9fjeo39jf084@pi.com\""
                          " deliver-before-timestamp=\"2002-11-01T06:45:00Z\""
                          " deliver-after-timestamp=\"2000-02-27T06:45:00Z\""
                          " progress-notes-requested=\"false\">"
			     "<address address-value=\"WAPPUSH=+358408676001/"
			 	"TYPE=PLMN@ppg.carrier.com\">"
                             "</address>"
                             "<quality-of-service"
                               " priority=\"low\""
                               " delivery-method=\"unconfirmed\""
                               " network-required=\"true\""
                               " network=\"GSM\""
                               " bearer-required=\"true\""
                               " bearer=\"SMS\">"
                             "</quality-of-service>"
                        "</push-message>"
                  "</pap>\r\n\r\n"         
                  "--asdlfkjiurwgasf\r\n"
                  "Content-Type: text/vnd.wap.si\r\n\r\n"
                  "<?xml version=\"1.0\"?>"
                  "<!DOCTYPE si PUBLIC \"-//WAPFORUM//DTD SI 1.0//EN\" "
                    " \"http://www.wapforum.org/DTD/si.dtd\">"
                  "<si>"
                      "<indication href=\"http://wap.iobox.fi\""
                          " si-id=\"1@wiral.com\""
                          " action=\"signal-high\""
                          " created=\"1999-06-25T15:23:15Z\""
                          " si-expires=\"2002-06-30T00:00:00Z\">"
                          "Want to test a fetch?"
                      "</indication>"
                   "</si>\r\n\r\n"
                 "--asdlfkjiurwgasf--\r\n\r\n"
                 "");
    } else {
        add_content_type(content_flag, &wap_content);
        add_content_transfer_encoding_type(content_transfer_encoding, 
                                           wap_content);
        if ((wap_file_content = 
                octstr_read_file(octstr_get_cstr(content_file))) == NULL)
	    panic(0, "Stopping");

	transfer_encode (content_transfer_encoding, wap_file_content);
        octstr_append(wap_content, wap_file_content);
        octstr_destroy(wap_file_content);

        pap_content = octstr_format("%s", "Content-Type: application/xml\r\n");
        if ((pap_file_content = 
                octstr_read_file(octstr_get_cstr(pap_file))) ==  NULL)
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

    return push_content;
}

static void make_url(Octstr **url)
{
    if (use_config && !use_headers) {
        octstr_append(*url, octstr_imm("?username="));
        octstr_append(*url, username ? username : octstr_imm("default"));
        octstr_append(*url, octstr_imm("&password="));
        octstr_append(*url, password ? password: octstr_imm("default"));
    }
}

static void start_push(HTTPCaller *caller, long i)   
{
    List *push_headers;
    Octstr *push_content;
    long *id;
    
    push_content = push_content_create();
    push_headers = push_headers_create(octstr_len(push_content));
    if (verbose) {
       debug("test.ppg", 0, "we have push content");
       octstr_dump(push_content, 0);
       debug("test.ppg", 0, "and headers");
       http_header_dump(push_headers);
    }

    id = gw_malloc(sizeof(long));
    *id = i;
    make_url(&push_url);
    http_start_request(caller, push_url, push_headers, push_content, 0, id,
                       ssl_client_certkey_file);
    debug("test.ppg", 0, "TEST_PPG: started pushing job %ld", i);

    octstr_destroy(push_url);
    octstr_destroy(push_content);
    http_destroy_headers(push_headers);
}

/*
 * Try log in defined number of times, when got response 401 and authentica-
 * tion info is in headers.
 */
static int receive_push_reply(HTTPCaller *caller)
{
    void *id;
    long *trid;
    int http_status,
        tries;
    List *reply_headers;
    Octstr *final_url,
           *auth_url,
           *reply_body,
           *os,
           *push_content,
           *auth_reply_body;
    WAPEvent *e;
    List *retry_headers;
    
    http_status = HTTP_UNAUTHORIZED;
    tries = 0;

    id = http_receive_result(caller, &http_status, &final_url, &reply_headers,
                             &reply_body);

    if (id == NULL || http_status == -1 || final_url == NULL) {
        error(0, "push failed, no reason found");
        goto push_failed;
    }

    while (use_headers && http_status == HTTP_UNAUTHORIZED && tries < retries) {
        debug("test.ppg", 0, "try number %d", tries);
        debug("test.ppg", 0, "authentication failure, get a challenge");
        http_destroy_headers(reply_headers);
        push_content = push_content_create();
        retry_headers = push_headers_create(octstr_len(push_content));
        http_add_basic_auth(retry_headers, username, password);
        trid = gw_malloc(sizeof(long));
        *trid = tries;
        http_start_request(caller, final_url, retry_headers, 
                           push_content, 0, trid, NULL);
        debug("test.ppg ", 0, "TEST_PPG: doing response to %s", 
              octstr_get_cstr(final_url));

        octstr_destroy(push_content);
        http_destroy_headers(retry_headers);
        
        trid = http_receive_result(caller, &http_status, &auth_url, 
                                   &reply_headers, &auth_reply_body);

        if (trid == NULL || http_status == -1 || auth_url == NULL) {
            error(0, "unable to send authorisation, no reason found");
            goto push_failed;
        }   

        debug("test.ppg", 0, "TEST_PPG: send authentication to %s, retry %ld", 
               octstr_get_cstr(auth_url), *(long *) trid);
        gw_free(trid);
        octstr_destroy(auth_reply_body);
        octstr_destroy(auth_url);
        ++tries;
    }

    if (http_status == HTTP_NOT_FOUND) {
        error(0, "push failed, service not found");
        goto push_failed;
    }

    if (http_status == HTTP_FORBIDDEN) {
        error(0, "push failed, service forbidden");
        goto push_failed;
    }

    if (http_status == HTTP_UNAUTHORIZED) {
        if (use_headers)
            error(0, "tried %ld times, stopping", retries);
        else
	    error(0, "push failed, authorisation failure");
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
            octstr_dump(os, 0); 
        octstr_destroy(os);
    }

    if (verbose) {
        debug("test.ppg", 0, "TEST_PPG: reply body was");
        octstr_dump(reply_body, 0);
    }

    e = NULL;
    if (pap_compile(reply_body, &e) < 0) {
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
    gw_free(id);
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
        if (wait)
            gwthread_sleep(wait_seconds);
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
    info(0, "      or");
    info(0, "Usage: test_ppg [options] [conf_file]");
    info(0, "Implements push initiator for wap push. Push services are ");
    info(0, "located in push_url, push content in the file content file.");
    info(0, "File pap_file contains pap control document that controls");
    info(0, "pushing");
    info(0, "If option -H is not used, command line has either three or one");
    info(0, "arguments:");
    info(0, "      a) the url of the push proxy gateway");
    info(0, "      b) a file containing the content to be pushed");
    info(0, "      c) a pap document controlling pushing");
    info(0, "     or");
    info(0, "      a) a test configuration file, containing all these");
    info(0, "Options are:");
    info(0, "-h");
    info(0, "print this info");
    info(0, "-c content qualifier");
    info(0, "Define content type of the push content. Wml, multipart, nil,"); 
    info(0, "scrap, sl, sia and si accepted. Si is default, nil (no content"); 
    info(0, " type at all) and scrap (random string) are used for debugging");
    info(0, "-a application id");
    info(0, "Define the client application that will handle the push. Any,"); 
    info(0, "sia, ua, mms, nil and scrap accepted, default any.");
    info(0, "-b");
    info(0, "If true, send username/password in headers. Default false");
    info(0, "-v number");
    info(0, "    Set log level for stderr logging. Default 0 (debug)");
    info(0, "-q");
    info(0, "    Do not print debugging information");
    info(0, "Default: print it");
    info(0, "-r number");
    info(0, "    Make `number' requests. Default one request");
    info(0, "-i seconds");
    info(0, "    Wait 'seconds' seconds between pushes. Default: do not wait");
    info(0, "-e transfer encoding");
    info(0, "    use transfer encoding to send push contents.");
    info(0, "    Currently supported is base64.");
    info(0, "-H");
    info(0, "Use hardcoded MIME message, containing a pap control document.");
    info(0, "In addition, use hardcoded username/password in headers (if ");
    info(0, "flag -b is set, too");
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
    Octstr *fos;

    gwlib_init();
    num_threads = 1;

    while ((opt = getopt(argc, argv, "Hhbv:qr:t:c:a:i:e")) != EOF) {
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
            
	    case 'i': 
	        wait = 1;
                wait_seconds = atof(optarg);
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
                    octstr_compare(content_flag, octstr_imm("sia")) != 0 &&
                    octstr_compare(content_flag, octstr_imm("sl")) != 0 &&
                    octstr_compare(content_flag, octstr_imm("nil")) != 0 &&
                    octstr_compare(content_flag, octstr_imm("scrap")) != 0 &&
                    octstr_compare(content_flag, 
                        octstr_imm("multipart")) != 0){
		    octstr_destroy(content_flag);
		    error(0, "TEST_PPG: Content type not known");
		    help();
                    exit(1);
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
                    exit(1);
                }
	    break;

	    case 'e':
		content_transfer_encoding = octstr_create(optarg);
                if (octstr_compare(content_transfer_encoding, 
                                   octstr_imm("base64")) != 0) {
		    octstr_destroy(content_transfer_encoding);
		    error(0, "TEST_PPG: unknown content transfer" 
                          " encoding \"%s\"",
			  octstr_get_cstr(content_transfer_encoding));
		    help();
                    exit(1);
		}
	    break;

	    case 'h':
	        help();
                exit(1);

	    case 'b':
	        use_headers = 1;
	    break;

	    case '?':
	    default:
	        error(0, "TEST_PPG: Invalid option %c", opt);
                help();
                error(0, "Stopping");
                exit(1);
        }
    }

    if (optind == argc) {
        help();
        exit(1);
    }
    
    push_data = argv + optind;
    num_urls = argc - optind;

    if (content_flag == NULL)
        content_flag = octstr_imm("si");

    if (appid_flag == NULL)
        appid_flag = octstr_imm("any");

    if (use_hardcoded) {
        username = octstr_imm("troo");
        password = octstr_imm("far");
    }

    if (push_data[0] == NULL) {
        error(0, "No ppg address or config file, stopping");
        exit(1);
    }

    use_config = 0;
    if (!use_hardcoded) {
        if (push_data[1] == NULL) {
            info(0, "a configuration file input assumed");
            use_config = 1;
        }
    }

    if (!use_config && push_data[1] != NULL) {
        if (push_data[2] == NULL) {
	   error(0, "no pap control document, stopping");
           exit(1);
        } else {
           info(0, "an input without a configuration file assumed");
           push_url = octstr_create(push_data[0]);
           content_file = octstr_create(push_data[1]);
           pap_file = octstr_create(push_data[2]);
        }
    }

    debug("test.ppg", 0, "using %s as a content file", push_data[1]);
    debug("test.ppg", 0, "using %s as a control file", push_data[2]);

    boundary = "asdlfkjiurwghasf";
    counter = counter_create();
    if (use_config) {
        read_test_ppg_config(fos = octstr_format("%s", push_data[0]));
        octstr_destroy(fos);
    }

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
    octstr_destroy(content_file);
    octstr_destroy(pap_file);
    octstr_destroy(ssl_client_certkey_file);
    octstr_destroy(username);
    octstr_destroy(password);
    counter_destroy(counter);
    gwlib_shutdown();

    exit(0);
}









