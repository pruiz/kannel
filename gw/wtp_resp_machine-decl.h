
/*
 * wtp_machine-decl.h - macro call for generating WTP responder state machine. See 
 * the architecture document for guidance how to use and update it.
 *
 * By Aarno Syvänen for Wapit Ltd.
 *
 * WTPRespMachine data structure includes current state of WTP responder state 
 * machine for a specific transaction. This means all data needed to handle at least
 * two incoming events of a certain transaction. Its fields can be grouped following 
 * way:
 *
 * General: wtp responder machine state 
 *
 * Fields telling the service required: 
 *               a) transaction class (is transaction confirmed or not) 
 *               b) user acknowledgement flag (do we wait for response primitive  
 *                  of WTP user (for instance, WSP) or not)
 *
 * Machine identification: address four-tuple and transaction identifier
 * 
 * Field required for tid verification: 
 *               a) packed wsp invoke indication, which is required by the 
 *                  protocol
 *
 * Fields required for reliable transmission: 
 *               a) pointer to the timer of this machine in the timers list
 *               b) counters for acknowledgement waiting periods and retrans- 
 *                  missions 
 *               c) flag telling are we resending the result or not
 *               d) similar flag for acknowledgements
 *               e) packed result message, for greater effectivity
 */

#if !defined(MACHINE) 
      #error "wsp_resp_machine-decl.h: Macro MACHINE is missing."
#elif !defined(INTEGER) 
      #error "wsp_resp_machine-decl.h: Macro INTEGER is missing."
#elif !defined(ENUM) 
      #error "wsp_resp_machine-decl.h: Macro ENUM is missing."
#elif !defined(TIMER) 
      #error "wsp_resp_machine-decl.h: Macro TIMER is missing."
#elif !defined(MSG) 
      #error "wsp_resp_machine-decl.h: Macro MSG is missing."
#elif !defined(WSP_EVENT) 
      #error "wsp_resp_machine-decl.h: Macro WSP_EVENT is missing."
#elif !defined(ADDRTUPLE)
      #error "wsp_resp_machine-decl.h: Macro ADDRTUPLE is missing."
#endif

MACHINE(ENUM(state)
        INTEGER(tid)              /* transaction identifier */
	ADDRTUPLE(addr_tuple)
        INTEGER(tcl)              /* transaction class */
        INTEGER(aec)              /* counter telling how many timer periods 
                                      we have waited for acknowledgement */
        INTEGER(rcr)              /* retransmission counter */
        INTEGER(u_ack)            /* user acknowledgement flag (are user 
                                      acknowledgement required) */ 
        INTEGER(rid)              /* retransmission flag, telling are we 
                                      resending the result */ 
        MSG(result)               /* packed result message - for resending */
        INTEGER(ack_pdu_sent)     /* are we resending the acknowledgement */
        TIMER(timer)              /* pointer to the timer of this machine timer
                                      in the global timers list */
        WSP_EVENT(invoke_indication) /* packed wsp invoke indication - for tid
                                         verification */
	)

#undef MACHINE
#undef INTEGER
#undef ENUM
#undef TIMER
#undef MSG
#undef WSP_EVENT
#undef ADDRTUPLE
