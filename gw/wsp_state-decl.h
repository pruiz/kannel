/*
 * wsp_state-decl.h
 *
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these.
 *
 * Note that two state machines defined by the specs are combined into one.
 * NULL_STATE of the method table is here CONNECTED.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

STATE_NAME(NULL_STATE)
STATE_NAME(CONNECTING)
STATE_NAME(CONNECTING_2)
STATE_NAME(CONNECTED)
STATE_NAME(HOLDING)
STATE_NAME(REQUESTING)
STATE_NAME(PROCESSING)
STATE_NAME(REPLYING)

ROW(NULL_STATE,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Connect,
	{
		WAPEvent *new_event;
		WAPEvent *wtp_event;

		/* Send TR-Invoke.res to WTP */
		wtp_event = wap_event_create(TR_Invoke_Res);
		if (wtp_event == NULL)
			panic(0, "wap_event_create failed");
		wtp_event->TR_Invoke_Res.tid = e->machine->tid;
		wtp_event->TR_Invoke_Res.exit_info = NULL;
		wtp_event->TR_Invoke_Res.exit_info_present = 0;
		wtp_event->TR_Invoke_Res.mid = e->machine->mid;
		wtp_dispatch_event(wtp_event);

		sm->n_methods = 0;

		if (pdu->u.Connect.capabilities_len > 0)
			unpack_caps(pdu->u.Connect.capabilities, sm);

		if (pdu->u.Connect.headers_len > 0) {
			List *hdrs;
			
			hdrs = unpack_headers(pdu->u.Connect.headers);
			http2_header_pack(hdrs);
			gw_assert(sm->http_headers == NULL);
			sm->http_headers = hdrs;
		}

		/*
		 * Send S-Connect.res to ourselves. NOTE: The spec says
		 * S-Connect.ind to the layer above WSP, but since we
		 * don't have any such layer, we'll just send .res to
		 * ourselves.
		 */
		new_event = wap_event_create(S_Connect_Res);
		new_event->S_Connect_Res.machine = e->machine;
		wsp_dispatch_event(new_event);
	},
	CONNECTING)


ROW(CONNECTING,
	S_Connect_Res,
	1,
	{
		WAPEvent *wtp_event;
		Octstr *ospdu;
		WSPMachine *sm2;
		List *old_sessions;
		long i;
		
		/* Send Disconnect event to existing sessions for client. */
		old_sessions = list_search_all(session_machines,
					       sm,
					       same_client);
		if (old_sessions != NULL) {
			for (i = 0; i < list_len(old_sessions); ++i) {
				sm2 = list_get(old_sessions, i);
				if (sm2 != sm)
					machine_mark_unused(sm2);
			}
			list_destroy(old_sessions);
		}

		/* Invent a new session ID since we're now the official
		 * session for this client. 
		 */
		sm->session_id = wsp_next_session_id();

		/* Make a ConnectReply PDU for WSP. */
		ospdu = make_connectreply_pdu(sm, sm->session_id);

		/* Make a TR-Result.req event for WTP. */
		wtp_event = wap_event_create(TR_Result_Req);
		wtp_event->TR_Result_Req.tid = e->machine->tid;
		wtp_event->TR_Result_Req.user_data = ospdu;
		wtp_event->TR_Result_Req.mid = e->machine->mid;
		wtp_dispatch_event(wtp_event);

		/* Release all method transactions in HOLDING state. */
	},
	CONNECTING_2)

ROW(CONNECTING_2,
	TR_Result_Cnf,
	1, /* XXX check here that it is a Connect transaction! */
	{
		/* Nothing to do (for a Connect transaction) */
	},
	CONNECTED)

ROW(CONNECTED,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Get &&
	sm->n_methods == 0 /* XXX check max from config */,
	{
		WAPEvent *new_event;
		List *headers;

		++sm->n_methods;

		if (octstr_len(pdu->u.Get.headers) > 0)
			headers = unpack_headers(pdu->u.Get.headers);
		else
			headers = NULL;

		new_event = wap_event_create(Release);
		new_event->Release.machine = e->machine;
		new_event->Release.url = octstr_duplicate(pdu->u.Get.uri);
		new_event->Release.http_headers = headers;
		wsp_dispatch_event(new_event);
	},
	HOLDING)

ROW(CONNECTED,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Post &&
	sm->n_methods == 0 /* XXX check max from config */,
	{
		WAPEvent *new_event;

		++sm->n_methods;

		new_event = wap_event_create(Release);
		new_event->Release.machine = e->machine;
		new_event->Release.url = octstr_duplicate(pdu->u.Post.uri);
		wsp_dispatch_event(new_event);
		/* XXX we should handle headers here as well --liw */
	},
	HOLDING)

ROW(CONNECTED,
	TR_Invoke_Ind,
	e->tcl == 0 && pdu->type == Disconnect,
	{
		machine_mark_unused(sm);
	},
	NULL_STATE)

ROW(CONNECTED,
	TR_Invoke_Ind,
	pdu->type == Resume,
	{
		WAPEvent *wtp_event;
		Octstr *ospdu;
		
		/* Send Disconnect event to existing sessions for client. */

		/* Invent a new session ID since we're now the official
		 * session for this client. 
		 */
		sm->session_id = wsp_next_session_id();
		debug("wap.wsp", 0, "WSP: Resuming ...Session ID is %ld", sm->session_id);

		/* Make a ConnectReply PDU for WSP. */
		ospdu = make_connectreply_pdu(sm, sm->session_id);

		/* Make a TR-Result.req event for WTP. */
		wtp_event = wap_event_create(TR_Result_Req);
		wtp_event->TR_Result_Req.tid = e->machine->tid;
		wtp_event->TR_Result_Req.user_data = ospdu;
		wtp_event->TR_Result_Req.mid = e->machine->mid;
		debug("wap.wsp", 0, "WSP: Resuming ...sending TR-Result.req event to old WTPMachine");
		wtp_dispatch_event(wtp_event);

		/* Release all method transactions in HOLDING state. */
	},
	CONNECTED)

ROW(HOLDING,
	Release,
	1,
	{
		WAPEvent *new_event;

		/* 
		 * This is where we start the HTTP fetch.
		 * We fork a new thread for it; if the fork succeeds,
		 * the thread sends us a S-MethodInvoke.res event. If
		 * it fails, then we have a problem.
		 */
		 
		new_event = wap_event_create(S_MethodInvoke_Ind);
		new_event->S_MethodInvoke_Ind.machine = e->machine;
		new_event->S_MethodInvoke_Ind.url = octstr_duplicate(e->url);
		new_event->S_MethodInvoke_Ind.method = Get_PDU;
		new_event->S_MethodInvoke_Ind.http_headers = 
			http2_header_duplicate(e->http_headers);
		new_event->S_MethodInvoke_Ind.server_transaction_id = 
			new_server_transaction_id();
		new_event->S_MethodInvoke_Ind.session = sm;
		wap_appl_dispatch(new_event);
	},
	REQUESTING)

ROW(REQUESTING,
	S_MethodInvoke_Res,
	1,
	{
		WAPEvent *wtp_event;
		
		/* Send TR-Invoke.res to WTP */
		wtp_event = wap_event_create(TR_Invoke_Res);
		wtp_event->TR_Invoke_Res.tid = e->machine->tid;
		wtp_event->TR_Invoke_Res.exit_info = NULL;
		wtp_event->TR_Invoke_Res.exit_info_present = 0;
		wtp_event->TR_Invoke_Res.mid = e->machine->mid;
		wtp_dispatch_event(wtp_event);
	},
	PROCESSING)

ROW(PROCESSING,
	S_MethodResult_Req,
	1,
	{
		WAPEvent *wtp_event;
		WSP_PDU *new_pdu;
		
		new_pdu = wsp_pdu_create(Reply);
		new_pdu->u.Reply.status = 
			convert_http_status_to_wsp_status(e->status);
		new_pdu->u.Reply.headers = 
			encode_http_headers(e->response_type);
		new_pdu->u.Reply.data = octstr_duplicate(e->response_body);

		/* Send TR-Result.req to WTP */
		wtp_event = wap_event_create(TR_Result_Req);
		wtp_event->TR_Result_Req.tid = e->machine->tid;
		wtp_event->TR_Result_Req.user_data = wsp_pdu_pack(new_pdu);
		wtp_event->TR_Result_Req.mid = e->machine->mid;
		wtp_dispatch_event(wtp_event);
		wsp_pdu_destroy(new_pdu);
	},
	REPLYING)

ROW(REPLYING,
	TR_Result_Cnf,
	1,
	{
		--sm->n_methods;
	},
	CONNECTED)

#undef ROW
#undef STATE_NAME
