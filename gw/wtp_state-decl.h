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
     machine->u_ack = event->RcvInvoke.up_flag;
     machine->tcl = event->RcvInvoke.tcl;
     current_primitive = TRInvokeIndication;

     wsp_event = pack_wsp_event(current_primitive, event, machine);
     if (wsp_event == NULL)
        goto mem_error;
     debug(0, "WTP: Sending TR-Invoke.ind to WSP");
     wsp_dispatch_event(machine, wsp_event);

     timer = wtp_timer_create();
     if (timer == NULL)
        goto mem_error;
     wtp_timer_start(timer, L_A_WITH_USER_ACK, machine, event); 
    },
    INVOKE_RESP_WAIT)

ROW(LISTEN,
    RcvInvoke,
    (event->RcvInvoke.tcl == 2 || event->RcvInvoke.tcl == 1) &&
    event->RcvInvoke.up_flag == 1 && !wtp_tid_is_valid(event),
    { 
     machine->tid_ve = 1;
     wtp_send_ack(machine->tid_ve, machine, event);
     machine->ack_pdu_sent = 1;
    },
    TIDOK_WAIT)

ROW(LISTEN,
    RcvInvoke,
    event->RcvInvoke.tcl == 0,
    {
     current_primitive=TRInvokeIndication;
     wsp_event=pack_wsp_event(current_primitive, event, machine);
     if (wsp_event == NULL)
        goto mem_error;
     debug(0, "RcvInvoke: generated TR-Invoke.ind for WSP");
     wsp_dispatch_event(machine, wsp_event);
    },
    LISTEN)

ROW(TIDOK_WAIT,
    RcvAck,
    (machine->tcl == 2 || machine->tcl == 1) && event->RcvAck.tid_ok == 1,
    { 
     current_primitive=TRInvokeIndication;
     wsp_event=pack_wsp_event(current_primitive, event, machine);
     if (wsp_event == NULL)
        goto mem_error;
     debug(0, "RcvAck: generated TR-Invoke.ind for WSP");
     wsp_dispatch_event(machine, wsp_event);
     
     timer = wtp_timer_create();
     if (timer == NULL)
        goto mem_error;
     wtp_timer_start(timer, L_A_WITH_USER_ACK, machine, event); 
    },
    INVOKE_RESP_WAIT)

ROW(TIDOK_WAIT,
    RcvAbort,
    1,
    { wtp_machine_mark_unused(machine); },
    LISTEN)

ROW(TIDOK_WAIT,
    RcvInvoke,
    event->RcvInvoke.rid == 0,
    { },
    TIDOK_WAIT)

ROW(TIDOK_WAIT,
    RcvInvoke,
    event->RcvInvoke.rid == 1,
    { 
     machine->tid_ve = 1;
     wtp_send_ack(machine->tid_ve, machine, event); 
     machine->ack_pdu_sent = 1;
    },
    TIDOK_WAIT)

/*
 * Ignore receiving invoke, when the state of the machine is INVOKE_RESP_WAIT.
 * (Always (1) do nothing ({ }).)
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
     timer = wtp_timer_create();
     if (timer == NULL)
        goto mem_error;
     wtp_timer_start(timer, L_A_WITH_USER_ACK, machine, event); 
    },
    RESULT_WAIT)

ROW(INVOKE_RESP_WAIT,
    RcvAbort,
    1,
    {
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     if (wsp_event == NULL)
        goto mem_error;
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(INVOKE_RESP_WAIT,
    TRAbort,
    1,
    { 
     wtp_machine_mark_unused(machine);
     wtp_send_abort(event->TRAbort.abort_type, event->TRAbort.abort_reason,
                    machine, event); 
    },
    LISTEN)

ROW(INVOKE_RESP_WAIT,
    TRResult,
    1,
    {
     machine->rcr = 0;

     timer = wtp_timer_create();
     if (timer == NULL)
        goto mem_error;
     wtp_timer_start(timer, L_R_WITH_USER_ACK, machine, event);
     debug(0, "WTP: sending results");
     wtp_send_result(machine, event); 
     machine->rid = 1;
    },
    RESULT_RESP_WAIT)

ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    machine->aec < AEC_MAX,
    { 
     ++machine->aec;
     wtp_timer_start(timer, L_R_WITH_USER_ACK, machine, event);
    },
    INVOKE_RESP_WAIT)

ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    machine->aec == AEC_MAX,
    {
     wtp_machine_mark_unused(machine);
     wtp_send_abort(PROVIDER, NORESPONSE, machine, event); 
    },
    LISTEN)

ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    (machine->tcl == 2 && machine->u_ack == 0),
    { wtp_send_ack(ACKNOWLEDGEMENT, machine, event);},
    RESULT_WAIT)

ROW(RESULT_WAIT,
    TRResult,
    1,
    {
     machine->rcr = 0;

     timer = wtp_timer_create();
     if (timer == NULL)
        goto mem_error;
     wtp_timer_start(timer, L_R_WITH_USER_ACK, machine, event);

     wtp_send_result(machine, event); 
     machine->rid = 1;
    },
    RESULT_RESP_WAIT)

ROW(RESULT_WAIT,
    RcvAbort,
    1,
    {
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     if (wsp_event == NULL)
        goto mem_error;
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_WAIT,
    RcvInvoke,
    event->RcvInvoke.rid == 0,
    { },
    RESULT_WAIT)

ROW(RESULT_WAIT,
    RcvInvoke,
    event->RcvInvoke.rid == 1 && machine->ack_pdu_sent == 0,
    { },
    RESULT_WAIT)

ROW(RESULT_WAIT,
    RcvInvoke,
    event->RcvInvoke.rid == 1 && machine->ack_pdu_sent == 1,
    {
     machine->rid = event->RcvInvoke.rid;
     wtp_send_ack(machine->tid_ve, machine, event);
     machine->ack_pdu_sent = 1;
    },
    RESULT_WAIT)

ROW(RESULT_WAIT,
    TRAbort,
    1,
    { 
     wtp_machine_mark_unused(machine);
     wtp_send_abort(event->TRAbort.abort_type, event->TRAbort.abort_type,
                    machine, event); 
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    RcvAck,
    1,
    {
     current_primitive = TRResultConfirmation;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     if (wsp_event == NULL)
        goto mem_error;
     wsp_dispatch_event(machine, wsp_event);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    RcvAbort,
    1,
    {
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     if (wsp_event == NULL)
        goto mem_error;
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    TRAbort,
    1,
    { 
     wtp_machine_mark_unused(machine);
     wtp_send_abort(event->TRAbort.abort_type, event->TRAbort.abort_type,
                    machine, event); 
    },
    LISTEN)

#undef ROW
#undef STATE_NAME







