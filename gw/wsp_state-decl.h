/*
 * wsp_state-decl.h
 *
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these.
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
	TRInvokeIndication,
	e->tcl == 2 && wsp_deduce_pdu_type(e->user_data, 0) == Connect_PDU,
	{
		WSPEvent *new_event;
		WTPEvent *wtp_event;

		/* Send TR-Invoke.res to WTP */
		wtp_event = wtp_event_create(TRInvokeResponse);
		if (wtp_event == NULL)
			panic(0, "wtp_event_create failed");
		wtp_event->TRInvokeResponse.tid = e->machine->tid;
		wtp_event->TRInvokeResponse.exit_info = NULL;
		wtp_event->TRInvokeResponse.exit_info_present = 0;
		debug("wap.wsp", 0, "WSP: sending TR-Invoke.res event to WTP");
		wtp_handle_event(e->machine, wtp_event);

		sm->n_methods = 0;

                if (unpack_connect_pdu(sm, e->user_data) == -1)
		         error(0, "Unpacking Connect PDU failed, oops.");

		/*
		 * Send S-Connect.res to ourselves. NOTE: The spec says
		 * S-Connect.ind to the layer above WSP, but since we
		 * don't have any such layer, we'll just send .res to
		 * ourselves.
		 */
		debug("wap.wsp", 0, "WSP: sending S-Connect.res to ourselves");
		new_event = wsp_event_create(SConnectResponse);
		new_event->SConnectResponse.machine = e->machine;
		wsp_handle_event(sm, new_event);
	},
	CONNECTING)


#if 0
/*
 * When WAP box is restarting, the first PDU can be other than Connect. (That 
 * can happen when the bearerbox is sending us old packets or the peer has not
 * closed the connection.) Get (hopefully) means that the connection is still 
 * open. We simply ignore Disconnect PDU.
 */

ROW(NULL_STATE,
        TRInvokeIndication,
	e->tcl == 2 && wsp_deduce_pdu_type(e->user_data, 0) == Get_PDU &&
	sm->n_methods == 0 /* XXX check max from config */,
	{
		WSPEvent *new_event;
		Octstr *url;
		Octstr *headers;

		++sm->n_methods;

		debug("wap.wsp", 0, "WSP: Got Get PDU, state being NULL_STATE");
                if (unpack_get_pdu(&url, &headers, e->user_data) == -1)
			error(0, "Unpacking Get PDU failed, oops.");
		debug("wap.wsp", 0, "WSP: sending Release to ourselves");
		new_event = wsp_event_create(Release);
		new_event->Release.machine = e->machine;
		new_event->Release.url = url;
		wsp_handle_event(sm, new_event);

	},
	HOLDING)

ROW(NULL_STATE,
    TRInvokeIndication,
    e->tcl == 0 && wsp_deduce_pdu_type(e->user_data, 0) == Disconnect_PDU,
    { },
    NULL_STATE)
#endif

ROW(CONNECTING,
	SConnectResponse,
	1,
	{
		WTPEvent *wtp_event;
		Octstr *pdu;
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
					sm2->client_port = -1;
			}
			list_destroy(old_sessions);
		}

		/* Invent a new session ID since we're now the official
		 * session for this client. 
		 */
		sm->session_id = wsp_next_session_id();
		debug("wap.wsp", 0, "WSP: Session ID is %ld", sm->session_id);

		/* Make a ConnectReply PDU for WSP. */
		pdu = make_connectreply_pdu(sm, sm->session_id);

		/* Make a TR-Result.req event for WTP. */
		wtp_event = wtp_event_create(TRResultRequire);
		wtp_event->TRResultRequire.tid = e->machine->tid;
		wtp_event->TRResultRequire.user_data = pdu;
		debug("wap.wsp", 0, "WSP: sending TR-Result.req event to old WTPMachine");
		wtp_handle_event(e->machine, wtp_event);

		/* Release all method transactions in HOLDING state. */
	},
	CONNECTING_2)

ROW(CONNECTING_2,
	TRResultConfirmation,
	1, /* XXX check here that it is a Connect transaction! */
	{
		/* Nothing to do (for a Connect transaction) */
	},
	CONNECTED)

ROW(CONNECTED,
	TRInvokeIndication,
	e->tcl == 2 && wsp_deduce_pdu_type(e->user_data, 0) == Get_PDU &&
	sm->n_methods == 0 /* XXX check max from config */,
	{
		WSPEvent *new_event;
		Octstr *url;
		HTTPHeader *headers;

		++sm->n_methods;

		if (unpack_get_pdu(&url, &headers, e->user_data) == -1)
			error(0, "Unpacking Get PDU failed, oops.");
		else {
			debug("wap.wsp", 0, "WSP: sending Release to ourselves");
			new_event = wsp_event_create(Release);
			new_event->Release.machine = e->machine;
			new_event->Release.url = url;
			new_event->Release.http_headers = headers;
			wsp_handle_event(sm, new_event);
		}

	},
	HOLDING)

ROW(CONNECTED,
	TRInvokeIndication,
	e->tcl == 2 && wsp_deduce_pdu_type(e->user_data, 0) == Post_PDU &&
	sm->n_methods == 0 /* XXX check max from config */,
	{
		WSPEvent *new_event;
		Octstr *url;
		Octstr *headers;

		++sm->n_methods;

		debug("wap.wsp", 0, "WSP: Post Request received");
		if (unpack_post_pdu(&url, &headers, e->user_data) == -1)
			error(0, "Unpacking Post PDU failed, oops.");
		else {
			debug("wap.wsp", 0, "WSP: sending Release to ourselves");
			new_event = wsp_event_create(Release);
			new_event->Release.machine = e->machine;
			new_event->Release.url = url;
			wsp_handle_event(sm, new_event);
		}

	},
	HOLDING)

ROW(CONNECTED,
	TRInvokeIndication,
	e->tcl == 0 && wsp_deduce_pdu_type(e->user_data, 0) == Disconnect_PDU,
	{
		debug("wap.wsp", 0, "WSP: Got Disconnect PDU.");
	},
	NULL_STATE)

#if 0 /* XXX This doesn't work at all. It initializes the wrong fields
	of the WTPEvent structure. I don't have the time to fix it yet.
	--liw */
ROW(CONNECTED,
	TRInvokeIndication,
	wsp_deduce_pdu_type(e->user_data, 0) == Connect_PDU,
	{
		WTPEvent *wtp_event;
		Octstr *pdu;
		
		debug("wap.wsp", 0, "WSP: Got Reconnect PDU.");
		/* Send Disconnect event to existing sessions for client. */

		/* Make a TR-Result.req event for WTP. */
		wtp_event = wtp_event_create(TRAbort);
		wtp_event->TRResultRequire.tid = e->machine->tid;
		wtp_event->TRResultRequire.user_data = pdu;
		debug("wap.wsp", 0, "WSP: Try Killing WTP....");
		wtp_handle_event(e->machine, wtp_event);

	},
	NULL_STATE)
#endif

ROW(CONNECTED,
	TRInvokeIndication,
	wsp_deduce_pdu_type(e->user_data, 0) == Resume_PDU,
	{
		WTPEvent *wtp_event;
		Octstr *pdu;
		
		
		/* Send Disconnect event to existing sessions for client. */

		/* Invent a new session ID since we're now the official
		 * session for this client. 
		 */
		sm->session_id = wsp_next_session_id();
		debug("wap.wsp", 0, "WSP: Resuming ...Session ID is %ld", sm->session_id);

		/* Make a ConnectReply PDU for WSP. */
		pdu = make_connectreply_pdu(sm, sm->session_id);

		/* Make a TR-Result.req event for WTP. */
		wtp_event = wtp_event_create(TRResultRequire);
		wtp_event->TRResultRequire.tid = e->machine->tid;
		wtp_event->TRResultRequire.user_data = pdu;
		debug("wap.wsp", 0, "WSP: Resuming ...sending TR-Result.req event to old WTPMachine");
		wtp_handle_event(e->machine, wtp_event);

		/* Release all method transactions in HOLDING state. */
	},
	CONNECTED)

ROW(HOLDING,
	Release,
	1,
	{
		WSPEvent *new_event;

		/* 
		 * This is where we start the HTTP fetch.
		 * We fork a new thread for it; if the fork succeeds,
		 * the thread sends us a S-MethodInvoke.res event. If
		 * it fails, then we have a problem.
		 */
		 
		new_event = wsp_event_create(SMethodInvokeResult);
		new_event->SMethodInvokeResult.session = sm;
		new_event->SMethodInvokeResult.machine = e->machine;
		new_event->SMethodInvokeResult.url = e->url;
		new_event->SMethodInvokeResult.method = Get_PDU;
		new_event->SMethodInvokeResult.http_headers = 
			e->http_headers;
		new_event->SMethodInvokeResult.server_transaction_id = 
			new_server_transaction_id();
		(void) start_thread(1, wsp_http_thread, new_event, 0);
	},
	REQUESTING)

ROW(REQUESTING,
	SMethodInvokeResult,
	1,
	{
		WTPEvent *wtp_event;
		
		/* Send TR-Invoke.res to WTP */
		wtp_event = wtp_event_create(TRInvokeResponse);
		wtp_event->TRInvokeResponse.tid = e->machine->tid;
		wtp_event->TRInvokeResponse.exit_info = NULL;
		wtp_event->TRInvokeResponse.exit_info_present = 0;
		debug("wap.wsp", 0, "WSP: sending TR-Invoke.res event to WTP");
		wtp_handle_event(e->machine, wtp_event);
	},
	PROCESSING)

ROW(PROCESSING,
	SMethodResultRequest,
	1,
	{
		WTPEvent *wtp_event;

		/* Send TR-Result.req to WTP */
		wtp_event = wtp_event_create(TRResultRequire);
		wtp_event->TRResultRequire.tid = e->machine->tid;
		wtp_event->TRResultRequire.user_data = 
			make_reply_pdu(e->status, e->response_type,
					e->response_body);
		debug("wap.wsp", 0, "WSP: sending TR-Result.req event to WTP");
		wtp_handle_event(e->machine, wtp_event);
	},
	REPLYING)

ROW(REPLYING,
	TRResultConfirmation,
	1,
	{
		--sm->n_methods;
	},
	CONNECTED)

#undef ROW
#undef STATE_NAME
