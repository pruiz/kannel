/*
 * wtp.c - WTP implementation
 *
 * Implementation is for now very straigthforward, WTP state machines are stored
 * in an unordered linked list (this fact will change, naturally).
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp.h"

enum {
    no_datagram,
    wrong_version,
    illegal_header,
    no_segmentation,
    memory_error
};

enum {
   CURRENT = 0x00,
};

enum {
   ACKNOWLEDGEMENT = 0,
   TID_VERIFICATION = 1
};

enum {
   PROVIDER = 0x00,
   USER = 0x01
};

enum {
   UNKNOWN = 0x00,
   PROTOERR = 0x01,
   INVALIDTID = 0x02,
   NOTIMPLEMENTEDCL2 = 0x03,
   NOTIMPLEMENTEDSAR = 0x04,
   NOTIMPLEMENTEDUACK = 0x05,
   WTPVERSIONZERO = 0x06,
   CAPTEMPEXCEEDED = 0x07,
   NORESPONSE = 0x08,
   MESSAGETOOLARGE = 0x09
};    

/*
 * Global data structures:
 */

static WTPMachine *list = NULL;       /* list of wtp state machines */


/*****************************************************************************
 *
 * Prototypes of internal functions:
 *
 * Create an uniniatilized wtp state machine.
 */

static WTPMachine *wtp_machine_create_empty(void);

/*
 * Print a wtp event or a wtp machine state name as a string.
 */

static char *name_event(int name);

static char *name_state(int name);

/*
 * Find the WTPMachine from the global list of WTPMachine structures that
 * corresponds to the five-tuple of source and destination addresses and
 * ports and the transaction identifier. Return a pointer to the machine,
 * or NULL if not found.
 */
static WTPMachine *wtp_machine_find(Octstr *source_address, long source_port,
	Octstr *destination_address, long destination_port, long tid);

/*
 * Packs a wsp event. Fetches flags and user data from a wtp event. Address 
 * five-tuple and tid are fields of the wtp machine.
 */
static WSPEvent *pack_wsp_event(WSPEventType wsp_name, WTPEvent *wtp_event, 
         WTPMachine *machine);

static int wtp_tid_is_valid(WTPEvent *event);

static void append_to_event_queue(WTPMachine *machine, WTPEvent *event);
 
static WTPEvent *remove_from_event_queue(WTPMachine *machine);

static long deduce_tid(Msg *msg);

static int message_header_fixed(char octet);

static char deduce_pdu_type(char octet);

static int single_message(char octet);

static int protocol_version(char octet);

static WTPEvent *unpack_ack(long tid, char octet);

static WTPEvent *unpack_abort(long tid, char first_octet, char fourth_octet);

static WTPEvent *unpack_invoke(Msg *msg, long tid, char first_octet, 
       char fourth_octet);

static void tell_about_error(int type, WTPEvent *event);

/******************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 */

WTPEvent *wtp_event_create(enum event_name type) {
	WTPEvent *event;
	
	event = gw_malloc(sizeof(WTPEvent));

	event->type = type;
	event->next = NULL;
	
	#define INTEGER(name) p->name=0
	#define OCTSTR(name) p->name=octstr_create_empty();\
                             if (p->name == NULL)\
                                goto error
	#define EVENT(type, field) { struct type *p = &event->type; field } 
	#include "wtp_events-decl.h"
        return event;
/*
 *TBD: Send Abort(CAPTEMPEXCEEDED)
 */
error:
        #define INTEGER(name) p->name=0
        #define OCTSTR(name) if (p->name != NULL)\
                                octstr_destroy(p->name)
        #define EVENT(type, field) { struct type *p = &event->type; field }
        #include "wtp_events-decl.h"
        gw_free(event);
	error(errno, "WTP: event_create: Out of memory.");
	return NULL;
}
/*
 * Note: We must use p everywhere (including events having only integer 
 * fields), otherwise we get a compiler warning.
 */

void wtp_event_destroy(WTPEvent *event) {
#if 0
	if (event != NULL) {
		#define INTEGER(name) p->name = 0
	        #define OCTSTR(name) octstr_destroy(p->name)
	        #define EVENT(type, field) { struct type *p = &event->type; field } 
	        #include "wtp_events-decl.h"
	
		gw_free(event);
	}
#endif
}

void wtp_event_dump(WTPEvent *event) {

  	debug(0, "WTPEvent %p:", (void *) event); 
	debug(0, "  type = %s", name_event(event->type));
	#define INTEGER(name) debug(0, "  %s.%s: %ld", t, #name, p->name)
	#define OCTSTR(name) \
		debug(0, "  %s.%s:", t, #name); \
		octstr_dump(p->name)
	#define EVENT(tt, field) \
		if (tt == event->type) \
			{ char *t = #tt; struct tt *p = &event->tt; field } 
	#include "wtp_events-decl.h"
  	debug(0, "WTPEvent %p ends.", (void *) event); 
}

/*
 * Mark a WTP state machine unused. Normal functions do not remove machines. 
 * Panics when there is no machine to mark unused.
 */
void wtp_machine_mark_unused(WTPMachine *machine){
#if 0	/* XXX this needs to be re-done together with the list locking */

        WTPMachine *temp;

/*
 * If the list or temp was empty, mutex_lock will panic. (This is acceptable, 
 * because calling this function when there were no machines is a programming
 * error).
 */
        mutex_lock(list->mutex);

        temp = list;
        mutex_lock(temp->next->mutex);

        while (temp != NULL && temp->next != machine){
	      mutex_unlock(temp->mutex);
              temp = temp->next;
	      mutex_lock(temp->next->mutex);
        }

        if (temp == NULL){
	    mutex_unlock(temp->mutex);
            debug(0, "WTP: machine_mark_unused: WTPMachine unknown");
            return;
	}
       
        temp->in_use = 0;
        mutex_unlock(temp->mutex);
        return;
#endif
}

/*
 * Really removes a WTP state machine. Used only by the garbage collection. 
 * Panics when there is no machines to destroy.
 */
void wtp_machine_destroy(WTPMachine *machine){
        WTPMachine *temp;

#if 0 /* XXX list locking done wrongly */


        mutex_lock(list->mutex);
        mutex_lock(list->next->mutex);

        if (list == machine) {
           list = machine->next;         
           mutex_unlock(&list->next->mutex);
	   mutex_unlock(&list->mutex);
        } else {
          temp = list;

          while (temp != NULL && temp->next != machine){ 
	        mutex_unlock(&temp->mutex);
                temp = temp->next;
                if (temp != NULL)
		   mutex_lock(&temp->next->mutex);
          }

          if (temp == NULL){
              mutex_unlock(&temp->next->mutex);
	      mutex_unlock(&temp->mutex);
              debug(0, "WTP: machine_destroy: Machine unknown");
              return;
	  }

          temp->next = machine->next;
	}

        mutex_unlock(&temp->next->mutex);
#else
	temp = machine;
#endif

        #define INTEGER(name)
        #define ENUM(name)        
        #define OCTSTR(name) octstr_destroy(temp->name)
        #define TIMER(name) wtp_timer_destroy(temp->name)
        #define QUEUE(name) if (temp->name != NULL) \
                panic(0, "WTP: machine_destroy: Event queue was not empty")
        #define MUTEX(name) mutex_destroy(temp->name)
        #define NEXT(name)
        #define MACHINE(field) field
        #include "wtp_machine-decl.h"

#if 0
        gw_free(temp);
        mutex_unlock(&machine->next->mutex);
	mutex_unlock(&machine->mutex);
#endif
        
        return;
}

/*
 * Write state machine fields, using debug function from a project library 
 * wapitlib.c.
 */
void wtp_machine_dump(WTPMachine  *machine){

        if (machine != NULL){

           debug(0, "WTPMachine %p: dump starting", (void *) machine); 
	   #define INTEGER(name) \
	           debug(0, "  %s: %ld", #name, machine->name)
           #define ENUM(name) debug(0, "  state = %s.", name_state(machine->name))
	   #define OCTSTR(name)  debug(0, "  Octstr field %s :", #name); \
                                 octstr_dump(machine->name)
           #define TIMER(name)   debug(0, "  Machine timer %p:", (void *) \
                                       machine->name)
           #define MUTEX(name)   if (mutex_try_lock(machine->name) == -1) \
                                    debug(0, "%s locked", #name);\
                                 else {\
                                    debug(0, "%s unlocked", #name);\
                                    mutex_unlock(machine->name);\
                                 }
           #define QUEUE(name) \
	   	debug (0, "  %s %p",#name, (void *) machine->name) 
           #define NEXT(name) 
	   #define MACHINE(field) field
	   #include "wtp_machine-decl.h"
           debug (0, "WTPMachine dump ends");
	
         } else
           debug(0, "machine does not exist");
}


WTPMachine *wtp_machine_find_or_create(Msg *msg, WTPEvent *event){

           WTPMachine *machine;
           long tid;

          machine = NULL;
	  tid = -1;
          switch (event->type){

	          case RcvInvoke:
                       tid = event->RcvInvoke.tid;
                  break;

	          case RcvAck:
                       tid = event->RcvAck.tid;
                  break;

	          case RcvAbort:
                       tid = event->RcvAck.tid;
                  break;

                  default:
                       debug(0, "WTP: machine_find_or_create: unhandled event");
                  break;
	   }

           machine = wtp_machine_find(msg->wdp_datagram.source_address,
                     msg->wdp_datagram.source_port, 
                     msg->wdp_datagram.destination_address,
                     msg->wdp_datagram.destination_port, tid);

           if (machine == NULL){

              switch (event->type){

	              case RcvInvoke:
	                   machine = wtp_machine_create(
                                     msg->wdp_datagram.source_address,
				     msg->wdp_datagram.source_port, 
				     msg->wdp_datagram.destination_address,
				     msg->wdp_datagram.destination_port,
				     tid, event->RcvInvoke.tcl);
                           machine->in_use = 1;
                      break;

	              case RcvAck: 
			   error(0, "WTP: machine_find_or_create: ack received, 
                                 yet having no machine");
                      break;

                      case RcvAbort: 
			   error(0, "WTP: machine_find_or_create: abort received, 
                                yet having no machine");
                      break;
                 
	              default:
                           debug(0, "WTP: machine_find_or_create: unhandled event");
                           wtp_event_dump(event);
                      break;
              }
	   }

           return machine;
}

/*
 * Transfers data from fields of a message to fields of WTP event. User data has
 * the host byte order. Updates the log and sends protocol error messages.
 */

WTPEvent *wtp_unpack_wdp_datagram(Msg *msg){

         WTPEvent *event = NULL;
         char first_octet,
              fourth_octet,
              this_octet,
              pdu_type;
 
         int tid,
             tpi_length_type,
             tpi_length;

         if (octstr_len(msg->wdp_datagram.user_data) < 3){
            tell_about_error(memory_error, event);
            return NULL;
         }

         tid = deduce_tid(msg);
         first_octet = octstr_get_char(msg->wdp_datagram.user_data, 0);
         pdu_type = deduce_pdu_type(first_octet);
         
         if (message_header_fixed(first_octet)){
/*
 * Message type was invoke
 */
            if (pdu_type == INVOKE){

               fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 3);
               if (fourth_octet == -1){
                  tell_about_error(memory_error, event);
                  return NULL;
               }
               return unpack_invoke(msg, tid, first_octet, fourth_octet);
            }
/*
 * Message type is supposed to be result. This is impossible, so we have an
 * illegal header.
 */
            if (pdu_type == RESULT){
               tell_about_error(illegal_header, event);
               return NULL;
            }

            if (pdu_type == ACK){
               return unpack_ack(tid, first_octet);
            }

	    if (pdu_type == ABORT){
               fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 4);
               if (fourth_octet == -1){
                  tell_about_error(memory_error, event);
                  return NULL;
               }
               return unpack_abort(tid, first_octet, fourth_octet);
            }

/*
 * WDP does segmentation.
 */
            if (pdu_type == SEGMENTED_INVOKE || pdu_type == SEGMENTED_RESULT ||
                pdu_type == NEGATIVE_ACK){
	       tell_about_error(no_segmentation, event);
               return NULL; 
            }

            if (pdu_type == -1){
               tell_about_error(illegal_header, event);
               return NULL;
            } 
/*
 * Message is of variable length. This is possible only when we are receiving
 * an invoke message. The feature remains to be implemented.
 */
         } /* if message_header fixed true */
         else {
           this_octet = fourth_octet = octstr_get_char(
                        msg->wdp_datagram.user_data, 4);
           tpi_length_type = this_octet>>2&1;
/*
 * TPI can be long
 */
           if (tpi_length_type == 1){           
               tpi_length = 1;
           } else {
/* or short*/
               tpi_length = 0;
           } /* if tpi_length_type */

           debug(0, "WTP: unpack_wdp_datagram: variable headers not implemented");
           return NULL;
	 } /* if message_header_fixed false */   
	 
	 panic(0, "WTP: this should never be reached or else there's a return statement missing here in " __FILE__);
	 return NULL; /* this is an assumption */
} /* function*/

/*
 * Feed an event to a WTP state machine. Handle all errors yourself, do not
 * report them to the caller. Note: Do not put {}s of the else block inside
 * the macro definition (it ends with a line without a backlash). 
 */
void wtp_handle_event(WTPMachine *machine, WTPEvent *event){
     WSPEventType current_primitive;
     WSPEvent *wsp_event = NULL;
     WTPTimer *timer = NULL;

/* 
 * If we're already handling events for this machine, add the event to the 
 * queue.
 */
     if (mutex_try_lock(machine->mutex) == -1) {
	  append_to_event_queue(machine, event);
	  return;
     }

     do {
	  debug(0, "WTP: handle_event: state is %s, event is %s.",
	  		name_state(machine->state),
	  		name_event(event->type));

	  #define STATE_NAME(state)
	  #define ROW(wtp_state, event_type, condition, action, next_state) \
		  if (machine->state == wtp_state && \
		     event->type == event_type && \
		     (condition)) { \
                     debug(0, "WTP: doing action for %s", #wtp_state); \
		     action \
                     debug(0, "WTP: setting state to %s", #next_state); \
                     machine->state = next_state; \
		  } else 
	  #include "wtp_state-decl.h"
		  {
			error(0, "WTP: handle_event: unhandled event!");
			debug(0, "WTP: handle_event: Unhandled event was:");
			wtp_event_dump(event);
		  }

          event = remove_from_event_queue(machine);
     } while (event != NULL);

     mutex_unlock(machine->mutex);
     return;

/*
 *Send Abort(CAPTEMPEXCEEDED)
 */
mem_error:
     debug(0, "WTP: handle_event: out of memory");
     if (timer != NULL)
        wtp_timer_destroy(timer);
     if (wsp_event != NULL)
        wsp_event_destroy(wsp_event);
     gw_free(timer);
     gw_free(wsp_event);
     mutex_unlock(machine->mutex);
}

unsigned long wtp_tid_next(void){
     static unsigned long next_tid = 0;
     return ++next_tid;
}


/*****************************************************************************
 *
 *INTERNAL FUNCTIONS:
 *
 * Give the name of an event in a readable form. 
 */

static char *name_event(int s){

       switch (s){
              #define EVENT(type, field) case type: return #type;
              #include "wtp_events-decl.h"
              default:
                      return "unknown event";
       }
 }


static char *name_state(int s){

       switch (s){
              #define STATE_NAME(state) case state: return #state;
              #define ROW(state, event, condition, action, new_state)
              #include "wtp_state-decl.h"
              default:
                      return "unknown state";
       }
}


static WTPMachine *wtp_machine_find(Octstr *source_address, long source_port,
	   Octstr *destination_address, long destination_port, long tid){

           WTPMachine *temp;

/*
 * We are interested only machines in use, it is, having in_use-flag 1.
 */
           if (list == NULL){
              return NULL;
           }

           mutex_lock(list->mutex); 
           temp = list;
          
           while (temp != NULL){
   
	   if ((octstr_compare(temp->source_address, source_address) == 0) &&
                temp->source_port == source_port && 
                (octstr_compare(temp->destination_address,
                                destination_address) == 0) &&
                temp->destination_port == destination_port &&
		temp->tid == tid && temp->in_use == 1){

                mutex_unlock(temp->mutex);
                return temp;
                 
		} else {

		   mutex_unlock(temp->mutex);
                   temp = temp->next;

                   if (temp != NULL)
		      mutex_lock(temp->mutex);
               }              
           }
           return temp;
}

static WTPMachine *wtp_machine_create_empty(void){

        WTPMachine *machine;

        machine = gw_malloc(sizeof(WTPMachine));
        
        #define INTEGER(name) machine->name = 0
        #define ENUM(name) machine->name = LISTEN
        #define OCTSTR(name) machine->name = octstr_create_empty();\
                             if (machine->name == NULL)\
                                goto error
        #define QUEUE(name) machine->name = NULL
        #define MUTEX(name) machine->name = mutex_create()
        #define TIMER(name) machine->name = wtp_timer_create();\
                            if (machine->name == NULL)\
                               goto error
        #define NEXT(name) machine->name = NULL
        #define MACHINE(field) field
        #include "wtp_machine-decl.h"

#if 0 /* This way of locking the list is broken, we need a separate mutex */
        if (list != NULL)
           mutex_lock(&list->mutex);
#endif

        machine->next = list;
        list = machine;

#if 0
        mutex_unlock(&list->mutex);
#endif

        return machine;

/*
 * Message Abort(CAPTEMPEXCEEDED), to be added later. 
 * Thou shalt not leak memory... Note, that a macro could be called many times.
 * So it is possible one call to succeed and another to fail. 
 */
 error:  if (machine != NULL) {
            #define INTEGER(name)
            #define ENUM(name)
            #define OCTSTR(name) if (machine->name != NULL)\
                                    octstr_destroy(machine->name)
            #define QUEUE(name)  
            #define MUTEX(name)  mutex_lock(machine->name);\
                                 mutex_destroy(machine->name)
            #define TIMER(name) if (machine->name != NULL)\
                                   wtp_timer_destroy(machine->name)
            #define NEXT(name)
            #define MACHINE(field) field
            #include "wtp_machine-decl.h"
        }
        gw_free(machine);
        error(errno, "WTP: machine_create_empty: Out of memory");
        return NULL;
}

/*
 * Create a new WTPMachine for a given transaction, identified by the
 * five-tuple in the arguments.
 */
WTPMachine *wtp_machine_create(Octstr *source_address, 
           long source_port, Octstr *destination_address, 
           long destination_port, long tid, long tcl) {

	   WTPMachine *machine;
	   
	   machine = wtp_machine_create_empty();
	   if (machine == NULL)
	   	panic(0, "wtp_machine_create_empty failed, out of memory");

           machine->source_address = source_address;
           machine->source_port = source_port;
           machine->destination_address = destination_address;
           machine->destination_port = destination_port;
           machine->tid = tid;
           machine->tcl = tcl;

           return machine;
} 


/*
 * Packs a wsp event. Fetches flags and user data from a wtp event. Address 
 * five-tuple and tid are fields of the wtp machine.
 */
static WSPEvent *pack_wsp_event(WSPEventType wsp_name, WTPEvent *wtp_event, 
         WTPMachine *machine){

         WSPEvent *event = wsp_event_create(wsp_name);

/*
 * Abort(CAPTEMPEXCEEDED)
 */
         if (event == NULL){
            debug(0, "WTP: pack_wsp_event: Out of memory");
            gw_free(event);
            return NULL;
         }
         
         switch (wsp_name){
                
	        case TRInvokeIndication:
                     event->TRInvokeIndication.ack_type = machine->u_ack;
                     event->TRInvokeIndication.user_data =
                            wtp_event->RcvInvoke.user_data;
                     event->TRInvokeIndication.tcl = wtp_event->RcvInvoke.tcl;
                     event->TRInvokeIndication.wsp_tid = wtp_tid_next();
                     event->TRInvokeIndication.machine = machine;
                break;

	        case TRInvokeConfirmation:
                     event->TRInvokeConfirmation.wsp_tid =
                            event->TRInvokeIndication.wsp_tid;
                     event->TRInvokeConfirmation.machine = machine;
                break;
                
	        case TRResultConfirmation:
                     event->TRResultConfirmation.exit_info =
                            wtp_event->RcvInvoke.exit_info;
                     event->TRResultConfirmation.exit_info_present =
                            wtp_event->RcvInvoke.exit_info_present;
                     event->TRResultConfirmation.wsp_tid =
                            event->TRInvokeIndication.wsp_tid;
                     event->TRResultConfirmation.machine = machine;
                break;

	        case TRAbortIndication:
                     event->TRAbortIndication.abort_code =
                            wtp_event->RcvAbort.abort_reason;
                     event->TRResultConfirmation.wsp_tid =
                            event->TRInvokeIndication.wsp_tid;
                     event->TRAbortIndication.machine = machine;
                break;
                
	        default:
                break;
         }

         return event;
} 

static int wtp_tid_is_valid(WTPEvent *event){
 
    return 1;
}

/*
 * Append an event to the event queue of a WTPMachine.
 */
static void append_to_event_queue(WTPMachine *machine, WTPEvent *event) {

	mutex_lock(machine->queue_lock);

	if (machine->event_queue_head == NULL) {
		machine->event_queue_head = event;
		machine->event_queue_tail = event;
		event->next = NULL;
	} else {
		machine->event_queue_tail->next = event;
		machine->event_queue_tail = event;
		event->next = NULL;
	}

	mutex_unlock(machine->queue_lock);
}


/*
 * Return the first event from the event queue of a WTPMachine, and remove
 * it from the queue. Return NULL if the queue was empty.
 */
static WTPEvent *remove_from_event_queue(WTPMachine *machine) {
	WTPEvent *event;
	
	mutex_lock(machine->queue_lock);

	if (machine->event_queue_head == NULL)
		event = NULL;
	else {
		event = machine->event_queue_head;
		machine->event_queue_head = event->next;
		event->next = NULL;
	}

	mutex_unlock(machine->queue_lock);

	return event;
}

/*
 * Every message type uses the second and the third octets for tid. Bytes are 
 * already in host order. Note that the iniator turns the first bit off, so we do
 * have a genuine tid.
 */
static long deduce_tid(Msg *msg){
   
       long first_part,
            second_part,
            tid;

       first_part = octstr_get_char(msg->wdp_datagram.user_data, 1);
       second_part = octstr_get_char(msg->wdp_datagram.user_data, 2);
       tid = first_part;
       tid = (tid<<8) + second_part; 

       return tid;
}


static int message_header_fixed(char octet){

       return !(octet>>7); 
}

static char deduce_pdu_type(char octet){

       int type;

       if ((type = octet>>3&15) > 7){
          return -1;
       } else {
          return type; 
       }
}

static int single_message(char octet){

       char this_octet,
            gtr,
            ttr;

       this_octet = octet;
       gtr = this_octet>>2&1;
       this_octet = octet;
       ttr = this_octet>>1&1;

       if (gtr == 0 || ttr == 0)
	  return 0;  
       else
          return 1;
}

static int protocol_version(char octet){

       return octet>>6&3;
}

static WTPEvent *unpack_ack(long tid, char octet){

      WTPEvent *event;
      char this_octet;

      event = wtp_event_create(RcvAck);

      if (event == NULL){
         tell_about_error(memory_error, event); 
         return NULL;
      }
               
      event->RcvAck.tid = tid;
      this_octet = octet;
      event->RcvAck.tid_ok = this_octet>>2&1;
      this_octet = octet;
      event->RcvAck.rid = this_octet&1;

      return event;
}

WTPEvent *unpack_abort(long tid, char first_octet, char fourth_octet){

         WTPEvent *event;
         char abort_type;      

         event = wtp_event_create(RcvAbort);

         if (event == NULL){
            tell_about_error(memory_error, event);
            return NULL;
         }   
    
         abort_type = first_octet&7;

	 if (abort_type > 1 || fourth_octet > NUMBER_OF_ABORT_REASONS){
            tell_about_error(illegal_header, event);
            return NULL;
         }
                
         event->RcvAbort.tid = tid;  
         event->RcvAbort.abort_type = abort_type;   
         event->RcvAbort.abort_reason = fourth_octet;
         debug(0, "WTP: unpack_abort: abort event packed");
         return event;
}

WTPEvent *unpack_invoke(Msg *msg, long tid, char first_octet, char fourth_octet){

         WTPEvent *event;
         char this_octet,
              tcl;

         event = wtp_event_create(RcvInvoke);

         if (event == NULL){
	    tell_about_error(memory_error, event);
            return NULL;
         }   

         if (!single_message(first_octet)){
            tell_about_error(no_segmentation, event);
            return NULL;
	 }

         if (protocol_version(fourth_octet) != CURRENT){
            tell_about_error(wrong_version, event);
            return NULL;
         }

         this_octet = fourth_octet;
         tcl = this_octet&3; 
         if (tcl > 2){
            tell_about_error(illegal_header, event);
            return NULL;
         }

         event->RcvInvoke.tid = tid;
         event->RcvInvoke.rid = first_octet&1;
         this_octet = fourth_octet;               
         event->RcvInvoke.tid_new = this_octet>>5&1;
         this_octet = fourth_octet;
         event->RcvInvoke.up_flag = this_octet>>4&1;
         this_octet = fourth_octet;
         event->RcvInvoke.tcl = tcl; 
 
/*
 * At last, the message itself. We remove the header.
 */
         octstr_delete(msg->wdp_datagram.user_data, 0, 4);
         event->RcvInvoke.user_data = msg->wdp_datagram.user_data; 

         return event;
}

static void tell_about_error(int type, WTPEvent *event){

       switch (type){
/*
 * TDB: Send Abort(WTPVERSIONZERO)
 */
              case wrong_version:
                   gw_free(event);
                   error(0, "WTP: Version not supported");
              break;
/*
 * Send Abort(NOTIMPLEMENTEDSAR)
 */
              case no_segmentation:
                   gw_free(event);
                   error(0, "WTP: No segmentation implemented");
              break;
/*
 *TBD: Send Abort(PROTOERR). Add necessary indications! 
 */
             case illegal_header:
                  gw_free(event);
                  error(0, "WTP: Illegal header structure");
             break;
/*
 * TBD: Send Abort(CAPTEMPEXCEEDED), too.
 */
            case memory_error:
                 gw_free(event);
                 error(0, "WTP: Out of memory");
            break;
/*
 * TBD: Reason to panic?
 */
           case no_datagram:   
                gw_free(event);
                error(0, "WTP: No datagram received");
           break;
     }
}

/*****************************************************************************/







