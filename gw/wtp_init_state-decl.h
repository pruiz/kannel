/*
 * wtp_init_state.h: Macro calls for implementing wtp iniator state tables
 * See documentation for guidance how to use and update these.
 *
 * Only classes 0 and 1 are implemented. State NULL is called INIATOR_NULL_
 * STATE. 1 in the action field means that action is unconditional.
 *
 * Class 0 service is here a stateless invoke message (used for disconnection
 * or unconfirmed push).
 *
 * Basic class 1 transaction, without timers, is following:
 *               - iniator sends an invoke message to the responder
 *               - responder acknowledges it, with an pdu with tid verification
 *                 off (if it is on, we have a tid verification transaction, 
 *                 see below).
 *
 * Retransmission until acknowledgement is implemented using timers and 
 * retransmission counters. When the iniator sends an invoke it starts a timer.
 * When it expires, it resends the packet (either ack or invoke), until counter
 * reaches the maximum value. Then the transaction is aborted.
 *
 * If user acknowledgement is on, timers have different values.
 *
 * When the iniator aborts the transaction, it sends an abort pdu. When the
 * responder does it, the iniator wtp user is indicated.
 *
 * Tid verification in the iniator means answering the question posed by the
 * responder: "Have you an outstanding transaction having this tid". If we do
 * not have it, we have already, before feeding the event into the state 
 * machine, sended an abort with reason INVALIDTID. So here we answer to  an
 * ack pdu with tidve-flag set with an ack pdu with tidok-flag set. See WTP
 * 5.6, table 2; WTP 8.9; WTP 9.3.4.1.
 *
 * By Aarno Syvänen for Wapit Ltd. 
 */

INIT_STATE_NAME(INIATOR_NULL_STATE)
INIT_STATE_NAME(INIATOR_RESULT_WAIT)

/*
 * We do not use transaction class 2 here: Server is iniator only when it is 
 * pushing (class 1 or class 0) or disconnecting (class 0). First and second 
 * rows are similar, with exception of timer period.
 */
ROW(INIATOR_NULL_STATE,
    TR_Invoke_Req,
    event->u.TR_Invoke_Req.tcl == 1,
    {
/*
 * A special counter is used for storing value used (1) for tidnew flag when
 * restarting (See WTP 8.8.3.2)
 */
     init_machine->tidnew = counter_value(tid_new_counter);
     
     msg_destroy(init_machine->invoke);
     init_machine->rid = 0;
        
     init_machine->invoke = wtp_send_invoke(init_machine, event);
     init_machine->rid = 1;
/*
 * Turn the tidnew-flag off if it was on. (This can happen when tid was 
 * wrapped or when we are restarting, see WTP 8.8.3.2) 
 */     
     if (init_machine->tidnew) {
        init_machine->tidnew = 0;
        ret = counter_set(tid_new_counter, 0);
     }
     init_machine->u_ack = event->u.TR_Invoke_Req.up_flag;
     init_machine->rcr = 0;
     start_iniator_timer_R(init_machine);
    }, 
    INIATOR_RESULT_WAIT)

/*
 * No need to turn tidnew flag when sending class 0 message; tid validation is
 * not invoked in this case.
 */
ROW(INIATOR_NULL_STATE,
    TR_Invoke_Req,
    event->u.TR_Invoke_Req.tcl == 0,
    {
     
     msg_destroy(init_machine->invoke);
     
     init_machine->invoke = wtp_send_invoke(init_machine, event);
    },
    INIATOR_NULL_STATE)

ROW(INIATOR_RESULT_WAIT,
    TR_Abort_Req, 
    1,
    {
     wtp_send_abort(USER, event->u.TR_Abort_Req.abort_reason, 
                    init_machine->tid, init_machine->addr_tuple);
    },
    INIATOR_NULL_STATE)

/*
 * Neither we check transaction class here: this can only be acknowledgement of
 * class 1 transaction.
 */
ROW(INIATOR_RESULT_WAIT,
    RcvAck,
    event->u.RcvAck.tid_ok == 0,
    {
     stop_iniator_timer(init_machine->timer);

     wsp_event = create_tr_invoke_cnf(init_machine);
     push_dispatch_event(wsp_event);     
    },
    INIATOR_NULL_STATE)

/*
 * This is a positive answer to a tid verification. (Negative one being 
 * already sent by init_machine_find_or_create)
 */
ROW(INIATOR_RESULT_WAIT,
    RcvAck,
    event->u.RcvAck.tid_ok == 1,
    {
     init_machine->rid = 0;
     wtp_send_ack(TID_VERIFICATION, init_machine->rid, init_machine->tid, 
                  init_machine->addr_tuple);
     init_machine->tidok_sent = 1;

     ++init_machine->rcr;

     start_iniator_timer_R(init_machine);
    },
    INIATOR_RESULT_WAIT)

ROW(INIATOR_RESULT_WAIT,
    RcvAbort,
    1,
    {
     wsp_event = create_tr_abort_ind(init_machine, 
                 event->u.RcvAbort.abort_reason);
     push_dispatch_event(wsp_event);
    },
    INIATOR_NULL_STATE)

ROW(INIATOR_RESULT_WAIT,
    RcvErrorPDU,
    1,
    {
     wtp_send_abort(USER, PROTOERR, init_machine->tid, 
                    init_machine->addr_tuple); 

     wsp_event = create_tr_abort_ind(init_machine, PROTOERR);
     push_dispatch_event(wsp_event);
    },
    INIATOR_NULL_STATE)

ROW(INIATOR_RESULT_WAIT,
    TimerTO_R,
    init_machine->rcr < MAX_RCR && !init_machine->tidok_sent,
    {
      ++init_machine->rcr;

     start_iniator_timer_R(init_machine);

     wtp_resend(init_machine->invoke, init_machine->rid);
    },
    INIATOR_RESULT_WAIT)

ROW(INIATOR_RESULT_WAIT,
    TimerTO_R,
    init_machine->rcr < MAX_RCR && init_machine->tidok_sent,
    {
     ++init_machine->rcr;

     start_iniator_timer_R(init_machine);

     wtp_send_ack(TID_VERIFICATION, init_machine->tidok_sent, 
                  init_machine->tid, init_machine->addr_tuple);
    },
    INIATOR_RESULT_WAIT)

ROW(INIATOR_RESULT_WAIT,
    TimerTO_R,
    init_machine->rcr == MAX_RCR, 
    {
     wsp_event = create_tr_abort_ind(init_machine, NORESPONSE);
     push_dispatch_event(wsp_event);
    },
    INIATOR_NULL_STATE)

#undef ROW
#undef INIT_STATE_NAME
