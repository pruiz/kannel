/*
 * wap_push_pap.c: Implementation of Push Proxy Gateway interface to PI.
 *
 * By Aarno Syvänen for Wiral Ltd and for Wapit Ltd.
 */

#include <unistd.h>

#include "gwlib/gwlib.h"
#include "wap_push_pap.h"
#include "wap_push_ppg.h"
#include "wap_push_pap_compiler.h"
#include "wap_push_pap_mime.h"

/*****************************************************************************
 *
 * Internal data structures
 */

/*
 * Give the status of the push pap module:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum {limbo, running, terminating} run_status = limbo;

/*
 * The event queue for this module
 */
static List *pap_queue = NULL;

wap_dispatch_func_t *dispatch_to_ppg;

/*****************************************************************************
 *
 * Prototypes of internal functions
 *
 * Event handling
 */
static void main_thread(void *arg);
static void http_read_thread(void *arg);
static void handle_pap_event(WAPEvent *e);

/*
 * Header functions
 */
static int headers_acceptable(List *push_headers, Octstr **content_header);
static int type_is(Octstr *content_header, char *required_type);
static int get_mime_boundary(List *push_headers, Octstr *content_header, 
                             Octstr **boundary);
static void change_header_value(List **push_headers, char *name, char *value);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 */

void wap_push_pap_init(wap_dispatch_func_t *ppg_dispatch)
{
     pap_queue = list_create();
     list_add_producer(pap_queue);

     dispatch_to_ppg = ppg_dispatch;
     gw_assert(run_status == limbo);
     run_status = running;
     gwthread_create(http_read_thread, NULL);
     gwthread_create(main_thread, NULL);
}

void wap_push_pap_shutdown(void)
{
     gw_assert(run_status == running);
     run_status = terminating;
     list_remove_producer(pap_queue);

     gwthread_join_every(http_read_thread);
     gwthread_join_every(main_thread);
     list_destroy(pap_queue, wap_event_destroy_item);
}

void wap_push_pap_dispatch_event(WAPEvent *e) 
{
     gw_assert(run_status == running);
     list_produce(pap_queue, e);
}

/*****************************************************************************
 *
 * INTERNAL FUNCTIONS
 */

static void main_thread(void *arg)
{
    WAPEvent *e;

    while (run_status == running && (e = list_consume(pap_queue)) != NULL) {
        handle_pap_event(e);
    } 
}

static void http_read_thread(void *arg)
{
    WAPEvent *ppg_event;
    long i,
         push_len;
    Octstr *pap_content,
           *push_data,
           *rdf_content,
           *mime_content,
           *cos,
           *plos,
           *boundary,
           *content_header;            /* Content-Type MIME header */
    int compiler_status;
    List *push_headers,                /* MIME headers themselves */
         *content_headers;             /* Headers from the content entity, see
                                          PAP chapters 8.2, 13.1. Rfc 2045 
                                          grammar calls these MIME-part-hea-
                                          ders */
   
    sleep(10);
    push_headers  = http_create_empty_headers();
    content_headers = http_create_empty_headers();
    http_header_add(push_headers, "Content-Type", "multipart/related;" 
                    " boundary=asdlfkjiurwgasf; type=\"application/xml\"");
    http_header_add(push_headers, "X-WAP-Application-Id", 
                    "http://wap.wapit.com:push.ua");
    mime_content = octstr_create("\r\n\r\n"
                  "--asdlfkjiurwgasf\r\n"
                  "Content-Type: application/xml\r\n\r\n"
                  "<?xml version=\"1.0\"?>"
                  "<!DOCTYPE pap PUBLIC \"-//WAPFORUM//DTD PAP//EN\" "
                             "\"http://www.wapforum.org/DTD/pap_1.0.dtd\">"
                  "<pap>"
                        "<push-message push-id=\"9fjeo39jf084@pi.com\""
                          " deliver-before-timestamp=\"2000-08-07T06:45:00Z\""
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

    http_header_add(push_headers, "Content-Length: ", 
                    octstr_get_cstr(cos = octstr_format("%d", 
                    octstr_len(mime_content))));
    octstr_destroy(cos);

    boundary = octstr_create("");
    i = 0;
    while (i < 0) {
        if (!headers_acceptable(push_headers, &content_header)) {
	    goto error;

        }
	if (get_mime_boundary(push_headers, content_header, &boundary) == -1) {
	    goto error;
        }

        pap_content = octstr_create("");
        push_data = octstr_create("");
        rdf_content = octstr_create("");

        if (!mime_parse(boundary, mime_content, &pap_content, &push_data, 
                        &content_headers, &rdf_content)) {
	    goto clean;
        } else {
	    debug("wap.push.pap", 0, "PAP: http_read_thread: pap multipart"
                  " accepted");
        }

        push_len = octstr_len(push_data); 
        http_header_remove_all(push_headers, "Content-Type");
	http_append_headers(push_headers, content_headers);
        change_header_value(&push_headers, "Content-Length", 
            octstr_get_cstr(plos = octstr_format("%d", push_len)));

        octstr_destroy(plos);
        octstr_destroy(content_header);
	http_destroy_headers(content_headers);
        
        ppg_event = NULL;
        if ((compiler_status = pap_compile(pap_content, &ppg_event)) == -2) {
            warning(0, "PAP: http_read_thread: pap control entity erroneous");
        } else if (compiler_status == -1) {
            warning(0, "PAP: http_read_thread: non-implemented pap feature"
                    " requested");
        } else {
            debug("wap.push.pap", 0, "PAP: http_read_thread: pap control"
                  " entity compiled ok, sending to ppg");
            ppg_event->u.Push_Message.push_headers = 
                http_header_duplicate(push_headers);
            ppg_event->u.Push_Message.push_data = octstr_duplicate(push_data);
            dispatch_to_ppg(ppg_event);
        }
/*
 * An intentional fall-through. We will eventually use a memory cleaning func-
 * tion.
 */
clean:
        http_destroy_headers(push_headers);
        octstr_destroy(mime_content);
        octstr_destroy(pap_content);
        octstr_destroy(push_data);
        octstr_destroy(rdf_content);
        octstr_destroy(boundary);
        sleep(1);
        ++i;
        continue;

error:
        http_destroy_headers(push_headers);
        http_destroy_headers(content_headers);
        octstr_destroy(mime_content);
        octstr_destroy(boundary);
        octstr_destroy(content_header);
        sleep(1);
        ++i;
        continue;
    }

    http_destroy_headers(push_headers);
    http_destroy_headers(content_headers);
    octstr_destroy(mime_content);
    octstr_destroy(push_data);
    octstr_destroy(boundary);
}

static void handle_pap_event(WAPEvent *e)
{
    switch (e->type) {
    case Push_Response:
        debug("wap.push.pap", 0, "PAP: handle_pap_event: we have a push"
              " response");
        wap_event_destroy(e);
    break;

    default:
        error(0, "PAP: handle_pap_event: we have an unknown event");
        wap_event_dump(e);
	wap_event_destroy(e);
    break;
    }
}

/*
 * Pi uses multipart/related content type when communicating with ppg. (see 
 * PAP, Chapter 8) and subtype application/xml.
 * Check if push headers are acceptable according this rule. In addition, 
 * return the field value of Content-Type header.
 */
static int headers_acceptable(List *push_headers, Octstr **content_header)
{
    *content_header = http_header_find_first(push_headers, "Content-Type");
    
    if (!type_is(*content_header, "multipart/related")) {
        goto error;
    }

    if (!type_is(*content_header, "application/xml")) {
        goto error;
    }

    return 1;

error:
    warning(0, "got unacceptable push headers");
    return 0;
}

/*
 * Content-Type header field is defined in rfc 1521, chapter 4. We are looking
 * after type multipart/related or "multipart/related" and parameter 
 * type=application/xml or type="application/xml", as required by PAP, chapter
 * 8.
 */
static int type_is(Octstr *content_header, char *name)
{
    Octstr *quoted_type,
           *osname;

    osname = octstr_imm(name);
    if (octstr_case_search(content_header, osname, 0) >= 0)
        return 1;

    quoted_type = octstr_create("\"");
    octstr_append(quoted_type, osname);
    octstr_format_append(quoted_type, "%c", '"');

    if (octstr_case_search(content_header, quoted_type, 0) >= 0) {
        octstr_destroy(quoted_type);
        return 1;
    }

    octstr_destroy(quoted_type);
    return 0;
}

/*
 * Again looking after a parameter, this time of type boundary=XXX or boundary=
 * "XXX".
 */
static int get_mime_boundary(List *push_headers, Octstr *content_header, 
                             Octstr **boundary)
{
    long pos;
    Octstr *bos;
    int c;

    pos = 0;
    if ((pos = octstr_case_search(content_header, 
                                  bos = octstr_imm("boundary="), 0)) < 0) {
        warning(0, "no boundary specified");
        return -1;
    }

    pos += octstr_len(bos);
    if (octstr_get_char(content_header, pos) == '\"')
        ++pos;
    while ((c = octstr_get_char(content_header, pos)) != ';') {
        if (c != ' ' && c != '\"')
            octstr_format_append(*boundary, "%c", c);
        ++pos;
    }

    return 0;
}

static void change_header_value(List **push_headers, char *name, char *value)
{
    http_header_remove_all(*push_headers, name);
    http_header_add(*push_headers, name, value);
}





