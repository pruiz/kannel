/*
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these. For more detailed explanation what this
 * state machine does, see a separate chapter in the documentation. (In this case,
 * very general comments are required.)  
 *
 * Macros have following arguments:
 *
 * STATE_NAME(name of a wtp machine state)
 *
 * ROW(the name of the current state,
 *     the event feeded to wtp machine,
 *     the condition for the action,
 *     {the action itself},
 *     the state wtp machine will transit)
 *
 * Condition 1 means that the action will be performed unconditionally, action
 * {} means that the event in question will be ignored. 
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
    event->RcvInvoke.up_flag == 1 && wtp_tid_is_valid(event, machine) == ok,
    {
     machine->u_ack = event->RcvInvoke.up_flag;
     machine->tcl = event->RcvInvoke.tcl;
     current_primitive = TRInvokeIndication;

     wsp_event = pack_wsp_event(current_primitive, event, machine);
     debug("wap.wtp", 0, "Sending TR-Invoke.ind to WSP, tid being valid");
     wsp_dispatch_event(machine, wsp_event);

     timer_event = wtp_event_create(TimerTO_A);
     wtp_timer_start(machine->timer, L_A_WITH_USER_ACK, machine, timer_event); 
    },
    INVOKE_RESP_WAIT)

ROW(LISTEN,
    RcvInvoke,
    (event->RcvInvoke.tcl == 2 || event->RcvInvoke.tcl == 1) &&
    event->RcvInvoke.up_flag == 1 && 
    (wtp_tid_is_valid(event, machine) == fail || 
     wtp_tid_is_valid(event, machine) == no_cached_tid),
    { 
     machine->tid_ve = 1;
     wtp_send_ack(machine->tid_ve, machine, event);
    
     machine->u_ack = event->RcvInvoke.up_flag;
     machine->tcl = event->RcvInvoke.tcl;
     current_primitive = TRInvokeIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     machine->invoke_indication = wsp_event;
     debug("wtp", 0, "generating invoke indication, tid being invalid");
     machine->rid = 1;
    },
    TIDOK_WAIT)

ROW(LISTEN,
    RcvInvoke,
    event->RcvInvoke.tcl == 0,
    {
     current_primitive = TRInvokeIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     debug("wap.wtp", 0, "RcvInvoke: generated TR-Invoke.ind for WSP");
     wsp_dispatch_event(machine, wsp_event);
    },
    LISTEN)

ROW(LISTEN,
    RcvErrorPDU,
    1,
    { 
     wtp_machine_mark_unused(machine);
     wtp_send_abort(PROVIDER, PROTOERR, machine, event);
    },
    LISTEN)

ROW(LISTEN,
    TRAbort,
    1,
    {
     wtp_machine_mark_unused(machine);
     wtp_send_abort(USER, PROTOERR, machine, event);
    },
    LISTEN)

ROW(TIDOK_WAIT,
    RcvAck,
    (machine->tcl == 2 || machine->tcl == 1) && event->RcvAck.tid_ok == 1,
    { 
     wsp_event = machine->invoke_indication;
     debug("wap.wtp", 0, "RcvAck: generated TR-Invoke.ind for WSP");
     wsp_event_dump(wsp_event);
     wsp_dispatch_event(machine, wsp_event);
     
     timer_event = wtp_event_create(TimerTO_A);
     wtp_timer_start(machine->timer, L_A_WITH_USER_ACK, machine, timer_event); 
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
    },
    TIDOK_WAIT)

ROW(TIDOK_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_send_abort(PROVIDER, PROTOERR, machine, event);
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

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
     wtp_timer_stop(machine->timer);
     timer_event = wtp_event_create(TimerTO_A);
     wtp_timer_start(machine->timer, L_A_WITH_USER_ACK, machine, timer_event); 
    },
    RESULT_WAIT)

ROW(INVOKE_RESP_WAIT,
    RcvAbort,
    1,
    {
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(INVOKE_RESP_WAIT,
    TRAbort,
    1,
    { 
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
     wtp_send_abort(event->TRAbort.abort_type, event->TRAbort.abort_reason,
                    machine, event); 
    },
    LISTEN)

/*
 * We must make two copies of the result message: one for sending and another for
 * possible resending.
 */
ROW(INVOKE_RESP_WAIT,
    TRResult,
    1,
    {
     machine->rcr = 0;

     wtp_timer_stop(machine->timer);
     timer_event = wtp_event_create(TimerTO_R);
     wtp_timer_start(machine->timer, L_R_WITH_USER_ACK, machine, timer_event);
     debug("wap.wtp", 0, "WTP: sending results");
     machine->result = wtp_send_result(machine, event); 
     machine->rid = 1;
    },
    RESULT_RESP_WAIT)

ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    machine->aec < AEC_MAX,
    { 
     ++machine->aec;
     wtp_timer_stop(machine->timer);
     timer_event = wtp_event_create(TimerTO_A);
     wtp_timer_start(machine->timer, L_A_WITH_USER_ACK, machine, timer_event);
    },
    INVOKE_RESP_WAIT)

ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    machine->aec == AEC_MAX,
    {
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
     wtp_send_abort(PROVIDER, NORESPONSE, machine, event); 
    },
    LISTEN)

ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    (machine->tcl == 2 && machine->u_ack == 0),
    { wtp_send_ack(ACKNOWLEDGEMENT, machine, event);},
    RESULT_WAIT)

ROW(INVOKE_RESP_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
     wtp_send_abort(PROVIDER, NORESPONSE, machine, event); 
     
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     /*wsp_dispatch_event(machine, wsp_event);*/
    },
    LISTEN)

/*
 * We must make two copies of the result message: one for sending and another for
 * possible resending.
 */
ROW(RESULT_WAIT,
    TRResult,
    1,
    {
     machine->rcr = 0;

     wtp_timer_stop(machine->timer);
     timer_event = wtp_event_create(TimerTO_R);
     wtp_timer_start(machine->timer, L_R_WITH_USER_ACK, machine, timer_event);

     machine->result = wtp_send_result(machine, event);
     machine->rid = 1;
    },
    RESULT_RESP_WAIT)

ROW(RESULT_WAIT,
    RcvAbort,
    1,
    {
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_timer_destroy(machine->timer);
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
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
     wtp_send_abort(event->TRAbort.abort_type, event->TRAbort.abort_reason,
                    machine, event); 
    },
    LISTEN)

ROW(RESULT_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
     wtp_send_abort(PROVIDER, NORESPONSE, machine, event); 
     
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     /*wsp_dispatch_event(machine, wsp_event);*/
    },
    LISTEN)

ROW(RESULT_WAIT,
    TimerTO_A,
    1,
    { wtp_send_ack(machine->tid_ve, machine, event);},
    RESULT_WAIT)

ROW(RESULT_RESP_WAIT,
    RcvAck,
    1,
    {
     current_primitive = TRResultConfirmation;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     wsp_dispatch_event(machine, wsp_event);
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    RcvAbort,
    1,
    {
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    TRAbort,
    1,
    { 
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
     wtp_send_abort(event->TRAbort.abort_type, event->TRAbort.abort_reason,
                    machine, event); 
    },
    LISTEN)

/* 
 * This hack will be removed when timers are properly tested, for instance, 
 * with a new version of fakewap. We just response to RcvInvoke with a resended
 * packet. 
 */ 
#if 1

ROW(RESULT_RESP_WAIT,
    RcvInvoke,
    machine->rcr < MAX_RCR,
    {
     wtp_resend_result(machine->result, machine->rid);
     ++machine->rcr;
    },
    RESULT_RESP_WAIT)

ROW(RESULT_RESP_WAIT,
    RcvInvoke,
    machine->rcr == MAX_RCR,
    {
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

/* 
 * We resend the packet, obviously the previous one did not reach the client.
 * We must still be able to handle an event RcvInvoke, when WTP machine state
 * is RESULT_RESP_WAIT. We resend only when we get a timer event - this way we 
 * can control number of resendings.
 */
#else

ROW(RESULT_RESP_WAIT,
    RcvInvoke,
    1,
    {},
    RESULT_RESP_WAIT)

ROW(RESULT_RESP_WAIT,
    TimerTO_R,
    machine->rcr < MAX_RCR,
    {
     wtp_timer_stop(machine->timer);
     timer_event = wtp_event_create(TimerTO_R);
     wtp_timer_start(machine->timer, L_R_WITH_USER_ACK, machine, timer_event);
     wtp_resend_result(machine->result, machine->rid);
     ++machine->rcr;
    },
    RESULT_RESP_WAIT)

ROW(RESULT_RESP_WAIT,
    TimerTO_R,
    machine->rcr == MAX_RCR,
    {
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)
#endif

ROW(RESULT_RESP_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_timer_destroy(machine->timer);
     wtp_machine_mark_unused(machine);
     wtp_send_abort(PROVIDER, NORESPONSE, machine, event); 
     
     current_primitive = TRAbortIndication;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     /*wsp_dispatch_event(machine, wsp_event);*/
    },
    LISTEN)

#undef ROW
#undef STATE_NAME







