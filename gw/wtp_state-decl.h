/*
 * Macro calls to generate rows of the state table. See the documentation for
 * guidance how to use and update these. 
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
 * {} means that the event in question will be ignored (of course, the state of 
 * the machine can change). 
 *
 * Commenting the state table is perhaps best done by pointing out how various 
 * services provided by WTP contribute rows to the state table.
 *
 * Normal transaction goes as follows (timers excluded):
 *        - WTP get an invoke pdu from the peer. WTP does TR-Invoke.ind (trans-
 *          mitting to WSP its PDU) and the state changes to INVOKE_RESP_WAIT
 *        - WSP does TR-Invoke.res, telling that it has handled the indication. 
 *          The state changes to RESULT_WAIT.
 *        - WSP tells that it has results from the content server, or reply pdu to
 *          send. It does TR-Result.req. State changes to RESULT_RESP_WAIT. 
 *        - WTP gets acknowledgement from the peer. It generates TR_Result.cnf
 *          and state changes to LISTEN. The transaction is over.
 *
 * Retransmission until acknowledgement guarantees reliability of the transaction,
 * if the peer stays up. It is implemented by using retransmissions controlled by
 * timers and counters. There are two kind of timers, retransmission and acknow-
 * ledgement timers. (Actually, there is one timer iniatilised with two intervals.   * But let us keep the language simple). These are used in concert with 
 * corresponding counters, RCR (retransmission counter) and AEC (acknowledgement 
 * expiration counter). AEC counts expired acknowledgement intervals.
 *
 * WTP starts an acknowledgement timer when it waits a WSP acknowledgement, and re-
 * transmission timer when it sends something. So when the acknowledgement timer
 * expires, the action is to increment AEC, and when the retransmission timer 
 * expires, the action is to resend a packet. (Note, however, the chapter concern-
 * ing user acknowledgement.)
 *
 * WTP ignores invoke pdus having same tid as the current transaction. This 
 * quarantees rejection of the duplicates. Note, however, how reliability is 
 * achieved when WTP is doing tid verification (next chapter).
 *
 * Tid verification is done if tid validation fails (which happens when the 
 * message is a duplicate or when tid wrapping-up could confuse the protocol). In 
 * this case, the state changes to TIDOK_WAIT. WSP is indicated only after an
 * acknowledgement is received. After a negative answer (Abort PDU) the trans-
 * action is teared down. Reliablity is quaranteed by resending, which happens
 * when WTP receives a resended invoke pdu, when its state TIDOK_WAIT. Abort pdu
 * now means a negative answer to a question "have you a transaction having tid
 * included in the tid verification message". So there is no need to indicate WSP.
 *
 * Error handling is mostly done before feeding an event to the state machine. 
 * However, when a pdu with an illegal header (header WTP does not understand) is
 * received, this is an special kind of event, because its handling depends of 
 * the state. WTP must allways send an abort pdu. If a transaction is established,
 * it must be teared down. If WSP has been indicated about a transaction, WTP must
 * do TR-Abort.ind.
 *
 * There is two kind of aborts: by the peer, when it send abort, pdu and by the 
 * wsp, when it does a primitive TR-Abort.req. When WSP does an abort, WTP must 
 * send an abort pdu to the peer; when WTP receives an abort, WSP must be indicat-
 * ed (note, however, the special meaning abort pdu has in tid verification; see 
 * the relevant chapter).
 *
 * User acknowledgement means that WTP waits WSP (which in most cases is WTP user)
 * acknowledgement, instead of doing it by itself. This means, that if user acknow-
 * ledgement flag is off, WTP sends an ack pdu when acknowledgement timer expires.
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
    wtp_tid_is_valid(event, machine) == ok,
    {
     machine->u_ack = event->RcvInvoke.up_flag;
     machine->tcl = event->RcvInvoke.tcl;
     current_primitive = TR_Invoke_Ind;

     wsp_event = pack_wsp_event(current_primitive, event, machine);
     wsp_dispatch_event(machine, wsp_event);

     timer_event = wtp_event_create(TimerTO_A);
     wtp_timer_start(machine->timer, L_A_WITH_USER_ACK, machine, timer_event); 
    },
    INVOKE_RESP_WAIT)

ROW(LISTEN,
    RcvInvoke,
    (event->RcvInvoke.tcl == 2 || event->RcvInvoke.tcl == 1) &&
    (wtp_tid_is_valid(event, machine) == fail || 
     wtp_tid_is_valid(event, machine) == no_cached_tid),
    { 
     machine->tid_ve = 1;
     wtp_send_ack(machine->tid_ve, machine, event);
    
     machine->u_ack = event->RcvInvoke.up_flag;
     machine->tcl = event->RcvInvoke.tcl;
     current_primitive = TR_Invoke_Ind;
     machine->invoke_indication = pack_wsp_event(current_primitive, event, machine);
     debug("wap.wtp", 0, "WTP_STAE: generating invoke indication, tid being invalid");
     machine->rid = 1;
    },
    TIDOK_WAIT)

ROW(LISTEN,
    RcvInvoke,
    event->RcvInvoke.tcl == 0,
    {
     current_primitive = TR_Invoke_Ind;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     wsp_dispatch_event(machine, wsp_event);
     wtp_machine_mark_unused(machine);
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
    TR_Abort_Req,
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
     wsp_event = wsp_event_duplicate(machine->invoke_indication);
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
    TR_Invoke_Res,
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
     current_primitive = TR_Abort_Ind;
     /*wsp_event = pack_wsp_event(current_primitive, event, machine);*/
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(INVOKE_RESP_WAIT,
    TR_Abort_Req,
    1,
    { 
     wtp_machine_mark_unused(machine);
     wtp_send_abort(event->TR_Abort_Req.abort_type, event->TR_Abort_Req.abort_reason,
                    machine, event); 
    },
    LISTEN)

/*
 * We must make two copies of the result message: one for sending and another for
 * possible resending.
 */
ROW(INVOKE_RESP_WAIT,
    TR_Result_Req,
    1,
    {
     machine->rcr = 0;

     wtp_timer_stop(machine->timer);
     timer_event = wtp_event_create(TimerTO_R);
     wtp_timer_start(machine->timer, L_R_WITH_USER_ACK, machine, timer_event);
     debug("wap.wtp", 0, "WTP: sending results");
     msg_destroy(machine->result);
     machine->result = wtp_send_result(machine, event);
     machine->rid = 1;
    },
    RESULT_RESP_WAIT)

ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    machine->aec < AEC_MAX && machine->tcl == 2 && machine->u_ack == 1,
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
     wtp_machine_mark_unused(machine);
     wtp_send_abort(PROVIDER, NORESPONSE, machine, event); 
     
     current_primitive = TR_Abort_Ind;
     /*wsp_event = pack_wsp_event(current_primitive, event, machine);*/
     /*wsp_dispatch_event(machine, wsp_event);*/
    },
    LISTEN)

/*
 * We must make two copies of the result message: one for sending and another for
 * possible resending.
 */
ROW(RESULT_WAIT,
    TR_Result_Req,
    1,
    {
     machine->rcr = 0;

     wtp_timer_stop(machine->timer);
     timer_event = wtp_event_create(TimerTO_R);
     wtp_timer_start(machine->timer, L_R_WITH_USER_ACK, machine, timer_event);
     msg_destroy(machine->result);
     machine->rid = 0;
     machine->result = wtp_send_result(machine, event);
     machine->rid = 1;
    },
    RESULT_RESP_WAIT)

ROW(RESULT_WAIT,
    RcvAbort,
    1,
    {
     current_primitive = TR_Abort_Ind;
     /*wsp_event = pack_wsp_event(current_primitive, event, machine);*/
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
    TR_Abort_Req,
    1,
    { 
     wtp_machine_mark_unused(machine);
     wtp_send_abort(event->TR_Abort_Req.abort_type, event->TR_Abort_Req.abort_reason,
                    machine, event); 
    },
    LISTEN)

ROW(RESULT_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_machine_mark_unused(machine);
     wtp_send_abort(PROVIDER, NORESPONSE, machine, event); 
     
     current_primitive = TR_Abort_Ind;
     /*wsp_event = pack_wsp_event(current_primitive, event, machine);*/
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
     current_primitive = TR_Result_Cnf;
     wsp_event = pack_wsp_event(current_primitive, event, machine);
     wsp_dispatch_event(machine, wsp_event);
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    RcvAbort,
    1,
    {
     current_primitive = TR_Abort_Ind;
     /*wsp_event = pack_wsp_event(current_primitive, event, machine);*/
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_machine_mark_unused(machine);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    TR_Abort_Req,
    1,
    { 
     wtp_machine_mark_unused(machine);
     wtp_send_abort(event->TR_Abort_Req.abort_type, event->TR_Abort_Req.abort_reason,
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
     current_primitive = TR_Abort_Ind;
     /*wsp_event = pack_wsp_event(current_primitive, event, machine);*/
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
     current_primitive = TR_Abort_Ind;
     /*wsp_event = pack_wsp_event(current_primitive, event, machine);*/
     /*wsp_dispatch_event(machine, wsp_event);*/
     wtp_machine_mark_unused(machine);
    },
    LISTEN)
#endif

ROW(RESULT_RESP_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_machine_mark_unused(machine);
     wtp_send_abort(PROVIDER, NORESPONSE, machine, event); 
      
     current_primitive = TR_Abort_Ind;
     /*wsp_event = pack_wsp_event(current_primitive, event, machine);*/
     /*wsp_dispatch_event(machine, wsp_event);*/
    },
    LISTEN)

#undef ROW
#undef STATE_NAME







