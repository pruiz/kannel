/*
 * wtp_events-decl.h - macro calls for defining WTP events. See the 
 * architecture document how to use and update these.
 *
 * By Aarno Syvänen for WapIT Ltd.
 *
 * WTPEvent data structure contains events WTP must handle. This means incoming 
 * messages, WSP primitives and timer expirations. Messages incoming are invoke, 
 * acknowledgement and abort. Receiving a message having an illegal PDU (PDU WTP
 * does not understand) is a separate event. 
 *
 * WSP primitives are TRInvoke.require and response, TRResult.require and TRAbort. 
 * require. 
 *
 * Fields of an incoming message event corresponds directly with fields of the 
 * message itself. Same apply to the fields of events generates by WSP, they are
 * ones required by specification. However, timer events have a meaningless 
 * dummy field.
 *
 * Data stored in an event is destroyed immediately after the event is handled.
 */

EVENT(RcvInvoke,
      {
      OCTSTR(user_data);
      OCTSTR(exit_info);
      INTEGER(tcl);
      INTEGER(tid);
      INTEGER(tid_new);
      INTEGER(rid);
      INTEGER(up_flag);
      INTEGER(exit_info_present);
      INTEGER(no_cache_supported);
      })

EVENT(RcvAbort,
      {
      INTEGER(tid);
      INTEGER(abort_type);
      INTEGER(abort_reason);
      })

EVENT(RcvAck,
      {
      INTEGER(tid);
      INTEGER(tid_ok);
      INTEGER(rid);
      })

EVENT(TRInvokeRequire,
      {
      OCTSTR(source_address);
      INTEGER(source_port);
      OCTSTR(destination_address);
      INTEGER(destination_port);
      INTEGER(ack_type);
      INTEGER(tcl);
      OCTSTR(user_data);
      })

EVENT(TRInvokeResponse,
      {
      INTEGER(tid);
      OCTSTR(exit_info);
      INTEGER(exit_info_present);
      })

EVENT(TRResultRequire,
      {
      INTEGER(tid);
      OCTSTR(user_data);
      })

EVENT(TRAbortRequire,
     {
     INTEGER(tid);
     INTEGER(abort_type);
     INTEGER(abort_reason);
     }) 

EVENT(TimerTO_A,
     {
     INTEGER(dummy);
     })

EVENT(TimerTO_R,
     {
     INTEGER(dummy);
     })

EVENT(TimerTO_W,
     {
     INTEGER(dummy);
     })

EVENT(RcvErrorPDU,
     {
     INTEGER(tid);
     })

#undef EVENT
#undef INTEGER
#undef OCTSTR
