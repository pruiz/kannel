
/*
 * wtp_machine-decl.h - macro call for generating WTP state machine. See the 
 * architecture document for guidance how to use and update it.
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#if !defined(MACHINE) || !defined(INTEGER) || !defined(ENUM) || \
	!defined(OCTSTR) || !defined(TIMER) || \
	!defined(QUEUE) || !defined(MUTEX) || \
	!defined(NEXT) || !defined(MSG) || \
	!defined(WSP_EVENT) || !defined(LIST)
#error "wsp_machine-decl.h: Some required macro is missing."
#endif

MACHINE(INTEGER(in_use);
        ENUM(state);
        INTEGER(tid);
        OCTSTR(source_address);
        INTEGER(source_port);
        OCTSTR(destination_address);
        INTEGER(destination_port);
        INTEGER(tcl);
        INTEGER(aec);
        INTEGER(rcr);
        INTEGER(tid_ve);
        INTEGER(u_ack);
        INTEGER(hold_on);
        INTEGER(rid);
        MSG(result);
        INTEGER(ack_pdu_sent);
        TIMER(timer_data);
        MUTEX(mutex);
	LIST(event_queue);
        WSP_EVENT(invoke_indication);
        NEXT(next);
        )

#undef MACHINE
#undef INTEGER
#undef ENUM
#undef OCTSTR
#undef TIMER
#undef QUEUE
#undef MUTEX
#undef NEXT
#undef MSG
#undef WSP_EVENT
#undef LIST
