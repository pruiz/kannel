/*
 * wtp_events-decl.h - macro calls for defining WTP events. See the 
 * architecture document how to use and update these.
 *
 * By Aarno Syvänen for WapIT Ltd.
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

EVENT(TRInvoke,
      {
      INTEGER(tid);
      OCTSTR(exit_info);
      INTEGER(exit_info_present);
      })

EVENT(TRResult,
      {
      INTEGER(tid);
      OCTSTR(user_data);
      })

EVENT(TRAbort,
     {
     INTEGER(tid);
     INTEGER(abort_type);
     INTEGER(abort_reason);
     }) 

EVENT(TimerTO_A,
     {
     INTEGER(tid);
     })

EVENT(TimerTO_R,
     {
     INTEGER(tid);
     })

EVENT(TimerTO_W,
     {
     INTEGER(tid);
     })

EVENT(RcvErrorPDU,
     {
     INTEGER(tid);
     })

#undef EVENT
#undef INTEGER
#undef OCTSTR
