/*
 * wap-events-def.h - definitions for wapbox events
 *
 * This file uses a pre-processor trick to define the structure of
 * structures. See the documentation.
 *
 * Aarno Syvänen
 * Lars Wirzenius
 */


WAPEVENT(TR_Invoke_Ind,
        INTEGER(ack_type)
        OCTSTR(user_data)
        INTEGER(tcl)
	ADDRTUPLE(addr_tuple)
	INTEGER(handle)
	)

WAPEVENT(TR_Invoke_Cnf,
	INTEGER(handle)
	)

WAPEVENT(TR_Result_Cnf,
	ADDRTUPLE(addr_tuple)
	INTEGER(handle)
        )

WAPEVENT(TR_Abort_Ind,
        INTEGER(abort_code)
	ADDRTUPLE(addr_tuple)
	INTEGER(handle)
        )

WAPEVENT(S_Connect_Ind,
	ADDRTUPLE(addr_tuple)
	HTTPHEADER(client_headers)
	CAPABILITIES(requested_capabilities)
	INTEGER(session_id)
	)

WAPEVENT(S_Connect_Res,
	HTTPHEADER(server_headers)
	CAPABILITIES(negotiated_capabilities)
	INTEGER(session_id)
	)

WAPEVENT(S_Disconnect_Ind,
	INTEGER(reason_code)
	INTEGER(redirect_security)
	INTEGER(redirect_addresses)
	OCTSTR(error_headers)
	OCTSTR(error_body)
	INTEGER(session_id)
	)

WAPEVENT(S_Suspend_Ind,
	INTEGER(reason)
	INTEGER(session_id)
	)

WAPEVENT(S_Resume_Ind,
	ADDRTUPLE(addr_tuple)
	HTTPHEADER(client_headers)
	INTEGER(session_id)
	)

WAPEVENT(S_Resume_Res,
	HTTPHEADER(server_headers)
	INTEGER(session_id)
	)

WAPEVENT(Disconnect_Event,
	INTEGER(session_id)
	)

WAPEVENT(Suspend_Event,
	INTEGER(session_id)
	)

WAPEVENT(Release_Event,
	INTEGER(dummy)
	)

WAPEVENT(Abort_Event,
	INTEGER(reason)
	)

WAPEVENT(S_MethodInvoke_Ind,
	INTEGER(server_transaction_id)
	INTEGER(method)
	OCTSTR(url)
	HTTPHEADER(http_headers)
	OCTSTR(body)

	HTTPHEADER(session_headers)
	ADDRTUPLE(addr_tuple)
	INTEGER(client_SDU_size)
	INTEGER(session_id)
	)

WAPEVENT(S_MethodInvoke_Res,
	INTEGER(server_transaction_id)
	INTEGER(session_id)
	)

WAPEVENT(S_MethodResult_Req,
	INTEGER(server_transaction_id)
	INTEGER(status)
	HTTPHEADER(response_headers)
	OCTSTR(response_body)
	INTEGER(session_id)
	)

WAPEVENT(S_MethodResult_Cnf,
	INTEGER(server_transaction_id)
	INTEGER(session_id)
	)

WAPEVENT(S_MethodAbort_Ind,
	INTEGER(transaction_id)
	INTEGER(reason)
	INTEGER(session_id)
	)

WAPEVENT(RcvInvoke,
	OCTSTR(user_data)
	INTEGER(tcl)
	INTEGER(tid)
	INTEGER(tid_new)
	INTEGER(rid)
	INTEGER(up_flag)
	INTEGER(no_cache_supported)
	INTEGER(version)
	INTEGER(gtr)
	INTEGER(ttr)
	ADDRTUPLE(addr_tuple)
	)

WAPEVENT(RcvAbort,
	INTEGER(tid)
	INTEGER(abort_type)
	INTEGER(abort_reason)
	ADDRTUPLE(addr_tuple)
	)

WAPEVENT(RcvAck,
	INTEGER(tid)
	INTEGER(tid_ok)
	INTEGER(rid)
	ADDRTUPLE(addr_tuple)
	)

WAPEVENT(TR_Invoke_Req,
	ADDRTUPLE(addr_tuple)
	INTEGER(up_flag)
	OCTSTR(user_data)
	INTEGER(tcl)
	INTEGER(handle)
	)

WAPEVENT(TR_Invoke_Res,
	INTEGER(handle)
	)

WAPEVENT(TR_Result_Req,
	OCTSTR(user_data)
	INTEGER(handle)
	)

WAPEVENT(TR_Abort_Req,
	INTEGER(abort_type)
	INTEGER(abort_reason)
	INTEGER(handle)
	) 

WAPEVENT(TimerTO_A,
	INTEGER(handle)
	)

WAPEVENT(TimerTO_R,
	INTEGER(handle)
	)

WAPEVENT(TimerTO_W,
	INTEGER(handle)
	)

WAPEVENT(RcvErrorPDU,
	INTEGER(tid)
	ADDRTUPLE(addr_tuple)
	)

WAPEVENT(S_Unit_MethodInvoke_Ind,
	ADDRTUPLE(addr_tuple)
	INTEGER(transaction_id)
	INTEGER(method)
	OCTSTR(request_uri)
	HTTPHEADER(request_headers)
	OCTSTR(request_body)
	)

WAPEVENT(S_Unit_MethodResult_Req,
	ADDRTUPLE(addr_tuple)
	INTEGER(transaction_id)
	INTEGER(status)
	HTTPHEADER(response_headers)
	OCTSTR(response_body)
	)

#undef WAPEVENT
#undef OCTSTR
#undef INTEGER
#undef HTTPHEADER
#undef ADDRTUPLE
#undef CAPABILITIES
