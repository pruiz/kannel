/*
 *Macro calls to generate rows of the state table. See the documentation for
 *guidance how to use and update these.
 *
 *By Aarno Syvänen for WapIT Ltd.
 */

ROW(LISTEN,
    RcvInvoke,
    ((event->RcvInvoke.tcl == 2 || event->RcvInvoke.tcl == 1) &&
    event->RcvInvoke.up_flag == 1 && wtp_tid_is_valid(event->RcvInvoke.tid)),
    {
     machine->u_ack=1;
     current_primitive=TRInvokeIndication;
     wsp_event=pack_wsp_event(current_primitive, event, machine);
     if (wsp_event == NULL)
        goto mem_error;
     wtp_timer_start(timer, L_A_WITH_USER_ACK, machine, event); 
    },
    INVOKE_RESP_WAIT)

ROW(INVOKE_RESP_WAIT,
    RcvInvoke,
    1,
    { },
    INVOKE_RESP_WAIT)

#undef ROW
