
/*
 * wtp_machine-decl.h - macro call for generating WTP state machine. See the 
 * architecture document for guidance how to use and update it.
 *
 * By Aarno Syvänen for WapIT Ltd.
 *
 * WTPMachine data sructure includes current state of WTP state machine for a 
 * specific transaction. This means all data needed to handle at least two 
 * incoming events of a certain transaction. Its fields can be grouped following 
 * way:
 *
 * General: wtp machine state and in-use flag
 *
 * Fields telling the service required: 
 *               a) transaction class (is transaction confirmed or not) 
 *               b) user acknowledgement flag (do we wait for response primitive  
 *                  of WTP user (for instance, WSP) or not)
 *
 * Machine identification: address four-tuple and transaction identifier
 * 
 * Fields required for tid verification: 
 *               a) flag telling are we doing it.
 *               b) packed wsp invoke indication, which is required by the 
 *                  protocol
 *
 * Fields required for reliable transmission: 
 *               a) pointer to the timer of this machine in the timers list
 *               b) counters for acknowledgement waiting periods and retrans- 
 *                  missions 
 *               c) flag telling are we resending the result or not
 *               d) similar flag for acknowledgements
 *               e) packed result message, for greater effectivity
 * 
 * WTPMachine cannot block when handling an event. So incoming events are queued.
 * Following fields are required for handling event queues:
 *               a) mutex for serialising event handling
 *               b) mutex for for queue updating
 *
 * And a pointer to the next machine in the wtp machines list.
 */

#if !defined(MACHINE) || !defined(INTEGER) || !defined(ENUM) || \
	!defined(OCTSTR) || !defined(TIMER) || \
	!defined(MSG) || \
	!defined(WSP_EVENT) || !defined(LIST) 
#error "wsp_machine-decl.h: Some required macro is missing."
#endif

MACHINE(INTEGER(in_use);
        ENUM(state);
        INTEGER(tid);              /* transaction identifier */
        OCTSTR(source_address);    /* address four-tuple */
        INTEGER(source_port);
        OCTSTR(destination_address);
        INTEGER(destination_port);
        INTEGER(tcl);              /* transaction class */
        INTEGER(aec);              /* counter telling how many timer periods 
                                      we have waited for acknowledgement */
        INTEGER(rcr);              /* retransmission counter */
        INTEGER(tid_ve);           /* are we doing tid verification or not */
        INTEGER(u_ack);            /* user acknowledgement flag (are user 
                                      acknowledgement required) */ 
        INTEGER(rid);              /* retransmission flag, telling are we 
                                      resending the result */ 
        MSG(result);               /* packed result message - for resending */
        INTEGER(ack_pdu_sent);     /* are we resending the acknowledgement */
        TIMER(timer);              /* pointer to the timer of this machine timer
                                      in the global timers list */
        WSP_EVENT(invoke_indication); /* packed wsp invoke indication - for tid
                                         verification */
	)

#undef MACHINE
#undef INTEGER
#undef ENUM
#undef OCTSTR
#undef TIMER
#undef MSG
#undef WSP_EVENT
#undef LIST
