/*
 * wsp_unit.c - Implement WSP Connectionless mode
 *
 * Lars Wirzenius
 */


#include <string.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_pdu.h"
#include "wsp_headers.h"
#include "wap_events.h"
#include "wsp_strings.h"
#include "wap.h"


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

static wap_dispatch_func_t *dispatch_to_wdp;
static wap_dispatch_func_t *dispatch_to_appl;

static List *queue = NULL;


static void main_thread(void *);
static WAPEvent *pack_into_result_datagram(WAPEvent *event);
static WAPEvent *pack_into_push_datagram(WAPEvent *event);

/***********************************************************************
 * Public functions
 */


void wsp_unit_init(wap_dispatch_func_t *datagram_dispatch,
                   wap_dispatch_func_t *application_dispatch) {
	queue = list_create();
	list_add_producer(queue);
	dispatch_to_wdp = datagram_dispatch;
	dispatch_to_appl = application_dispatch;
        wsp_strings_init();
	run_status = running;
	gwthread_create(main_thread, NULL);
}


void wsp_unit_shutdown(void) {
	gw_assert(run_status == running);
	run_status = terminating;
	list_remove_producer(queue);
	gwthread_join_every(main_thread);
	list_destroy(queue, wap_event_destroy_item);
        wsp_strings_shutdown();
}


void wsp_unit_dispatch_event(WAPEvent *event) {
	wap_event_assert(event);
	list_produce(queue, event);
}


static WAPEvent *unpack_datagram(WAPEvent *datagram) {
	WAPEvent *event;
	Octstr *os;
	WSP_PDU *pdu;
	long tid_byte;
	int method;
	Octstr *method_name;

	gw_assert(datagram->type == T_DUnitdata_Ind);
	
	os = NULL;
	pdu = NULL;
	event = NULL;

	os = octstr_duplicate(datagram->u.T_DUnitdata_Ind.user_data);
	if (octstr_len(os) == 0) {
		warning(0, "WSP UNIT: Empty datagram.");
		goto error;
	}
	
	tid_byte = octstr_get_char(os, 0);
	octstr_delete(os, 0, 1);
	
	pdu = wsp_pdu_unpack(os);
	if (pdu == NULL)
		goto error;
	
	if (pdu->type != Get && pdu->type != Post) {
		warning(0, "WSP UNIT: Unsupported PDU type %d", pdu->type);
		goto error;
	}
        
	event = wap_event_create(S_Unit_MethodInvoke_Ind);
	event->u.S_Unit_MethodInvoke_Ind.addr_tuple = wap_addr_tuple_duplicate(
                datagram->u.T_DUnitdata_Ind.addr_tuple);
	event->u.S_Unit_MethodInvoke_Ind.transaction_id = tid_byte;
        
	switch (pdu->type) {
	case Get:
		debug("wap.wsp", 0, "Connectionless Get request received.");
		method = GET_METHODS + pdu->u.Get.subtype;
		event->u.S_Unit_MethodInvoke_Ind.request_uri = 
			octstr_duplicate(pdu->u.Get.uri);
		event->u.S_Unit_MethodInvoke_Ind.request_headers = 
			wsp_headers_unpack(pdu->u.Get.headers, 0);
		event->u.S_Unit_MethodInvoke_Ind.request_body = NULL;
		break;

	case Post:
		debug("wap.wsp", 0, "Connectionless Post request received.");
                method = POST_METHODS + pdu->u.Post.subtype;
		event->u.S_Unit_MethodInvoke_Ind.request_uri = 
			octstr_duplicate(pdu->u.Post.uri);
		event->u.S_Unit_MethodInvoke_Ind.request_headers = 
			wsp_headers_unpack(pdu->u.Post.headers, 1);
		event->u.S_Unit_MethodInvoke_Ind.request_body = 
			octstr_duplicate(pdu->u.Post.data);
		break;

	default:
		warning(0, "WSP UNIT: Unsupported PDU type %d", pdu->type);
		goto error;
	}

	method_name = wsp_method_to_string(method);
	if (method_name == NULL)
		method_name = octstr_format("UNKNOWN%02X", method);
	event->u.S_Unit_MethodInvoke_Ind.method = method_name;

	octstr_destroy(os);
	wsp_pdu_destroy(pdu);
	return event;

error:
	octstr_destroy(os);
	wsp_pdu_destroy(pdu);
	wap_event_destroy(event);
	return NULL;
}


/***********************************************************************
 * Local functions
 */


static void main_thread(void *arg) {
	WAPEvent *e;
	WAPEvent *newevent;
	
	while (run_status == running && (e = list_consume(queue)) != NULL) {
		wap_event_assert(e);
		switch (e->type) {
		case T_DUnitdata_Ind:
			newevent = unpack_datagram(e);
			dispatch_to_appl(newevent);
			break;

		case S_Unit_MethodResult_Req:
			newevent = pack_into_result_datagram(e);
			if (newevent != NULL)
				dispatch_to_wdp(newevent);
			break;

                case S_Unit_Push_Req:
		        newevent = pack_into_push_datagram(e);
                        if (newevent != NULL) 
				dispatch_to_wdp(newevent);
		        break;
	
		default:
			warning(0, "WSP UNIT: Unknown event type %d", e->type);
			break;
		}

                wap_event_destroy(e);
	}
}

/*
 * We do not set TUnitData.ind's SMS-specific fields here, because we do not
 * support sending results to the phone over SMS.
 */
static WAPEvent *pack_into_result_datagram(WAPEvent *event) {
	WAPEvent *datagram;
	struct S_Unit_MethodResult_Req *p;
	WSP_PDU *pdu;
	Octstr *ospdu;
	unsigned char tid;
	
	gw_assert(event->type == S_Unit_MethodResult_Req);
	p = &event->u.S_Unit_MethodResult_Req;

	pdu = wsp_pdu_create(Reply);
	pdu->u.Reply.status = wsp_convert_http_status_to_wsp_status(p->status);
	pdu->u.Reply.headers = wsp_headers_pack(p->response_headers, 1);
	pdu->u.Reply.data = octstr_duplicate(p->response_body);
	ospdu = wsp_pdu_pack(pdu);
	wsp_pdu_destroy(pdu);
	if (ospdu == NULL)
		return NULL;

	tid = p->transaction_id;
	octstr_insert_data(ospdu, 0, &tid, 1);

	datagram = wap_event_create(T_DUnitdata_Req);
	datagram->u.T_DUnitdata_Req.addr_tuple =
		wap_addr_tuple_duplicate(p->addr_tuple);
	datagram->u.T_DUnitdata_Req.user_data = ospdu;

	return datagram;
}

/*
 * According to WSP table 12, p. 63, push id and transaction id are stored 
 * into same field. T-UnitData.ind is different for IP and SMS bearer.
 */
static WAPEvent *pack_into_push_datagram(WAPEvent *event) {
        WAPEvent *datagram;
        WSP_PDU *pdu;
        Octstr *ospdu;
	unsigned char push_id;

        gw_assert(event->type == S_Unit_Push_Req);
        pdu = wsp_pdu_create(Push);
	pdu->u.Push.headers = wsp_headers_pack(
            event->u.S_Unit_Push_Req.push_headers, 1);
	pdu->u.Push.data = octstr_duplicate(
            event->u.S_Unit_Push_Req.push_body);
        ospdu = wsp_pdu_pack(pdu);
	wsp_pdu_destroy(pdu);
	if (ospdu == NULL)
	    return NULL;

        push_id = event->u.S_Unit_Push_Req.push_id;
	octstr_insert_data(ospdu, 0, &push_id, 1);

        debug("wap.wsp.unit", 0, "WSP_UNIT: Connectionless push accepted");
        datagram = wap_event_create(T_DUnitdata_Req);

        datagram->u.T_DUnitdata_Req.addr_tuple =
	    wap_addr_tuple_duplicate(event->u.S_Unit_Push_Req.addr_tuple);
        datagram->u.T_DUnitdata_Req.network_required = 
	    event->u.S_Unit_Push_Req.network_required;
        datagram->u.T_DUnitdata_Req.bearer_required =
	    event->u.S_Unit_Push_Req.bearer_required;

        if (event->u.S_Unit_Push_Req.bearer_required && 
                event->u.S_Unit_Push_Req.network_required) {
            datagram->u.T_DUnitdata_Req.bearer = 
  	        octstr_duplicate(event->u.S_Unit_Push_Req.bearer);
            datagram->u.T_DUnitdata_Req.network =
	        octstr_duplicate(event->u.S_Unit_Push_Req.network); 
        }

	datagram->u.T_DUnitdata_Req.user_data = ospdu;

        return datagram;
}





