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
STATE_NAME(TERMINATING)
STATE_NAME(CONNECTING_2)
STATE_NAME(CONNECTED)

ROW(NULL_SESSION,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Connect,
	{
		WAPEvent *new_event;
		WAPEvent *wtp_event;

		/* Send TR-Invoke.res to WTP */
		wtp_event = wap_event_create(TR_Invoke_Res);
		wtp_event->u.TR_Invoke_Res.handle = e->handle;
		wtp_dispatch_event(wtp_event);

		/* Assign a session ID for this session.  We do this
		 * early, instead of in the CONNECTING state, because
		 * we want to use the session id as a way for the
		 * application layer to refer back to this machine. */
		sm->session_id = wsp_next_session_id();

		if (pdu->u.Connect.capabilities_len > 0) {
			sm->request_caps = wsp_cap_unpack_list(
				pdu->u.Connect.capabilities);
		} else {
			sm->request_caps = list_create();
		}

		if (pdu->u.Connect.headers_len > 0) {
			List *hdrs;
			
			hdrs = unpack_headers(pdu->u.Connect.headers);
			http_header_pack(hdrs);
			gw_assert(sm->http_headers == NULL);
			sm->http_headers = hdrs;
		}

		/* Send S-Connect.ind to application layer */
		new_event = wap_event_create(S_Connect_Ind);
		new_event->u.S_Connect_Ind.addr_tuple =
			wap_addr_tuple_duplicate(e->addr_tuple);
		new_event->u.S_Connect_Ind.client_headers =
			http_header_duplicate(sm->http_headers);
		new_event->u.S_Connect_Ind.requested_capabilities =
			wsp_cap_duplicate_list(sm->request_caps);
		new_event->u.S_Connect_Ind.session_id = sm->session_id;
		wap_appl_dispatch(new_event);
	},
	CONNECTING)


ROW(CONNECTING,
	S_Connect_Res,
	1,
	{
		WAPEvent *wtp_event;
		Octstr *ospdu;

		sm->reply_caps = wsp_cap_duplicate_list(
				e->negotiated_capabilities);
		
		/* Send Disconnect event to existing sessions for client. */
		wsp_disconnect_other_sessions(sm);

		/* Assign a Session_ID for this session. */
		/* We've already done that in the NULL_STATE. */

		/* TR-Result.req(ConnectReply) */
		ospdu = make_connectreply_pdu(sm);

		wtp_event = wap_event_create(TR_Result_Req);
		wtp_event->u.TR_Result_Req.user_data = ospdu;
		wtp_event->u.TR_Result_Req.handle = sm->connect_handle;
		wtp_dispatch_event(wtp_event);

		/* Release all method transactions in HOLDING state. */
		wsp_release_holding_methods(sm);
	},
	CONNECTING_2)

/* MISSING: CONNECTING, S_Disconnect_Req, reason == 301 (moved permanently) or
 * 302 (moved temporarily). */

/* MISSING: CONNECTING, S_Disconnect_Req, reason == anything else */

ROW(CONNECTING,
	Disconnect_Event,
	1,
	{
		/* TR-Abort.req(DISCONNECT) the Connect transaction */
		wsp_abort_session(sm, WSP_ABORT_DISCONNECT);

		/* Abort(DISCONNECT) all method transactions */
		wsp_abort_methods(sm, WSP_ABORT_DISCONNECT);

		/* S-Disconnect.ind(USERREQ) */
		wsp_indicate_disconnect(sm, WSP_ABORT_USERREQ);
	},
	NULL_SESSION)

/* MISSING: CONNECTING, Suspend_Event */

#ifdef POST_SUPPORT

ROW(CONNECTING,
	TR_Invoke_Ind,
	e->tcl == 2 && (pdu->type == Get || pdu->type == Post),
	{ 
		WSPMethodMachine *msm;

		/* Start new method transaction */
		msm = method_machine_create(sm, e->handle);

		/* Hand off the event to the new method machine */
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTING)

#else	/* POST_SUPPORT */

ROW(CONNECTING,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Get,
	{ 
		WSPMethodMachine *msm;

		/* Start new method transaction */
		msm = method_machine_create(sm, e->handle);

		/* Hand off the event to the new method machine */
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTING)

#endif	/* POST_SUPPORT */

/* MISSING: CONNECTING, TR_Invoke_Ind, pdu->type = Resume */

ROW(CONNECTING,
	TR_Abort_Ind,
	e->handle == sm->connect_handle,
	{
		/* Abort(DISCONNECT) all method transactions */
		wsp_abort_methods(sm, WSP_ABORT_DISCONNECT);

		/* S-Disconnect.ind(abort reason) */
		wsp_indicate_disconnect(sm, e->abort_code);
	},
	NULL_SESSION)

ROW(CONNECTING,
	TR_Abort_Ind,
	e->handle != sm->connect_handle,
	{
		WSPMethodMachine *msm;

		msm = wsp_find_method_machine(sm, e->handle);
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTING)

ROW(TERMINATING,
	Disconnect_Event,
	1,
	{
		/* TR-Abort.req(DISCONNECT) remaining transport transaction */
		wsp_abort_session(sm, WSP_ABORT_DISCONNECT);
	},
	NULL_SESSION)
		
ROW(TERMINATING,
	Suspend_Event,
	1,
	{
		/* TR-Abort.req(SUSPEND) remaining transport transaction */
		wsp_abort_session(sm, WSP_ABORT_SUSPEND);
	},
	NULL_SESSION)

ROW(TERMINATING,
	TR_Result_Cnf,
	1,
	{
		/* Ignore */
	},
	NULL_SESSION)

ROW(TERMINATING,
	TR_Abort_Ind,
	1,
	{
		/* Ignore */
	},
	NULL_SESSION)

/* MISSING: CONNECTING_2, S-Disconnect.req */

ROW(CONNECTING_2,
	Disconnect_Event,
	1,
	{
		/* TR-Abort.req(DISCONNECT) the Connect transaction */
		wsp_abort_session(sm, WSP_ABORT_DISCONNECT);

		/* Abort(DISCONNECT) all method and push transactions */
		wsp_abort_methods(sm, WSP_ABORT_DISCONNECT);

		/* S-Disconnect.ind(DISCONNECT) */
		wsp_indicate_disconnect(sm, WSP_ABORT_DISCONNECT);
	},
	NULL_SESSION)

ROW(CONNECTING_2,
	S_MethodInvoke_Res,
	1,
	{
		WSPMethodMachine *msm;

		/* See method state table */
		msm = wsp_find_method_machine(sm, e->server_transaction_id);
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTING_2)

ROW(CONNECTING_2,
	S_MethodResult_Req,
	1,
	{
		WSPMethodMachine *msm;

		/* See method state table */
		msm = wsp_find_method_machine(sm, e->server_transaction_id);
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTING_2)

/* MISSING: CONNECTING_2, S-Push.req */
/* MISSING: CONNECTING_2, S-ConfirmedPush.req */

ROW(CONNECTING_2,
	Suspend_Event,
	1,
	{
		/* Session Resume facility disabled */

		/* TR-Abort.req(DISCONNECT) the Connect transaction */
		wsp_abort_session(sm, WSP_ABORT_DISCONNECT);

		/* Abort(DISCONNECT) all method and push transactions */
		wsp_abort_methods(sm, WSP_ABORT_DISCONNECT);

		/* S-Disconnect.ind(SUSPEND) */
		wsp_indicate_disconnect(sm, WSP_ABORT_SUSPEND);
	},
	NULL_SESSION)

/* MISSING: CONNECTING_2, Session Resume facility enabled */

#ifdef POST_SUPPORT

ROW(CONNECTING_2,
	TR_Invoke_Ind,
	e->tcl == 2 && (pdu->type == Get || pdu->type == Post),
	{
		WSPMethodMachine *msm;
		WAPEvent *new_event;

		/* Start new method transaction */
		msm = method_machine_create(sm, e->handle);

		/* Hand off the event to the new method machine */
		handle_method_event(sm, msm, current_event, pdu);

		/* Release the new method transaction */
		new_event = wap_event_create(Release_Event);
		handle_method_event(sm, msm, new_event, NULL);
		wap_event_destroy(new_event);
	},
	CONNECTING_2)

#else	/* POST_SUPPORT */

ROW(CONNECTING_2,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Get,
	{
		WSPMethodMachine *msm;
		WAPEvent *new_event;

		/* Start new method transaction */
		msm = method_machine_create(sm, e->handle);

		/* Hand off the event to the new method machine */
		handle_method_event(sm, msm, current_event, pdu);

		/* Release the new method transaction */
		new_event = wap_event_create(Release_Event);
		handle_method_event(sm, msm, new_event, NULL);
		wap_event_destroy(new_event);
	},
	CONNECTING_2)

#endif	/* POST_SUPPORT */

ROW(CONNECTING_2,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Resume,
	{
		/* Resume facility disabled */

		WAPEvent *wtp_event;

		/* TR-Abort.req(DISCONNECT) the TR-Invoke */
		wtp_event = wap_event_create(TR_Abort_Req);
		wtp_event->u.TR_Abort_Req.abort_type = 0x01;
		wtp_event->u.TR_Abort_Req.abort_reason = WSP_ABORT_DISCONNECT;
		wtp_event->u.TR_Abort_Req.handle = e->handle;
		wtp_dispatch_event(wtp_event);
	},
	CONNECTING_2)

/* MISSING: As above, Resume facility enabled */

ROW(CONNECTING_2,
	TR_Invoke_Ind,
	e->tcl == 0 && pdu->type == Disconnect,
	{
		/* TR-Abort.req(DISCONNECT) the Connect transaction */
		wsp_abort_session(sm, WSP_ABORT_DISCONNECT);

		/* Abort(DISCONNECT) all method and push transactions */
		wsp_abort_methods(sm, WSP_ABORT_DISCONNECT);

		/* S-Disconnect.ind(DISCONNECT) */
		wsp_indicate_disconnect(sm, WSP_ABORT_DISCONNECT);
	},
	NULL_SESSION)

/* MISSING: CONNECTING_2, TR-Invoke.ind(Suspend), Session Resume facility enabled */

/* MISSING: CONNECTING_2, TR_Invoke.cnf for push transaction */

ROW(CONNECTING_2,
	TR_Result_Cnf,
	e->handle == sm->connect_handle,
	{
	},
	CONNECTED)

ROW(CONNECTING_2,
	TR_Result_Cnf,
	e->handle != sm->connect_handle,
	{
		WSPMethodMachine *msm;

		/* See method state table */
		msm = wsp_find_method_machine(sm, e->handle);
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTING_2)

ROW(CONNECTING_2,
	TR_Abort_Ind,
	e->handle == sm->connect_handle,
	{
		/* Abort(DISCONNECT) all method and push transactions */
		wsp_abort_methods(sm, WSP_ABORT_DISCONNECT);

		/* S-Disconnect.ind(abort reason) */
		wsp_indicate_disconnect(sm, e->abort_code);
	},
	NULL_SESSION)

/* MISSING: As below, for push transactions */

ROW(CONNECTING_2,
	TR_Abort_Ind,
	e->handle != sm->connect_handle,
	{
		WSPMethodMachine *msm;

		/* See method state table */
		msm = wsp_find_method_machine(sm, e->handle);
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTING_2)

/* MISSING: CONNECTED, S-Disconnect.req */

ROW(CONNECTED,
	Disconnect_Event,
	1,
	{
		/* Abort(DISCONNECT) all method and push transactions */
		wsp_abort_methods(sm, WSP_ABORT_DISCONNECT);

		/* S-Disconnect.ind(DISCONNECT) */
		wsp_indicate_disconnect(sm, WSP_ABORT_DISCONNECT);
	},
	NULL_SESSION)

ROW(CONNECTED,
	S_MethodInvoke_Res,
	1,
	{
		WSPMethodMachine *msm;

		/* See method state table */
		msm = wsp_find_method_machine(sm, e->server_transaction_id);
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTED)
		
ROW(CONNECTED,
	S_MethodResult_Req,
	1,
	{
		WSPMethodMachine *msm;

		/* See method state table */
		msm = wsp_find_method_machine(sm, e->server_transaction_id);
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTED)

/* MISSING: CONNECTED, S-Push.req */
/* MISSING: CONNECTED, S-ConfirmedPush.req */

ROW(CONNECTED,
	Suspend_Event,
	1,
	{
		/* Session Resume facility disabled */

		/* Abort(SUSPEND) all method and push transactions */
		wsp_abort_methods(sm, WSP_ABORT_SUSPEND);

		/* S-Disconnect.ind(SUSPEND) */
		wsp_indicate_disconnect(sm, WSP_ABORT_SUSPEND);
	},
	NULL_SESSION)

/* MISSING: CONNECTED, Session Resume facility enabled */

#ifdef POST_SUPPORT

ROW(CONNECTED,
	TR_Invoke_Ind,
	e->tcl == 2 && (pdu->type == Get || pdu->type == Post),
	{
		WSPMethodMachine *msm;
		WAPEvent *new_event;

		/* Start new method transaction */
		msm = method_machine_create(sm, e->handle);

		/* Hand off the event to the new method machine */
		handle_method_event(sm, msm, current_event, pdu);

		/* Release the new method transaction */
		new_event = wap_event_create(Release_Event);
		handle_method_event(sm, msm, new_event, NULL);
		wap_event_destroy(new_event);
	},
	CONNECTED)

#else	/* POST_SUPPORT */

ROW(CONNECTED,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Get,
	{
		WSPMethodMachine *msm;
		WAPEvent *new_event;

		/* Start new method transaction */
		msm = method_machine_create(sm, e->handle);

		/* Hand off the event to the new method machine */
		handle_method_event(sm, msm, current_event, pdu);

		/* Release the new method transaction */
		new_event = wap_event_create(Release_Event);
		handle_method_event(sm, msm, new_event, NULL);
		wap_event_destroy(new_event);
	},
	CONNECTED)

#endif	/* POST_SUPPORT */

ROW(CONNECTED,
	TR_Invoke_Ind,
	e->tcl == 2 && pdu->type == Resume,
	{
		/* Resume facility disabled */

		WAPEvent *wtp_event;

		/* TR-Abort.req(DISCONNECT) the TR-Invoke */
		wtp_event = wap_event_create(TR_Abort_Req);
		wtp_event->u.TR_Abort_Req.abort_type = 0x01;
		wtp_event->u.TR_Abort_Req.abort_reason = WSP_ABORT_DISCONNECT;
		wtp_event->u.TR_Abort_Req.handle = e->handle;
		wtp_dispatch_event(wtp_event);
	},
	CONNECTED)

/* MISSING: As above, Resume facility enabled */

ROW(CONNECTED,
	TR_Invoke_Ind,
	e->tcl == 0 && pdu->type == Disconnect,
	{
		/* Abort(DISCONNECT) all method and push transactions */
		wsp_abort_methods(sm, WSP_ABORT_DISCONNECT);

		/* S-Disconnect.ind(DISCONNECT) */
		wsp_indicate_disconnect(sm, WSP_ABORT_DISCONNECT);
	},
	NULL_SESSION)

/* MISSING: CONNECTED, TR-Invoke.ind(Suspend), Session Resume facility enabled */

/* MISSING: CONNECTED, Tr-Invoke.cnf for push transaction */

ROW(CONNECTED,
	TR_Result_Cnf,
	e->handle != sm->connect_handle,
	{
		WSPMethodMachine *msm;

		/* See method state table */
		msm = wsp_find_method_machine(sm, e->handle);
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTED)
		
/* MISSING: As below, for push transactions */

ROW(CONNECTED,
	TR_Abort_Ind,
	e->handle != sm->connect_handle,
	{
		WSPMethodMachine *msm;

		/* See method state table */
		msm = wsp_find_method_machine(sm, e->handle);
		handle_method_event(sm, msm, current_event, pdu);
	},
	CONNECTED)

/* MISSING: SUSPENDED state */

/* MISSING: RESUMING state */

/* MISSING: RESUMING_2 state */

#undef ROW
#undef STATE_NAME
