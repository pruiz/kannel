/*
 * wsp-session-state-decl.h - states for WSP session state machines
 *
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these.
 *
 * Note that `NULL' state is renamed to `NULL_SESSION' because NULL is
 * reserved by C.
 *
 * Lars Wirzenius
 */

STATE_NAME(NULL_SESSION)
STATE_NAME(CONNECTING)
STATE_NAME(CONNECTING_2)
STATE_NAME(CONNECTED)

ROW(NULL_SESSION,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Connect,
	{
		WAPEvent *new_event;
		WAPEvent *wtp_event;

		/* Remember the tid for the Connect transaction */
		sm->connect_tid = e->tid;

		/* Send TR-Invoke.res to WTP */
		wtp_event = wap_event_create(TR_Invoke_Res);
		wtp_event->u.TR_Invoke_Res.tid = e->tid;
		wtp_event->u.TR_Invoke_Res.mid = e->mid;
		wtp_dispatch_event(wtp_event);

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
		new_event->u.S_Connect_Res.mid = e->mid;
		new_event->u.S_Connect_Res.tid = e->tid;
		wsp_session_dispatch_event(new_event);
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
		old_sessions = list_extract_all(session_machines,
					       sm,
					       same_client);
		if (old_sessions != NULL) {
			for (i = 0; i < list_len(old_sessions); ++i) {
				sm2 = list_get(old_sessions, i);
				if (sm2 != sm)
					machine_destroy(sm2);
				else
					list_append(session_machines, sm);
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
		wtp_event->u.TR_Result_Req.tid = e->tid;
		wtp_event->u.TR_Result_Req.user_data = ospdu;
		wtp_event->u.TR_Result_Req.mid = e->mid;
		wtp_dispatch_event(wtp_event);

		/* Release all method transactions in HOLDING state. */
	},
	CONNECTING_2)

ROW(CONNECTING_2,
	TR_Result_Cnf,
	e->tid == sm->connect_tid,
	{
	},
	CONNECTED)

ROW(CONNECTED,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Get,
	{
		WAPEvent *new_event;
		List *headers;
		WSPMethodMachine *msm;

		if (octstr_len(pdu->u.Get.headers) > 0)
			headers = unpack_headers(pdu->u.Get.headers);
		else
			headers = NULL;

		msm = method_machine_create(sm->addr_tuple, e->tid);
		list_append(method_machines, msm);

		new_event = wap_event_create(Release);
		new_event->u.Release.mid = e->mid;
		new_event->u.Release.tid = e->tid;
		new_event->u.Release.msmid = msm->id;
		new_event->u.Release.session_headers =
			http2_header_duplicate(sm->http_headers);
		new_event->u.Release.addr_tuple =
			wap_addr_tuple_duplicate(sm->addr_tuple);
		new_event->u.Release.session_id = sm->session_id;
		new_event->u.Release.client_SDU_size = sm->client_SDU_size;
		new_event->u.Release.url = octstr_duplicate(pdu->u.Get.uri);
		new_event->u.Release.http_headers = headers;
		wsp_session_dispatch_event(new_event);
	},
	CONNECTED)

ROW(CONNECTED,
	TR_Invoke_Ind,
	e->tcl == 0 && pdu->type == Disconnect,
	{
	},
	NULL_SESSION)

#undef ROW
#undef STATE_NAME
