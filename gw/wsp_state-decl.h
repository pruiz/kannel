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

ROW(NULL_STATE,
	TRInvokeIndication,
	e->tcl == 2 && wsp_deduce_pdu_type(e->user_data, 0) == Connect_PDU,
	{
		WSPEvent *new_event;
		WTPEvent *wtp_event;

		/* Send TR-Invoke.res to WTP */
		wtp_event = wtp_event_create(TRInvoke);
		if (wtp_event == NULL)
			panic(0, "wtp_event_create failed");
		wtp_event->TRInvoke.tid = e->machine->tid;
		wtp_event->TRInvoke.exit_info = NULL;
		wtp_event->TRInvoke.exit_info_present = 0;
		debug(0, "WSP: sending TR-Invoke.res event to WTP");
		wtp_handle_event(e->machine, wtp_event);

		sm->n_methods = 0;

		/*
		 * Send S-Connect.res to ourselves. NOTE: The spec says
		 * S-Connect.ind to the layer above WSP, but since we
		 * don't have any such layer, we'll just send .res to
		 * ourselves.
		 */
		debug(0, "WSP: sending S-Connect.res to ourselves");
		new_event = wsp_event_create(SConnectResponse);
		new_event->SConnectResponse.machine = e->machine;
		wsp_handle_event(sm, new_event);
	},
	CONNECTING)

ROW(CONNECTING,
	SConnectResponse,
	1,
	{
		WTPEvent *wtp_event;
		Octstr *pdu;
		
		/* Send Disconnect event to existing sessions for client. */

		/* Invent a new session ID since we're now the official
		 * session for this client. 
		 */
		sm->session_id = wsp_next_session_id();
		debug(0, "WSP: Session ID is %ld", sm->session_id);

		/* Make a ConnectReply PDU for WSP. */
		pdu = make_connectreply_pdu(sm->session_id);
		debug(0, "WSP: ConnectReply PDU is:");
		octstr_dump(pdu);

		/* Make a TR-Result.req event for WTP. */
		wtp_event = wtp_event_create(TRResult);
		if (wtp_event == NULL)
			panic(0, "wtp_event_create failed");
		wtp_event->TRResult.tid = wtp_tid_next();
		wtp_event->TRResult.user_data = pdu;
		debug(0, "WSP: sending TR-Result.req event to old WTPMachine");
		wtp_handle_event(e->machine, wtp_event);

		/* Release all method transactions in HOLDING state. */
		 
		error(0, "State not yet fully implemented.");
	},
	CONNECTING_2)

#if 0

ROW(CONNECTING_2,
	TRResultConfirmation,
	1, /* XXX check here that it is a Connect transaction! */
	{
		error(0, "State not yet implemented.");
	},
	CONNECTED)

#endif

#undef ROW
#undef STATE_NAME
