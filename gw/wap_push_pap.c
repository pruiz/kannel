/*
 * wap_push_pap.c: Implementation of Push Proxy Gateway interface to PI.
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#include <unistd.h>

#include "wap_push_pap.h"
#include "wap_push_ppg.h"
#include "wap_push_pap_compiler.h"
#include "gwlib/gwlib.h"

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
    long i;
    Octstr *pap_content,
           *push_data;
    int compiler_status;
    List *push_headers;
  
    sleep(10);
    push_headers = http_create_empty_headers();
    http_header_add(push_headers, "Content-Type", "text/vnd.wap.wml");
    http_header_add(push_headers, "X-WAP-Application-Id", 
                    "http://wap.wapit.com:push.sia");
    pap_content = octstr_create(""
                  "<?xml version=\"1.0\"?>"
                  "<!DOCTYPE pap PUBLIC \"-//WAPFORUM//DTD PAP//EN\" "
                             "\"http://www.wapforum.org/DTD/pap_1.0.dtd\">"
                  "<pap>"
                        "<push-message push-id=\"9fjeo39jf084@pi.com\""
                          " deliver-before-timestamp=\"2000-03-31T06:45:00Z\""
                          " deliver-after-timestamp=\"2000-02-27T06:45:00Z\""
                          " progress-notes-requested=\"false\">"
			     "<address address-value=\"WAPPUSH="
                                  "270.0.0.1/"
				"TYPE=IPv4@ppg.carrier.com\">"
                             "</address>"
                             "<quality-of-service"
                               " priority=\"low\""
                               " delivery-method=\"confirmed\""
                               " network-required=\"false\""
                               " bearer-required=\"false\">"
                             "</quality-of-service>"
                        "</push-message>"
                  "</pap>"         
                              "");

    push_data = octstr_create(""
                  "<?xml version=\"1.0\"?>"
                  "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\"" 
                  " \"http://www.wapforum.org/DTD/wml_1.1.xml\">"
                  "<wml>"
                       "<card id=\"main\" title=\"Hello, world\""
                                 " newcontext=\"true\">"
                            "<p>Hello, world.</p>"
                       "</card>"
                 "</wml>");

    i = 0;
    while (i < 0) {
        ppg_event = NULL;
        if ((compiler_status = pap_compile(pap_content, &ppg_event)) == -2) {
            warning(0, "PAP: http_read_thread: pap control message erroneous");
        } else if (compiler_status == -1) {
            warning(0, "PAP: http_read_thread: non-implemented pap feature"
                    " requested");
        } else {
            debug("wap.push.pap", 0, "PAP: http_read_thread: having a ppg"
                  " event");
	    ppg_event->u.Push_Message.push_headers = 
                http_header_duplicate(push_headers);
            ppg_event->u.Push_Message.push_data = octstr_duplicate(push_data);
            dispatch_to_ppg(ppg_event);
        }
        http_destroy_headers(push_headers);
        octstr_destroy(pap_content);
        octstr_destroy(push_data);   
        sleep(1);
        ++i;
    }

    http_destroy_headers(push_headers);
    octstr_destroy(pap_content);
    octstr_destroy(push_data);
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






