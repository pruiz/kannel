/*
 * Macro calls to generate WTP indications and confirmations and WSP events
 * (data structures represents both of them. See documentation for guidance 
 * how to use and update these.
 *
 * Note that the address five-tuple is included in fields of wtp machine.
 * 
 * By Aarno Syvänen and Lars Wirzenius for WapIt Ltd.
 */

#if !defined(INTEGER) || !defined(OCTSTR) || !defined(WTP_MACHINE) || \
	!defined(SESSION_MACHINE)
#error "wsp_events-decl.h: Some of the required macros not defined."
#endif



WSP_EVENT(TRInvokeIndication,
          {
          INTEGER(ack_type);
          OCTSTR(user_data);
          INTEGER(tcl);
          INTEGER(wsp_pdu);
          INTEGER(wsp_tid);
          WTP_MACHINE(machine);
	  })

WSP_EVENT(TRInvokeConfirmation,
          {
          OCTSTR(exit_info);
          INTEGER(exit_info_present);
          INTEGER(wsp_pdu);
          INTEGER(wsp_tid);
          WTP_MACHINE(machine);
          })

WSP_EVENT(TRResultConfirmation,
	  {
          OCTSTR(exit_info);
          INTEGER(exit_info_present);
          INTEGER(wsp_pdu);
          INTEGER(wsp_tid);
          WTP_MACHINE(machine);
          })

WSP_EVENT(TRAbortIndication,
          {
          INTEGER(abort_code);
          INTEGER(wsp_pdu);
          INTEGER(wsp_tid);
          WTP_MACHINE(machine);
          })

WSP_EVENT(TRAbortRequire,
          {
          INTEGER(abort_code);
          INTEGER(wsp_tid);
          WTP_MACHINE(machine);
          })

WSP_EVENT(SConnectResponse,
	  {
	  WTP_MACHINE(machine); /* XXX this is a kludge */
	  OCTSTR(server_headers);
	  OCTSTR(negotiated_capabilities);
	  })

WSP_EVENT(Release,
	  {
	  WTP_MACHINE(machine);
	  OCTSTR(url);
	  })

#if 0
WSP_EVENT(SMethodInvokeIndication,
	  {
	  WTP_MACHINE(machine);
	  OCTSTR(url);
	  INTEGER(method);
	  INTEGER(server_transaction_id);
	  })
#endif

WSP_EVENT(SMethodInvokeResult,
	  {
	  WTP_MACHINE(machine);
	  OCTSTR(url);
	  INTEGER(method);
	  INTEGER(server_transaction_id);
	  SESSION_MACHINE(session);
	  })

WSP_EVENT(SMethodResultRequest,
	  {
	  INTEGER(server_transaction_id);
	  INTEGER(status);
	  INTEGER(response_type);
	  OCTSTR(response_body);
	  WTP_MACHINE(machine);
	  })

#undef WSP_EVENT
#undef OCTSTR
#undef INTEGER
#undef WTP_MACHINE
#undef SESSION_MACHINE
