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

		/* TR-Invoke.res */
		wtp_event = wtp_event_create(TRInvoke);
		if (wtp_event == NULL)
			panic(0, "wtp_event_create failed");
		wtp_event->TRInvoke.tid = e->machine->tid;
		wtp_event->TRInvoke.exit_info = NULL;
		wtp_event->TRInvoke.exit_info_present = 0;
		debug(0, "WSP: sending event to WTP:");
		wtp_event_dump(wtp_event);
		debug(0, "WSP: event will be sent to the following machine:");
		wtp_machine_dump(e->machine);
		wtp_handle_event(e->machine, wtp_event);

		sm->n_methods = 0;
		new_event = wsp_event_create(SConnectResponse);
		wsp_handle_event(sm, new_event);
	},
	CONNECTING)

ROW(CONNECTING,
	SConnectResponse,
	1,
	{
		error(0, "State not yet implemented.");
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
