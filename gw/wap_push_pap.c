/*
 * wap_push_pap.c: Implementation of Push Proxy Gateway interface to PI.
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#include <unistd.h>

#include "wap_push_pap.h"
#include "wap_push_ppg.h"
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
static WAPEvent *convert_pap_to_event(Octstr *pap_content);

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
    Octstr *pap_content;
  
    sleep(10);
    pap_content = octstr_imm("not implemented");
    ppg_event = convert_pap_to_event(pap_content);

    i = 0;
    while (i < 2) {
      /*dispatch_to_ppg(wap_event_duplicate(ppg_event));*/
        sleep(1);
        ++i;
    }
    wap_event_destroy(ppg_event);  
}

static void handle_pap_event(WAPEvent *e)
{
    switch (e->type) {
    case Push_Response:
         debug("wap.push.pap", 0, "PAP: we have a push response");
         wap_event_destroy(e);
    break;

    default:
         error(0, "PAP: we have an unknown event");
         wap_event_dump(e);
	 wap_event_destroy(e);
    break;
    }
}

static WAPEvent *convert_pap_to_event(Octstr *pap_content)
{
    WAPEvent *ppg_event;
    List *push_headers;
    Octstr *push_data;

    push_headers = http_create_empty_headers();
    http_header_add(push_headers, "Content-Type", "text/vnd.wap.wml");
    http_header_add(push_headers, "X-WAP-Application-Id", 
                    "http://wap.wapit.com:push.sia");
    push_data = octstr_imm("<?xml version=\"1.0\"?>"
        "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\"" 
           " \"http://www.wapforum.org/DTD/wml_1.1.xml\">"
        "<wml>"
             "<card id=\"main\" title=\"Hello, world\" newcontext=\"true\">"
                   "<p>Hello, world.</p>"
             "</card>"
        "</wml>");

    octstr_destroy(pap_content);
    ppg_event = wap_event_create(Push_Message);
    ppg_event->u.Push_Message.pi_push_id = octstr_imm("0@ppg.wapit.fi");
    ppg_event->u.Push_Message.progress_notes_requested = PAP_FALSE;
    ppg_event->u.Push_Message.address_value =
        octstr_imm("WAPPUSH=127.0.0.1/TYPE=IPv4@ppg.wapit.fi");
    ppg_event->u.Push_Message.priority = LOW;
    ppg_event->u.Push_Message.delivery_method = CONFIRMED;
    ppg_event->u.Push_Message.network_required = PAP_FALSE;
    ppg_event->u.Push_Message.bearer_required = PAP_FALSE;
    ppg_event->u.Push_Message.push_headers = push_headers;
    ppg_event->u.Push_Message.push_data = octstr_duplicate(push_data);
    ppg_event->u.Push_Message.pi_capabilities = NULL;
    ppg_event->u.Push_Message.deliver_before_timestamp = 
        octstr_imm("2000-03-28T06:45:00Z");
    ppg_event->u.Push_Message.deliver_after_timestamp = 
        octstr_imm("2000-02-27T06:45:00Z");
        
    octstr_destroy(push_data); 
    return ppg_event;
}







