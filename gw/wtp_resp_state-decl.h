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
 * {} means that the event in question will be ignored (of course, the state 
 * of the machine can change). 
 *
 * Commenting the state table is perhaps best done by pointing out how various 
 * services provided by WTP contribute rows to the state table.
 *
 * Normal transaction goes as follows (timers excluded):
 *        - WTP get an invoke pdu from the peer. WTP does TR-Invoke.ind (trans-
 *          mitting to WSP its PDU) and the state changes to INVOKE_RESP_WAIT
 *        - WSP does TR-Invoke.res, telling that it has handled the 
 *          indication. 
 *          The state changes to RESULT_WAIT.
 *        - WSP tells that it has results from the content server, or reply 
 *          pdu to send. It does TR-Result.req. State changes to 
 *          RESULT_RESP_WAIT. 
 *        - WTP gets acknowledgement from the peer. It generates TR_Result.cnf
 *          and state changes to LISTEN. The transaction is over.
 *
 * Retransmission until acknowledgement guarantees reliability of the trans-
 * action, if the peer stays up. It is implemented by using retransmissions 
 * controlled by timers and counters. There are two kind of timers, retrans-
 * mission and acknowledgement timers. (Actually, there is one timer 
 * iniatilised with two intervals. But let us keep the language simple). 
 * These are used in concert with corresponding counters, RCR (retransmission 
 * counter) and AEC (acknowledgement expiration counter). AEC counts expired 
 * acknowledgement intervals.
 *
 * WTP starts an acknowledgement timer when it waits a WSP acknowledgement, 
 * and retransmission timer when it sends something. So when the acknowledge_
 * ment timer expires, the action is to increment AEC, and when the retrans-
 * mission timer expires, the action is to resend a packet. (Note, however, 
 * the chapter concerning user acknowledgement.)
 *
 * WTP ignores invoke pdus having same tid as the current transaction. This 
 * quarantees rejection of the duplicates. Note, however, how reliability is 
 * achieved when WTP is doing tid verification (next chapter).
 *
 * Tid verification is done if tid validation fails (which happens when the 
 * message is a duplicate or when tid wrapping-up could confuse the protocol).
 * In this case, the state changes to TIDOK_WAIT. WSP is indicated only after 
 * an acknowledgement is received. After a negative answer (Abort PDU) the 
 * transaction is teared down. Reliablity is quaranteed by resending, which 
 * happens when WTP receives a resended invoke pdu, when its state TIDOK_WAIT.
 * Abort pdu now means a negative answer to a question "have you a transaction
 * having tid included in the tid verification message". So there is no need 
 * to indicate WSP.
 *
 * Error handling is mostly done before feeding an event to the state machine. 
 * However, when a pdu with an illegal header (header WTP does not understand)
 * is received, this is an special kind of event, because its handling depends
 * of the state. WTP must allways send an abort pdu. If a transaction is 
 * established, it must be teared down. If WSP has been indicated about a 
 * transaction, WTP must do TR-Abort.ind.
 *
 * There is two kind of aborts: by the peer, when it send abort pdu and by the 
 * wsp, when it does a primitive TR-Abort.req. When WSP does an abort, WTP 
 * must send an abort pdu to the peer; when WTP receives an abort, WSP must be
 * indicated (note, however, the special meaning abort pdu has in tid 
 * verification; see the relevant chapter).
 *
 * User acknowledgement means that WTP waits WSP (which in most cases is WTP
 * user) acknowledgement, instead of doing it by itself. This means, that if 
 * user acknowledgement flag is off, WTP sends an ack pdu when acknowledgement
 * timer expires.
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
    (event->u.RcvInvoke.tcl == 2 || event->u.RcvInvoke.tcl == 1) &&
     wtp_tid_is_valid(event, resp_machine) == ok,
    {
     resp_machine->u_ack = event->u.RcvInvoke.up_flag;
     resp_machine->tcl = event->u.RcvInvoke.tcl;

     wsp_event = create_tr_invoke_ind(resp_machine, 
         event->u.RcvInvoke.user_data);
     wsp_session_dispatch_event(wsp_event);

     start_timer_A(resp_machine); 
     resp_machine->ack_pdu_sent = 0;
    },
    INVOKE_RESP_WAIT)

ROW(LISTEN,
    RcvInvoke,
    (event->u.RcvInvoke.tcl == 2 || event->u.RcvInvoke.tcl == 1) &&
     (wtp_tid_is_valid(event, resp_machine) == fail || 
     wtp_tid_is_valid(event, resp_machine) == no_cached_tid),
    { 
     wtp_send_ack(TID_VERIFICATION, resp_machine->rid, resp_machine->tid, 
                  resp_machine->addr_tuple);
     
     resp_machine->u_ack = event->u.RcvInvoke.up_flag;
     resp_machine->tcl = event->u.RcvInvoke.tcl;
     resp_machine->invoke_indication = create_tr_invoke_ind(resp_machine, 
                                       event->u.RcvInvoke.user_data);
     debug("wap.wtp", 0, "WTP_STATE: generating invoke indication, tid being 
           invalid");
    },
    TIDOK_WAIT)

/*
 * Do not change state when class 0 message is received.
 */
ROW(LISTEN,
    RcvInvoke,
    event->u.RcvInvoke.tcl == 0,
    {
     wsp_event = create_tr_invoke_ind(resp_machine, 
         event->u.RcvInvoke.user_data);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)

ROW(LISTEN,
    RcvErrorPDU,
    1,
    { 
     wtp_send_abort(PROVIDER, PROTOERR, resp_machine->tid, 
                    resp_machine->addr_tuple);
    },
    LISTEN)

/*
 * We must cache the newly accepted tid item, otherwise every tid after a 
 * suspected one will be validated.
 */
ROW(TIDOK_WAIT,
    RcvAck,
    (resp_machine->tcl == 2 || resp_machine->tcl == 1) && 
     event->u.RcvAck.tid_ok == 1,
    { 
     wsp_event = wap_event_duplicate(resp_machine->invoke_indication);
     wsp_session_dispatch_event(wsp_event);
     
     wtp_tid_set_by_machine(resp_machine, event->u.RcvAck.tid);

     start_timer_A(resp_machine); 
     resp_machine->ack_pdu_sent = 0;
    },
    INVOKE_RESP_WAIT)

/*
 * Here we just abort tranaction. Because wtp machines are destroyed when their
 * state return to LISTEN, there is no need to do anything here.
 */
ROW(TIDOK_WAIT,
    RcvAbort,
    1,
    { },
    LISTEN)

ROW(TIDOK_WAIT,
    RcvInvoke,
    event->u.RcvInvoke.rid == 0,
    { },
    TIDOK_WAIT)

/*
 * Because the phone sends invoke again, previous ack was dropped by the 
 * bearer.
 */
ROW(TIDOK_WAIT,
    RcvInvoke,
    event->u.RcvInvoke.rid == 1,
    { 
     wtp_send_ack(TID_VERIFICATION, resp_machine->rid, resp_machine->tid, 
                  resp_machine->addr_tuple); 
    },
    TIDOK_WAIT)

ROW(TIDOK_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_send_abort(PROVIDER, PROTOERR, resp_machine->tid, 
                    resp_machine->addr_tuple);
    },
    LISTEN)

/*
 * Ignore receiving invoke, when the state of the resp_machine is INVOKE_RESP_
 * WAIT. (Always (1) do nothing ({ }).)
 */
ROW(INVOKE_RESP_WAIT,
    RcvInvoke,
    1,
    { },
    INVOKE_RESP_WAIT)

ROW(INVOKE_RESP_WAIT,
    TR_Invoke_Res,
    resp_machine->tcl == 2,
    { 
     start_timer_A(resp_machine); 
    },
    RESULT_WAIT)

ROW(INVOKE_RESP_WAIT,
    RcvAbort,
    1,
    {
     wsp_event = create_tr_abort_ind(resp_machine, 
         event->u.RcvAbort.abort_reason);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)

ROW(INVOKE_RESP_WAIT,
    TR_Abort_Req,
    1,
    { 
     wtp_send_abort(USER, event->u.TR_Abort_Req.abort_reason,
                    resp_machine->tid, resp_machine->addr_tuple); 
    },
    LISTEN)

ROW(INVOKE_RESP_WAIT,
    TR_Result_Req,
    1,
    {
     resp_machine->rcr = 0;

     start_timer_R(resp_machine);
     msg_destroy(resp_machine->result);
     resp_machine->rid = 0;
     resp_machine->result = wtp_send_result(resp_machine, event);
     resp_machine->rid = 1;
    },
    RESULT_RESP_WAIT)

/*
 * Conditions below do not correspond wholly ones found from the spec. (If 
 * they does, user acknowledgement flag would never be used by the protocol, 
 * which cannot be the original intention.) 
 * User acknowledgement flag is used following way: if it is on, WTP does not
 * send an acknowledgement (user acknowledgement in form of TR-Invoke.res or 
 * TR-Result.req instead of provider acknowledgement is awaited); if it is 
 * off, WTP does this. IMHO, specs support this exegesis: there is condition 
 * Uack == False && class == 2 with action send ack pdu. In addition, WSP 
 * 8.3.1 says " When [user acknowledgement] is enabled WTP provider does not
 * respond to a received message until after WTP user has confirmed the 
 * indication service primitive by issuing the response primitive".
 */
ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    resp_machine->aec < AEC_MAX && resp_machine->tcl == 2 && 
    resp_machine->u_ack == 1,
    { 
     ++resp_machine->aec;
     start_timer_A(resp_machine);
    },
    INVOKE_RESP_WAIT)

ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    resp_machine->aec == AEC_MAX,
    {
     wtp_send_abort(PROVIDER, NORESPONSE, resp_machine->tid, 
                    resp_machine->addr_tuple); 
     wsp_event = create_tr_abort_ind(resp_machine, PROTOERR);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)

ROW(INVOKE_RESP_WAIT,
    TimerTO_A,
    (resp_machine->tcl == 2 && resp_machine->u_ack == 0),
    { 
     wtp_send_ack(ACKNOWLEDGEMENT, resp_machine->rid, resp_machine->tid, 
                  resp_machine->addr_tuple);
     resp_machine->ack_pdu_sent = 1;
    },
    RESULT_WAIT)

ROW(INVOKE_RESP_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_send_abort(PROVIDER, PROTOERR, resp_machine->tid, 
                    resp_machine->addr_tuple); 
     
     wsp_event = create_tr_abort_ind(resp_machine, PROTOERR);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)

ROW(RESULT_WAIT,
    TR_Result_Req,
    1,
    {
     resp_machine->rcr = 0;

     start_timer_R(resp_machine);

     msg_destroy(resp_machine->result);
     resp_machine->rid = 0;
     resp_machine->result = wtp_send_result(resp_machine, event);
     resp_machine->rid = 1;
    },
    RESULT_RESP_WAIT)

ROW(RESULT_WAIT,
    RcvAbort,
    1,
    {
     wsp_event = create_tr_abort_ind(resp_machine, 
         event->u.RcvAbort.abort_reason);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)

ROW(RESULT_WAIT,
    RcvInvoke,
    event->u.RcvInvoke.rid == 0,
    { },
    RESULT_WAIT)

ROW(RESULT_WAIT,
    RcvInvoke,
    event->u.RcvInvoke.rid == 1 && resp_machine->ack_pdu_sent == 0,
    { },
    RESULT_WAIT)

ROW(RESULT_WAIT,
    RcvInvoke,
    event->u.RcvInvoke.rid == 1 && resp_machine->ack_pdu_sent == 1,
    {
     wtp_send_ack(ACKNOWLEDGEMENT, resp_machine->rid, 
                  resp_machine->tid, resp_machine->addr_tuple);
    },
    RESULT_WAIT)

ROW(RESULT_WAIT,
    TR_Abort_Req,
    1,
    { 
     wtp_send_abort(USER, event->u.TR_Abort_Req.abort_reason, 
                    resp_machine->tid, resp_machine->addr_tuple); 
    },
    LISTEN)

ROW(RESULT_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_send_abort(PROVIDER, PROTOERR, resp_machine->tid, 
                    resp_machine->addr_tuple); 
     
     wsp_event = create_tr_abort_ind(resp_machine, PROTOERR);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)
   
/*
 * This state follows two possible ones: INVOKE_RESP_WAIT & TR-Invoke.res and 
 * INVOKE_RESP_WAIT & TimerTO_A & Class == 2 & Uack == FALSE. Contrary what 
 * spec says, in first case we are now sending first time. 
 */
ROW(RESULT_WAIT,
    TimerTO_A,
    1,
    { 
     wtp_send_ack(ACKNOWLEDGEMENT, resp_machine->rid, resp_machine->tid, 
                  resp_machine->addr_tuple);
     resp_machine->ack_pdu_sent = 1;
    },
    RESULT_WAIT)

/*
 * A duplicate ack(tidok) caused by a heavy load (the original changed state
 * from TIDOK_WAIT).
 */
ROW(RESULT_WAIT,
    RcvAck,
    event->u.RcvAck.tid_ok,
    {},
    RESULT_WAIT)

ROW(RESULT_RESP_WAIT,
    RcvAck,
    1,
    {
     wsp_event = create_tr_result_cnf(resp_machine);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    RcvAbort,
    1,
    {
     wsp_event = create_tr_abort_ind(resp_machine, 
         event->u.RcvAbort.abort_reason);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    TR_Abort_Req,
    1,
    { 
     wtp_send_abort(USER, event->u.TR_Abort_Req.abort_reason, 
                    resp_machine->tid, resp_machine->addr_tuple); 
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    TimerTO_R,
    resp_machine->rcr < MAX_RCR,
    {
     start_timer_R(resp_machine);
     wtp_resend(resp_machine->result, resp_machine->rid);
     ++resp_machine->rcr;
    },
    RESULT_RESP_WAIT)

ROW(RESULT_RESP_WAIT,
    TimerTO_R,
    resp_machine->rcr == MAX_RCR,
    {
     wsp_event = create_tr_abort_ind(resp_machine, NORESPONSE);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)

ROW(RESULT_RESP_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_send_abort(PROVIDER, PROTOERR, resp_machine->tid, 
                    resp_machine->addr_tuple); 
      
     wsp_event = create_tr_abort_ind(resp_machine, PROTOERR);
     wsp_session_dispatch_event(wsp_event);
    },
    LISTEN)

#undef ROW
#undef STATE_NAME
