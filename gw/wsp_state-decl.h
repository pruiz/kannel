/*
 * wsp_state-decl.h
 *
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

STATE_NAME(NULL_STATE)

ROW(NULL_STATE,
	TRInvokeIndication,
	e->tcl == 2 && wsp_deduce_pdu_type(e->user_data, 0) == Connect_PDU,
	{
		/* XXX unpack connect pdu and store relevant info in sm */
		generate_tr_invoke_res(e);
		sm->n_methods = 0;
		generate_s_connect_ind(e);
	},
	CONNECTING)

ROW(CONNECTING,
	SConnectResponse,
	1,
	{
		disconnect_other_sessions_for_this_peer(sm);
		assign_session_id(sm);
		generate_tr_result_req(sm, ConnectReply);
		release_all_method_transactions_in_holding_state(sm);
	},
	CONNECTING_2)

ROW(CONNECTING_2,
	TRResultConfirmation,
	1, /* XXX check here that it is a Connect transaction! */
	{
	},
	CONNECTED)

#undef ROW
#undef STATE_NAME
