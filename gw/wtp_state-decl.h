/*
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these.
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

STATE_NAME(LISTEN)
STATE_NAME(TIDOK_WAIT)
STATE_NAME(INVOKE_RESP_WAIT)
STATE_NAME(RESULT_WAIT)
STATE_NAME(RESULT_RESP_WAIT)
STATE_NAME(WAIT_TIMEOUT)

ROW(LISTEN,
    RcvInvoke,
    (event->RcvInvoke.tcl == 2 || event->RcvInvoke.tcl == 1) &&
    event->RcvInvoke.up_flag == 1 && wtp_tid_is_valid(event),
    {
     machine->u_ack=event->RcvInvoke.up_flag;
     machine->tcl=event->RcvInvoke.tcl;
     current_primitive=TRInvokeIndication;
     machine->rid=0;
     machine->result_pdu_sent=1;
     wsp_event=pack_wsp_event(current_primitive, event, machine);
     if (wsp_event == NULL)
        goto mem_error;
     wtp_timer_start(timer, L_A_WITH_USER_ACK, machine, event); 
    },
    INVOKE_RESP_WAIT)
/*
 *Ignore receiving invoke, when the state of the machine is INVOKE_RESP_WAIT.
 *(Always (1) do nothing ({ }).)
 */
ROW(INVOKE_RESP_WAIT,
    RcvInvoke,
    1,
    { },
    INVOKE_RESP_WAIT)

ROW(INVOKE_RESP_WAIT,
    TRInvoke,
    machine->tcl == 2,
    { 
     wtp_timer_stop(timer);
     wtp_timer_start(timer, L_A_WITH_USER_ACK, machine, event); 
    },
    RESULT_WAIT)

ROW(INVOKE_RESP_WAIT,
    RcvAbort,
    1,
    {
     current_primitive=TRAbortIndication;
     wsp_event=pack_wsp_event(current_primitive, event, machine);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_WAIT,
    TRResult,
    1,
    {
     machine->rcr=0;
     wtp_timer_start(timer, L_R_WITH_USER_ACK, machine, event);
     wtp_send_result(machine, event); 
    },
    RESULT_RESP_WAIT)

ROW(RESULT_WAIT,
    RcvAbort,
    1,
    {
     current_primitive=TRAbortIndication;
     wsp_event=pack_wsp_event(current_primitive, event, machine);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    RcvAck,
    1,
    {
     current_primitive=TRResultConfirmation;
     wsp_event=pack_wsp_event(current_primitive, event, machine);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    RcvAbort,
    1,
    {
     current_primitive=TRAbortIndication;
     wsp_event=pack_wsp_event(current_primitive, event, machine);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

#undef ROW
#undef STATE_NAME





