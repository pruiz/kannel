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
          INTEGER(wsp_pdu)
          INTEGER(wsp_tid)
          WTP_MACHINE(machine)
	  )

WAPEVENT(TR_Invoke_Cnf,
          OCTSTR(exit_info)
          INTEGER(exit_info_present)
          INTEGER(wsp_pdu)
          INTEGER(wsp_tid)
          WTP_MACHINE(machine)
          )

WAPEVENT(TR_Result_Cnf,
          OCTSTR(exit_info)
          INTEGER(exit_info_present)
          INTEGER(wsp_pdu)
          INTEGER(wsp_tid)
          WTP_MACHINE(machine)
          )

WAPEVENT(TR_Abort_Ind,
          INTEGER(abort_code)
          INTEGER(wsp_pdu)
          INTEGER(wsp_tid)
          WTP_MACHINE(machine)
          )

WAPEVENT(S_Connect_Res,
	  WTP_MACHINE(machine) /* XXX this is a kludge */
	  OCTSTR(server_headers)
	  OCTSTR(negotiated_capabilities)
	  )

WAPEVENT(Release,
	  WTP_MACHINE(machine)
	  OCTSTR(url)
	  HTTPHEADER(http_headers)
	  )

WAPEVENT(S_MethodInvoke_Ind,
	  WTP_MACHINE(machine)
	  OCTSTR(url)
	  INTEGER(method)
	  HTTPHEADER(http_headers)
	  INTEGER(server_transaction_id)
	  SESSION_MACHINE(session)
	  )

WAPEVENT(S_MethodInvoke_Res,
	  WTP_MACHINE(machine)
	  )

WAPEVENT(S_MethodResult_Req,
	  INTEGER(server_transaction_id)
	  INTEGER(status)
	  INTEGER(response_type)
	  OCTSTR(response_body)
	  WTP_MACHINE(machine)
	  )

WAPEVENT(RcvInvoke,
      OCTSTR(user_data)
      OCTSTR(exit_info)
      INTEGER(tcl)
      INTEGER(tid)
      INTEGER(tid_new)
      INTEGER(rid)
      INTEGER(up_flag)
      INTEGER(exit_info_present)
      INTEGER(no_cache_supported)
      OCTSTR(client_address)
      INTEGER(client_port)
      OCTSTR(server_address)
      INTEGER(server_port)
      )

WAPEVENT(RcvAbort,
      INTEGER(tid)
      INTEGER(abort_type)
      INTEGER(abort_reason)
      OCTSTR(client_address)
      INTEGER(client_port)
      OCTSTR(server_address)
      INTEGER(server_port)
      )

WAPEVENT(RcvAck,
      INTEGER(tid)
      INTEGER(tid_ok)
      INTEGER(rid)
      OCTSTR(client_address)
      INTEGER(client_port)
      OCTSTR(server_address)
      INTEGER(server_port)
      )

WAPEVENT(TR_Invoke_Req,
      OCTSTR(source_address)
      INTEGER(source_port)
      OCTSTR(destination_address)
      INTEGER(destination_port)
      INTEGER(ack_type)
      INTEGER(tcl)
      OCTSTR(user_data)
      )

WAPEVENT(TR_Invoke_Res,
      INTEGER(tid)
      OCTSTR(exit_info)
      INTEGER(exit_info_present)
      INTEGER(mid);
      )

WAPEVENT(TR_Result_Req,
      INTEGER(tid)
      OCTSTR(user_data)
      INTEGER(mid)
      )

WAPEVENT(TR_Abort_Req,
     INTEGER(tid)
     INTEGER(abort_type)
     INTEGER(abort_reason)
     INTEGER(mid)
     ) 

WAPEVENT(TimerTO_A,
     INTEGER(dummy)
     )

WAPEVENT(TimerTO_R,
     INTEGER(dummy)
     )

WAPEVENT(TimerTO_W,
     INTEGER(dummy)
     )

WAPEVENT(RcvErrorPDU,
     INTEGER(tid)
      OCTSTR(client_address)
      INTEGER(client_port)
      OCTSTR(server_address)
      INTEGER(server_port)
     )


#undef WAPEVENT
#undef OCTSTR
#undef INTEGER
#undef WTP_MACHINE
#undef SESSION_MACHINE
#undef HTTPHEADER
