/*
 * Macro calls to generate WTP indications and confirmations and WSP events
 * (data structures represents both of them. See documentation for guidance 
 * how to use and update these.
 *
 * Note that the address five-tuple is included in fields of wtp machine.
 * 
 * By Aarno Syvänen for WapIt Ltd.
 */

WSP_EVENT(TRInvokeIndication,
          {
          INTEGER(ack_type);
          OCTSTR(user_data);
          INTEGER(tcl);
          INTEGER(wsp_pdu);
          INTEGER(wsp_tid);
          MACHINE(machine);
	  })

WSP_EVENT(TRInvokeConfirmation,
          {
          OCTSTR(exit_info);
          INTEGER(exit_info_present);
          INTEGER(wsp_pdu);
          INTEGER(wsp_tid);
          MACHINE(machine);
          })

WSP_EVENT(TRResultConfirmation,
	  {
          OCTSTR(exit_info);
          INTEGER(exit_info_present);
          INTEGER(wsp_pdu);
          INTEGER(wsp_tid);
          MACHINE(machine);
          })

WSP_EVENT(TRAbortIndication,
          {
          INTEGER(abort_code);
          INTEGER(wsp_pdu);
          INTEGER(wsp_tid);
          MACHINE(machine);
          })

WSP_EVENT(TRInvokeResponse,
          {
          OCTSTR(exit_info);
          INTEGER(exit_info_present);
          INTEGER(wsp_tid);
          })

WSP_EVENT(TRResultRequire,
          {
          OCTSTR(user_data);
          INTEGER(wsp_tid);
          })

WSP_EVENT(TRAbortRequire,
          {
          INTEGER(abort_code);
          INTEGER(wsp_tid);
          })

WSP_EVENT(SConnectResponse,
	  {
	  OCTSTR(server_headers);
	  OCTSTR(negotiated_capabilities);
	  })

#undef WSP_EVENT
#undef OCTSTR
#undef INTEGER
#undef MACHINE
