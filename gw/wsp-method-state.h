/*
 * wsp-method-state.h
 *
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these.
 *
 * Note that the `NULL' state has been renamed to `NULL_METHOD' because
 * NULL is reserved by C.
 *
 * Lars Wirzenius
 */

STATE_NAME(NULL_METHOD)
STATE_NAME(HOLDING)
STATE_NAME(REQUESTING)
STATE_NAME(PROCESSING)
STATE_NAME(REPLYING)

/* MISSING: TR_Invoke.ind, N_Methods == MOM */

ROW(NULL_METHOD,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Get,
	{
		List *headers;
		WAPEvent *invoke;

		/* Prepare the MethodInvoke here, because we have all
		 * the information nicely available. */

		if (octstr_len(pdu->u.Get.headers) > 0)
			headers = unpack_headers(pdu->u.Get.headers);
		else
			headers = NULL;

		invoke = wap_event_create(S_MethodInvoke_Ind);
		invoke->u.S_MethodInvoke_Ind.server_transaction_id =
			msm->transaction_id;
		/* XXX This 0x40 is the GET type, must fix it for POST/PUT */
		invoke->u.S_MethodInvoke_Ind.method = 0x40 + pdu->u.Get.subtype;
		invoke->u.S_MethodInvoke_Ind.url =
			octstr_duplicate(pdu->u.Get.uri);
		invoke->u.S_MethodInvoke_Ind.http_headers = headers;
		invoke->u.S_MethodInvoke_Ind.body = NULL; /* Use for POST */
		invoke->u.S_MethodInvoke_Ind.session_headers =
			http_header_duplicate(sm->http_headers);
		invoke->u.S_MethodInvoke_Ind.addr_tuple =
			wap_addr_tuple_duplicate(sm->addr_tuple);
		invoke->u.S_MethodInvoke_Ind.client_SDU_size =
			sm->client_SDU_size;
		invoke->u.S_MethodInvoke_Ind.session_id =
			msm->session_id;

		msm->invoke = invoke;
	},
	HOLDING)

#ifdef POST_SUPPORT

ROW(NULL_METHOD,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Post,
	{
		List *headers;
		WAPEvent *invoke;
		int req_body_size;

		/* Prepare the MethodInvoke here, because we have all
		 * the information nicely available. */

		if (octstr_len(pdu->u.Post.headers) > 0)
			headers = unpack_post_headers(pdu->u.Post.headers);
		else
			headers = NULL;

		invoke = wap_event_create(S_MethodInvoke_Ind);
		invoke->u.S_MethodInvoke_Ind.server_transaction_id =
			msm->transaction_id;
		/* XXX This 0x60 is the POST type and the subtype indicates whether a
		 * POST or a PUT. */
		invoke->u.S_MethodInvoke_Ind.method = 0x60 + pdu->u.Post.subtype;
		invoke->u.S_MethodInvoke_Ind.url =
			octstr_duplicate(pdu->u.Post.uri);
		invoke->u.S_MethodInvoke_Ind.http_headers = headers;
		
/*******			POST_SUPPORT			********/
/*******			Siemens Fix				********/
/*
 * The Siemens S35 adds an extra Null character to the end of the 
 * request body which may not work with certain cgi scripts. It is 
 * removed here by truncating the length.
 *
 */
		req_body_size = octstr_len(pdu->u.Post.data);
		if(octstr_get_char(pdu->u.Post.data,(req_body_size - 1)) == 0)
			octstr_truncate(pdu->u.Post.data,(req_body_size - 1));

/*******			Siemens Fix				********/

		invoke->u.S_MethodInvoke_Ind.body = 
			octstr_duplicate(pdu->u.Post.data);


		invoke->u.S_MethodInvoke_Ind.session_headers =
			http_header_duplicate(sm->http_headers);
		invoke->u.S_MethodInvoke_Ind.addr_tuple =
			wap_addr_tuple_duplicate(sm->addr_tuple);
		invoke->u.S_MethodInvoke_Ind.client_SDU_size =
			sm->client_SDU_size;
		invoke->u.S_MethodInvoke_Ind.session_id =
			msm->session_id;

		msm->invoke = invoke;
	},
	HOLDING)

#endif	/* POST_SUPPORT */
		
ROW(HOLDING,
	Release_Event,
	1,
	{
		/* S-MethodInvoke.ind */
		wap_appl_dispatch(msm->invoke);
		msm->invoke = NULL;
	},
	REQUESTING)

ROW(HOLDING,
	Abort_Event,
	1,
	{
		/* Decrement N_Methods; we don't do that */
		/* Tr-Abort.req(abort reason) the method */
		wsp_method_abort(msm, e->reason);
	},
	NULL_METHOD)

ROW(HOLDING,
	TR_Abort_Ind,
	e->abort_code == WSP_ABORT_DISCONNECT,
	{
		WAPEvent *wsp_event;

		/* Disconnect the session */
		wsp_event = wap_event_create(Disconnect_Event);
		wsp_event->u.Disconnect_Event.session_id = msm->session_id;
		/* We put this on the queue instead of doing it right away,
		 * because the session machine is currently our caller and
		 * we don't want to recurse.  We put it in the front of
		 * the queue because the state machine definitions expect
		 * an event to be handled completely before the next is
		 * started. */
		list_insert(queue, 0, wsp_event);
	},
	HOLDING)

ROW(HOLDING,
	TR_Abort_Ind,
	e->abort_code = WSP_ABORT_SUSPEND,
	{
		WAPEvent *wsp_event;

		/* Suspend the session */
		wsp_event = wap_event_create(Suspend_Event);
		wsp_event->u.Suspend_Event.session_id = msm->session_id;
		/* See story for Disconnect, above */
		list_insert(queue, 0, wsp_event);
	},
	HOLDING)

ROW(HOLDING,
	TR_Abort_Ind,
	e->abort_code != WSP_ABORT_DISCONNECT
	&& e->abort_code != WSP_ABORT_SUSPEND,
	{
		/* Decrement N_Methods; we don't do that */
	},
	NULL_METHOD)

ROW(REQUESTING,
	S_MethodInvoke_Res,
	1,
	{
		WAPEvent *wtp_event;
		
		/* Send TR-Invoke.res to WTP */
		wtp_event = wap_event_create(TR_Invoke_Res);
		wtp_event->u.TR_Invoke_Res.handle = msm->transaction_id;
		wtp_dispatch_event(wtp_event);
	},
	PROCESSING)

/* MISSING: REQUESTING, S-MethodAbort.req */

ROW(REQUESTING,
	Abort_Event,
	1,
	{
		/* Decrement N_Methods; we don't do that */

		/* TR-Abort.req(abort reason) the method */
		wsp_method_abort(msm, e->reason);

		/* S-MethodAbort.ind(abort reason) */
		wsp_indicate_method_abort(msm, e->reason);
	},
	NULL_METHOD)

ROW(REQUESTING,
	TR_Abort_Ind,
	e->abort_code == WSP_ABORT_DISCONNECT,
	{
		WAPEvent *wsp_event;

		/* Disconnect the session */
		wsp_event = wap_event_create(Disconnect_Event);
		wsp_event->u.Disconnect_Event.session_id = msm->session_id;
		list_insert(queue, 0, wsp_event);
	},
	REQUESTING)

ROW(REQUESTING,
	TR_Abort_Ind,
	e->abort_code = WSP_ABORT_SUSPEND,
	{
		WAPEvent *wsp_event;

		/* Suspend the session */
		wsp_event = wap_event_create(Suspend_Event);
		wsp_event->u.Suspend_Event.session_id = msm->session_id;
		list_insert(queue, 0, wsp_event);
	},
	REQUESTING)

ROW(REQUESTING,
	TR_Abort_Ind,
	e->abort_code != WSP_ABORT_DISCONNECT
	&& e->abort_code != WSP_ABORT_SUSPEND,
	{
		/* Decrement N_Methods; we don't do that */

		/* S-MethodAbort.ind(abort reason) */
		wsp_indicate_method_abort(msm, e->abort_code);
	},
	NULL_METHOD)

ROW(PROCESSING,
	S_MethodResult_Req,
	1,
	{
		WAPEvent *wtp_event;
		WSP_PDU *new_pdu;

		/* TR-Result.req */
		new_pdu = wsp_pdu_create(Reply);
		new_pdu->u.Reply.status = 
			wsp_convert_http_status_to_wsp_status(e->status);
		new_pdu->u.Reply.headers = 
			wsp_encode_http_headers(e->response_type);
		new_pdu->u.Reply.data = octstr_duplicate(e->response_body);

		/* Send TR-Result.req to WTP */
		wtp_event = wap_event_create(TR_Result_Req);
		wtp_event->u.TR_Result_Req.user_data = wsp_pdu_pack(new_pdu);
		wtp_event->u.TR_Result_Req.handle = msm->transaction_id;
		wtp_dispatch_event(wtp_event);
		wsp_pdu_destroy(new_pdu);
	},
	REPLYING)

/* MISSING: PROCESSING, S-MethodAbort.req */

ROW(PROCESSING,
	Abort_Event,
	1,
	{
		/* Decrement N_Methods; we don't do that */

		/* TR-Abort.req(abort reason) the method */
		wsp_method_abort(msm, e->reason);

		/* S-MethodAbort.ind(abort reason) */
		wsp_indicate_method_abort(msm, e->reason);
	},
	NULL_METHOD)

ROW(PROCESSING,
	TR_Abort_Ind,
	e->abort_code == WSP_ABORT_DISCONNECT,
	{
		WAPEvent *wsp_event;

		/* Disconnect the session */
		wsp_event = wap_event_create(Disconnect_Event);
		wsp_event->u.Disconnect_Event.session_id = msm->session_id;
		list_insert(queue, 0, wsp_event);
	},
	PROCESSING)

ROW(PROCESSING,
	TR_Abort_Ind,
	e->abort_code = WSP_ABORT_SUSPEND,
	{
		WAPEvent *wsp_event;

		/* Suspend the session */
		wsp_event = wap_event_create(Suspend_Event);
		wsp_event->u.Suspend_Event.session_id = msm->session_id;
		list_insert(queue, 0, wsp_event);
	},
	PROCESSING)

ROW(PROCESSING,
	TR_Abort_Ind,
	e->abort_code != WSP_ABORT_DISCONNECT
	&& e->abort_code != WSP_ABORT_SUSPEND,
	{
		/* Decrement N_Methods; we don't do that */

		/* S-MethodAbort.ind(abort reason) */
		wsp_indicate_method_abort(msm, e->abort_code);
	},
	NULL_METHOD)

/* MISSING: REPLYING, S-MethodAbort.req */

ROW(REPLYING,
	Abort_Event,
	1,
	{
		/* Decrement N_Methods; we don't do that */

		/* TR-Abort.req(abort reason) the method */
		wsp_method_abort(msm, e->reason);

		/* S-MethodAbort.ind(abort reason) */
		wsp_indicate_method_abort(msm, e->reason);
	},
	NULL_METHOD)

ROW(REPLYING,
	TR_Result_Cnf,
	1,
	{
		WAPEvent *new_event;

		/* Decrement N_Methods; we don't do that */

		/* S-MethodResult.cnf */
		/* We don't do acknowledgement headers */
		new_event = wap_event_create(S_MethodResult_Cnf);
		new_event->u.S_MethodResult_Cnf.server_transaction_id =
			msm->transaction_id;
		new_event->u.S_MethodResult_Cnf.session_id = msm->session_id;
		wap_appl_dispatch(new_event);
	},
	NULL_METHOD)

ROW(REPLYING,
	TR_Abort_Ind,
	e->abort_code == WSP_ABORT_DISCONNECT,
	{
		WAPEvent *wsp_event;

		/* Disconnect the session */
		wsp_event = wap_event_create(Disconnect_Event);
		wsp_event->u.Disconnect_Event.session_id = msm->session_id;
		list_insert(queue, 0, wsp_event);
	},
	REPLYING)

ROW(REPLYING,
	TR_Abort_Ind,
	e->abort_code = WSP_ABORT_SUSPEND,
	{
		WAPEvent *wsp_event;

		/* Suspend the session */
		wsp_event = wap_event_create(Suspend_Event);
		wsp_event->u.Suspend_Event.session_id = msm->session_id;
		list_insert(queue, 0, wsp_event);
	},
	REPLYING)

ROW(REPLYING,
	TR_Abort_Ind,
	e->abort_code != WSP_ABORT_DISCONNECT
	&& e->abort_code != WSP_ABORT_SUSPEND,
	{
		/* Decrement N_Methods; we don't do that */

		/* S-MethodAbort.ind(abort reason) */
		wsp_indicate_method_abort(msm, e->abort_code);
	},
	REPLYING)

#undef ROW
#undef STATE_NAME
