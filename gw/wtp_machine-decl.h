
/*
 *wtp_machine-decl.h. Macro call for defining WTP state machine. See the 
 *architecture document for guidance how to use and update it.
 *
 *By Aarno Syvänen for WapIT Ltd.
 */

#if HAVE_THREADS
MACHINE( 
        {
        INTEGER(in_use);
        INTEGER(state);
        INTEGER(send_tid);
        INTEGER(rcv_tid);
        INTEGER(tid);
        OCTSTR(source_address);
        INTEGER(source_port);
        OCTSTR(destination_address);
        INTEGER(destination_port);
        INTEGER(tcl);
        INTEGER(aec);
        INTEGER(rcr);
        INTEGER(u_ack);
        INTEGER(hold_on);
        INTEGER(ack_pdu_sent);
        TIMER(timer_data);
        QUEUE(event_queue);
        MUTEX(mutex);
        NEXT(next);
        })
#else
MACHINE( 
        {
        INTEGER(in_use);
        INTEGER(state);
        INTEGER(send_tid);
        INTEGER(rcv_tid);
        INTEGER(tid);
        OCTSTR(source_address);
        INTEGER(source_port);
        OCTSTR(destination_address);
        INTEGER(destination_port);
        INTEGER(tcl);
        INTEGER(aec);
        INTEGER(rcr);
        INTEGER(u_ack);
        INTEGER(hold_on);
        INTEGER(ack_pdu_sent);
        TIMER(timer_data);
        QUEUE(event_queue);
        INTEGER(mutex);
        NEXT(next);
        })
#endif

#undef MACHINE
#undef INTEGER
#undef OCTSTR
#undef TIMER
#undef QUEUE
#undef MUTEX
#undef NEXT

