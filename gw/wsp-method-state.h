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

ROW(NULL_METHOD,
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
		new_event->u.S_MethodInvoke_Ind.mid = e->mid;
		new_event->u.S_MethodInvoke_Ind.tid = e->tid;
		new_event->u.S_MethodInvoke_Ind.url = octstr_duplicate(e->url);
		new_event->u.S_MethodInvoke_Ind.method = Get_PDU;
		new_event->u.S_MethodInvoke_Ind.http_headers = 
			http_header_duplicate(e->http_headers);
		new_event->u.S_MethodInvoke_Ind.server_transaction_id = 
			new_server_transaction_id();
		new_event->u.S_MethodInvoke_Ind.msmid = msm->id;
		new_event->u.S_MethodInvoke_Ind.session_headers = 
			http_header_duplicate(e->session_headers);
		new_event->u.S_MethodInvoke_Ind.addr_tuple = 
			wap_addr_tuple_duplicate(e->addr_tuple);
		new_event->u.S_MethodInvoke_Ind.session_id = e->session_id;
		new_event->u.S_MethodInvoke_Ind.client_SDU_size = 
			e->client_SDU_size;
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
		wtp_event->u.TR_Invoke_Res.tid = e->tid;
		wtp_event->u.TR_Invoke_Res.mid = e->mid;
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
			wsp_convert_http_status_to_wsp_status(e->status);
		new_pdu->u.Reply.headers = 
			wsp_encode_http_headers(e->response_type);
		new_pdu->u.Reply.data = octstr_duplicate(e->response_body);

		/* Send TR-Result.req to WTP */
		wtp_event = wap_event_create(TR_Result_Req);
		wtp_event->u.TR_Result_Req.tid = e->tid;
		wtp_event->u.TR_Result_Req.user_data = wsp_pdu_pack(new_pdu);
		wtp_event->u.TR_Result_Req.mid = e->mid;
		wtp_dispatch_event(wtp_event);
		wsp_pdu_destroy(new_pdu);
	},
	REPLYING)

ROW(REPLYING,
	TR_Result_Cnf,
	1,
	{
	},
	NULL_METHOD)

#undef ROW
#undef STATE_NAME
