/*
 * wap_push_pap.c: Implementation of Push Proxy Gateway interface to PI.
 *
 * By Aarno Syvänen for Wiral Ltd and for Wapit Ltd.
 */

#define HTTP_PORT 80
#define NUMBER_OF_PUSHES 100

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

/*
 * We need a mapping between HTTPClient structures, used by http library, and
 * push ids, used by ppg. 
 */
static Dict *http_clients = NULL;

/*
 * Mapping between urls used by pi and push ids used by ppg.
 */
static Dict *urls = NULL;

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

/*
 * Communicating with pi.
 */
static void send_bad_message_response(HTTPClient *c, Octstr *body_fragment,
                                      int code);
static void send_push_response(WAPEvent *e, Octstr *url);
static void send_to_pi(HTTPClient *c, Octstr *reply_body);
static Octstr *escape_fragment(Octstr *fragment);

/*****************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 *
 */

void wap_push_pap_init(wap_dispatch_func_t *ppg_dispatch)
{
     pap_queue = list_create();
     list_add_producer(pap_queue);
     http_open_port(HTTP_PORT);
     http_clients = dict_create(NUMBER_OF_PUSHES, NULL);
     urls = dict_create(NUMBER_OF_PUSHES, octstr_destroy_item);
     
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
     http_close_all_ports();
     dict_destroy(http_clients);
     dict_destroy(urls);

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

/*
 * We send a push response to a push initiator when we cannot parse MIME 
 * content or when our control entity was erroneous. Otherwise the response
 * is up to ppg module. In addition, we must store HTTPClient data structure
 * corresponding a given push id, so that we can send responses to the rigth
 * address.
 */
static void http_read_thread(void *arg)
{
    WAPEvent *ppg_event;
    size_t push_len;
    Octstr *pap_content,
           *push_data,
           *rdf_content,
           *mime_content,
           *plos,                      /* a temporary variable*/
           *boundary,
           *content_header,            /* Content-Type MIME header */
           *url,
           *ip;
    int compiler_status;
    List *push_headers,                /* MIME headers themselves */
         *content_headers,             /* Headers from the content entity, see
                                          PAP chapters 8.2, 13.1. Rfc 2045 
                                          grammar calls these MIME-part-hea-
                                          ders */
         *cgivars;
    HTTPClient *client;
    long port;
    
    port = HTTP_PORT;

    while (run_status == running) {
        client = http_accept_request(port, &ip, &url, &push_headers, 
                                     &mime_content, &cgivars);
        if (client == NULL) 
	    break;

        info(0, "PAP: http_read_thread: Request received from <%s: %s>", 
             octstr_get_cstr(url), octstr_get_cstr(ip));
        octstr_destroy(ip);
         
        if (!headers_acceptable(push_headers, &content_header)) {
            send_bad_message_response(client, content_header, PAP_BAD_REQUEST);
	    goto herror;
        }
        
        if (get_mime_boundary(push_headers, content_header, &boundary) == -1) {
            send_bad_message_response(client, content_header, PAP_BAD_REQUEST);
	    goto berror;
        }
        
        pap_content = octstr_create("");
        push_data = octstr_create("");
        rdf_content = octstr_create("");

        gw_assert(mime_content);
        if (!mime_parse(boundary, mime_content, &pap_content, &push_data, 
                        &content_headers, &rdf_content)) {
            send_bad_message_response(client, mime_content, PAP_BAD_REQUEST);
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
	    send_bad_message_response(client, pap_content, PAP_BAD_REQUEST);
            warning(0, "PAP: http_read_thread: pap control entity erroneous");
            goto no_compile;
        } else if (compiler_status == -1) {
            send_bad_message_response(client, pap_content, PAP_BAD_REQUEST);
            warning(0, "PAP: http_read_thread: non implemented pap feature"
                    " requested");
        } else {
	    dict_put(http_clients, ppg_event->u.Push_Message.pi_push_id, 
                     client);
            dict_put(urls, ppg_event->u.Push_Message.pi_push_id, url);
            debug("wap.push.pap", 0, "PAP: http_read_thread: pap control"
                  " entity compiled ok, sending to ppg");
            ppg_event->u.Push_Message.push_headers = 
                http_header_duplicate(push_headers);
            ppg_event->u.Push_Message.push_data = octstr_duplicate(push_data);
            dispatch_to_ppg(ppg_event);
        }

        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(mime_content);
        octstr_destroy(pap_content);
        octstr_destroy(push_data);
        octstr_destroy(rdf_content);
        octstr_destroy(boundary);
        continue;

no_compile:
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(mime_content);
        octstr_destroy(push_data);
        octstr_destroy(rdf_content);
        octstr_destroy(boundary);
        octstr_destroy(url);
        continue;

clean:
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(pap_content);
        octstr_destroy(push_data);
        octstr_destroy(rdf_content);
        octstr_destroy(content_header);
        octstr_destroy(boundary);
        octstr_destroy(url);
        continue;

herror:
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(content_header);
        octstr_destroy(url);
        continue;

berror:
        http_destroy_headers(push_headers);
        http_destroy_cgiargs(cgivars);
        octstr_destroy(mime_content);
        octstr_destroy(content_header);
        octstr_destroy(boundary);
        octstr_destroy(url);
        continue;
    }
}

static void handle_pap_event(WAPEvent *e)
{
    Octstr *url;

    switch (e->type) {
    case Push_Response:
        debug("wap.push.pap", 0, "PAP: handle_pap_event: we have a push"
              " response");
        url = dict_get(urls, e->u.Push_Response.pi_push_id);
        send_push_response(e, url);
        dict_remove(urls, e->u.Push_Response.pi_push_id);
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
    gw_assert(push_headers);
    *content_header = http_header_find_first(push_headers, "Content-Type");
    
    if (!type_is(*content_header, "multipart/related")) {
        goto error;
    }

    if (!type_is(*content_header, "application/xml")) {
        goto error;
    }

    return 1;

error:
    warning(0, "PAP: headers_acceptable: got unacceptable push headers");
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
        warning(0, "PAP: get_mime_boundary: no boundary specified");
        return -1;
    }

    pos += octstr_len(bos);
    if (octstr_get_char(content_header, pos) == '\"')
        ++pos;
    *boundary = octstr_create("");
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

/*
 * Badmessage-response element is redefined in PAP, Implementation Note, 
 * chapter 5.
 */
static void send_bad_message_response(HTTPClient *c, Octstr *fragment, 
                                      int code)
{
    Octstr *reply_body;

    reply_body = octstr_format("%s", 
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE pap PUBLIC \"-//WAPFORUM//DTD PAP 1.0//EN\""
                   " \"http://www.wapforum.org/DTD/pap_1.0.dtd\">"
        "<pap>"
             "<badmessage-response code=\"");
    octstr_format_append(reply_body, "%d", code);
    octstr_format_append(reply_body, "%s", "\""
                  " desc=\"");
    octstr_format_append(reply_body, "%s", "Not understood due to malformed"
                                            " syntax");
    octstr_format_append(reply_body, "%s", "\""
                  " bad-message-fragment=\"");
    octstr_format_append(reply_body, "%S", escape_fragment(fragment));
    octstr_format_append(reply_body, "%s", "\"");
    octstr_format_append(reply_body, "%s", ">"
              "</badmessage-response>"
         "</pap>");

    debug("wap.push.pap", 0, "PAP: bad message response to pi");
    send_to_pi(c, reply_body);

    octstr_destroy(fragment);
}

/*
 * Push response is defined in PAP, chapter 9.3. Mapping between push ids and
 * http clients is done by using http_clients. We remove (push id, http client)
 * pair from the dictionary after the mapping has been done.
 */
static void send_push_response(WAPEvent *e, Octstr *url)
{
    Octstr *reply_body,
           *pos;
    HTTPClient *c;

    gw_assert(e->type = Push_Response);
    reply_body = octstr_format("%s", 
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE pap PUBLIC \"-//WAPFORUM//DTD PAP 1.0//EN\""
                   " \"http://www.wapforum.org/DTD/pap_1.0.dtd\">"
        "<pap>"
             "<push-response push-id=\"");
    octstr_format_append(reply_body, "%S", 
        pos = e->u.Push_Response.pi_push_id);
    octstr_format_append(reply_body, "%s", "\""); 

    if (e->u.Push_Response.sender_name != NULL) {
        octstr_format_append(reply_body, "%s",
                   " sender-name=\"");
        octstr_format_append(reply_body, "%S", 
            e->u.Push_Response.sender_name);
        octstr_format_append(reply_body, "%s", "\"");
    }

    if (e->u.Push_Response.reply_time != NULL) {
        octstr_format_append(reply_body, "%s",
                   " reply-time=\"");
        octstr_format_append(reply_body, "%S", 
            e->u.Push_Response.reply_time);
        octstr_format_append(reply_body, "%s", "\"");
    }

    if (url != NULL) {
        octstr_format_append(reply_body, "%s",
                   " sender-address=\"");
        octstr_format_append(reply_body, "%S", url);
        octstr_format_append(reply_body, "%s", "\"");
    }

        octstr_format_append(reply_body, "%s", ">"
             "</push-response>"
         "</pap>");

    octstr_destroy(url);
    c = dict_get(http_clients, pos);
    dict_remove(http_clients, pos);

    debug("push.pap", 0, "PAP: push response to pi");
    send_to_pi(c, reply_body);
}

static void send_to_pi(HTTPClient *c, Octstr *reply_body) {
    size_t body_len; 
    List *reply_headers;
    Octstr *bos;          /* a temporary */

    reply_headers = http_create_empty_headers();
    http_header_add(reply_headers, "Content-Type", "application/xml");
    body_len = octstr_len(reply_body);
    http_header_add(reply_headers, "Content-Length", 
                    octstr_get_cstr(bos = octstr_format("%d", body_len)));
    octstr_destroy(bos);
    http_send_reply(c, HTTP_OK, reply_headers, reply_body);  

    octstr_destroy(reply_body);
    http_destroy_headers(reply_headers);
}

/*
 * Escape characters not allowed in the value of an attribute. PAP specs does
 * not define escape sequences for message fragments; here we remove dangerous
 * characters.
 */
static Octstr *escape_fragment(Octstr *fragment)
{
    long i;
    int c;

    i = 0;
    while (i < octstr_len(fragment)) {
        if ((c = octstr_get_char(fragment, i)) == '"') {
	    octstr_delete(fragment, i, 1);
            --i;
        } else if (c == '<') {
	    octstr_delete(fragment, i, 1);
            --i;
        } else if (c == '>') {
	    octstr_delete(fragment, i, 1);
            --i;
        } else if (c == '&') {
	    octstr_delete(fragment, i, 1);
            --i;
	    } 

        ++i;
    }

    return fragment;
}







