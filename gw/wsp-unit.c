/*
 * wsp-unit.c - Implement WSP Connectionless mode
 *
 * Lars Wirzenius
 */


#include <string.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_pdu.h"
#include "wsp_headers.h"
#include "wapbox.h"


/*
 * Give the status the module:
 *
 *	limbo
 *		not running at all
 *	running
 *		operating normally
 *	terminating
 *		waiting for operations to terminate, returning to limbo
 */
static enum { limbo, running, terminating } run_status = limbo;


static List *queue = NULL;


static void main_thread(void *);
static Msg *pack_into_datagram(WAPEvent *event);


/***********************************************************************
 * Public functions
 */


void wsp_unit_init(void) {
	queue = list_create();
	list_add_producer(queue);
	run_status = running;
	gwthread_create(main_thread, NULL);
}


void wsp_unit_shutdown(void) {
	WAPEvent *e;
	
	gw_assert(run_status == running);
	run_status = terminating;
	list_remove_producer(queue);
	gwthread_join_every(main_thread);

	while ((e = list_extract_first(queue)) != e)
		wap_event_destroy(e);
	list_destroy(queue);
}


void wsp_unit_dispatch_event(WAPEvent *event) {
	wap_event_assert(event);
	list_produce(queue, event);
}


#ifdef POST_SUPPORT

WAPEvent *wsp_unit_unpack_wdp_datagram(Msg *msg) {
	WAPEvent *event;
	Octstr *os;
	WSP_PDU *pdu;
	long tid_byte;
	
	os = NULL;
	pdu = NULL;
	event = NULL;

	os = octstr_duplicate(msg->wdp_datagram.user_data);
	if (octstr_len(os) == 0) {
		warning(0, "WSP UNIT: Empty datagram.");
		goto error;
	}
	
	tid_byte = octstr_get_char(os, 0);
	octstr_delete(os, 0, 1);
	
	pdu = wsp_pdu_unpack(os);
	if (pdu == NULL)
		goto error;
	
/* POST_SUPPORT */
	if (pdu->type != Get && pdu->type != Post) {
		warning(0, "WSP UNIT: Unsupported PDU type %d", pdu->type);
		goto error;
	}

	event = wap_event_create(S_Unit_MethodInvoke_Ind);
	event->u.S_Unit_MethodInvoke_Ind.addr_tuple = wap_addr_tuple_create(
				msg->wdp_datagram.source_address,
				msg->wdp_datagram.source_port,
				msg->wdp_datagram.destination_address,
				msg->wdp_datagram.destination_port);


	event->u.S_Unit_MethodInvoke_Ind.transaction_id = tid_byte;
	
	switch (pdu->type) {
	case Get:
		debug("wap.wsp", 0, "Connectionless Get Request Received.");
		event->u.S_Unit_MethodInvoke_Ind.method = 0x40 + pdu->u.Get.subtype;
		event->u.S_Unit_MethodInvoke_Ind.request_uri = 
			octstr_duplicate(pdu->u.Get.uri);
		event->u.S_Unit_MethodInvoke_Ind.request_headers = 
			unpack_headers(pdu->u.Get.headers);
		event->u.S_Unit_MethodInvoke_Ind.request_body = NULL;
		break;
	case Post:
		debug("wap.wsp", 0, "Connectionless Post Request Received.");
		event->u.S_Unit_MethodInvoke_Ind.method = 0x60 + pdu->u.Post.subtype;
		event->u.S_Unit_MethodInvoke_Ind.request_uri = 
			octstr_duplicate(pdu->u.Post.uri);
		event->u.S_Unit_MethodInvoke_Ind.request_headers = 
			unpack_post_headers(pdu->u.Post.headers);
		event->u.S_Unit_MethodInvoke_Ind.request_body = 
			octstr_duplicate(pdu->u.Post.data);
		break;
	default:
		warning(0, "WSP UNIT: Unsupported PDU type %d", pdu->type);
		goto error;
	}

	return event;

error:
	octstr_destroy(os);
	wsp_pdu_destroy(pdu);
	wap_event_destroy(event);
	return NULL;
}


#else	/* POST_SUPPORT */


WAPEvent *wsp_unit_unpack_wdp_datagram(Msg *msg) {
	WAPEvent *event;
	Octstr *os;
	WSP_PDU *pdu;
	long tid_byte;
	
	os = NULL;
	pdu = NULL;
	event = NULL;

	os = octstr_duplicate(msg->wdp_datagram.user_data);
	if (octstr_len(os) == 0) {
		warning(0, "WSP UNIT: Empty datagram.");
		goto error;
	}
	
	tid_byte = octstr_get_char(os, 0);
	octstr_delete(os, 0, 1);
	
	pdu = wsp_pdu_unpack(os);
	if (pdu == NULL)
		goto error;
		
	if (pdu->type != Get) {
		warning(0, "WSP UNIT: Unsupported PDU type %d", pdu->type);
		goto error;
	}

	event = wap_event_create(S_Unit_MethodInvoke_Ind);
	event->u.S_Unit_MethodInvoke_Ind.addr_tuple = wap_addr_tuple_create(
				msg->wdp_datagram.source_address,
				msg->wdp_datagram.source_port,
				msg->wdp_datagram.destination_address,
				msg->wdp_datagram.destination_port);
	event->u.S_Unit_MethodInvoke_Ind.transaction_id = tid_byte;
	/* FIXME: This only works for Get pdus. */
	event->u.S_Unit_MethodInvoke_Ind.method = 0x40 + pdu->u.Get.subtype;
	event->u.S_Unit_MethodInvoke_Ind.request_uri = 
		octstr_duplicate(pdu->u.Get.uri);
	event->u.S_Unit_MethodInvoke_Ind.request_headers = 
		unpack_headers(pdu->u.Get.headers);
	event->u.S_Unit_MethodInvoke_Ind.request_body = NULL;

        wsp_pdu_destroy(pdu);
        octstr_destroy(os);
	
	return event;

error:
	octstr_destroy(os);
	wsp_pdu_destroy(pdu);
	wap_event_destroy(event);
	return NULL;
}


#endif	/* POST_SUPPORT */



/***********************************************************************
 * Local functions
 */


static void main_thread(void *arg) {
	WAPEvent *e;
	Msg *msg;
	
	while (run_status == running && (e = list_consume(queue)) != NULL) {
		wap_event_assert(e);
		switch (e->type) {
		case S_Unit_MethodInvoke_Ind:
			wap_appl_dispatch(e);
			break;
			
		case S_Unit_MethodResult_Req:
			msg = pack_into_datagram(e);
                        wap_event_destroy(e);
			if (msg != NULL)
				put_msg_in_queue(msg);
			break;
	
		default:
			warning(0, "WSP UNIT: Unknown event type %d", e->type);
                        wap_event_destroy(e);
			break;
		}
	}
}


static Msg *pack_into_datagram(WAPEvent *event) {
	Msg *msg;
	struct S_Unit_MethodResult_Req *p;
	WSP_PDU *pdu;
	Octstr *os, *ospdu;
	
	gw_assert(event->type == S_Unit_MethodResult_Req);
	p = &event->u.S_Unit_MethodResult_Req;

	pdu = wsp_pdu_create(Reply);
	pdu->u.Reply.status = wsp_convert_http_status_to_wsp_status(p->status);
	pdu->u.Reply.headers = wsp_encode_http_headers(p->response_type);
	pdu->u.Reply.data = octstr_duplicate(p->response_body);
	ospdu = wsp_pdu_pack(pdu);
	wsp_pdu_destroy(pdu);
	if (ospdu == NULL)
		return NULL;

	os = octstr_create_empty();
	octstr_append_char(os, p->transaction_id);
	octstr_append(os, ospdu);
	octstr_destroy(ospdu);

	msg = msg_create(wdp_datagram);
	
	msg->wdp_datagram.source_address = 
		octstr_duplicate(p->addr_tuple->server->address);
	msg->wdp_datagram.source_port = 
		p->addr_tuple->server->port;
	msg->wdp_datagram.destination_address = 
		octstr_duplicate(p->addr_tuple->client->address);
	msg->wdp_datagram.destination_port = 
		p->addr_tuple->client->port;
	msg->wdp_datagram.user_data = os;

	return msg;
}
