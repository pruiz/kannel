/*
 * wtp_init_machine-decl.h: macro call for generating WTP initiator state 
 * machine. See the architecture document for guidance how to use and update 
 * it.
 *
 * By Aarno Syvänen for Wapit Ltd.
 *
 * WTPRespMachine data structure includes current state of WTP responder state 
 * machine for a specific transaction. This means all data needed to handle at
 * least two incoming events of a certain transaction. Its fields can be 
 * grouped following way:
 *
 * General:      a) wtp initiator machine state 
 *               b) tidnew flag, telling whether tid is wrapped up
 *
 * Fields telling the service required: 
 *               a) transaction class (is transaction confirmed or not) 
 *               b) user acknowledgement flag (do we wait for response 
 *                  primitive of WTP user (for instance, WSP) or not)
 *
 * Machine identification: address four-tuple and transaction identifier
 *
 * Fields required for reliable transmission: 
 *               a) pointer to the timer of this machine in the timers list
 *               b) counter for retransmissions 
 *               c) flag telling are we resending ack pdu doing tid verifica-
 *                  tion or not
 *               d) packed invoke message, for greater effectivity
 */


#if !defined(MACHINE) 
      #error "wsp_init_machine-decl.h: Macro MACHINE is missing."
#elif !defined(INTEGER) 
      #error "wsp_init_machine-decl.h: Macro INTEGER is missing."
#elif !defined(ENUM)  
      #error "wsp_init_machine-decl.h: Macro ENUM is missing."
#elif !defined(TIMER) 
      #error "wsp_init_machine-decl.h: Macro TIMER is missing."
#elif !defined(MSG) 
      #error "wsp_init_machine-decl.h: Macro MSG is missing."
#elif !defined(ADDRTUPLE)
      #error "wsp_init_machine-decl.h: Macro ADDRTUPLE is missing."
#endif

MACHINE(ENUM(state)
        INTEGER(tid)             /* transaction identifier */
        ADDRTUPLE(addr_tuple)
        INTEGER(tidnew)          /* tidnew flag */
        INTEGER(u_ack)           /* user acknowledgement flag */
        MSG(invoke)              /* packed invoke message for resending */
        TIMER(timer)
        INTEGER(rcr)             /* retransmission counter */
        INTEGER(tidok_sent)      /* are we resending tid verification */
        INTEGER(rid)             /* are we resending invoke */
       )

#undef MACHINE
#undef ENUM
#undef INTEGER
#undef ADDRTUPLE
#undef MSG
#undef TIMER
