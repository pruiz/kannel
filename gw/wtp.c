/*
 * wtp.c - WTP implementation
 *
 * Implementation is for now very straigthforward, WTP state machines are stored
 * as an unordered linked list (this fact will change, naturally). Segments to be 
 * reassembled are stored as ordered linked list. 
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp.h" 

enum {
    no_datagram,
    wrong_version,
    illegal_header,
    no_segmentation,
    pdu_too_short_error,
    no_concatenation
};

enum {
   CURRENT = 0x00,
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

enum {
   body_segment,
   group_trailer_segment,
   transmission_trailer_segment,
   single_message
};

/*
 * Global data structures:
 */

struct Machines {
       WTPMachine *first;        /* pointer to the first machine in the machines 
                                    list */
       WTPMachine *list;         /* pointer to the last machine in the machines 
                                    list */       
       Mutex *lock;              /* global mutex inserting, updating and removing 
                                    machines */   
};

typedef struct Machines Machines;                                      

static Machines machines =
{
      NULL,
      NULL,
      NULL,
};

/*****************************************************************************
 *
 * Prototypes of internal functions:
 *
 * Create an uniniatilized wtp state machine.
 */

static WTPMachine *wtp_machine_create_empty(void);

/*
 * Functions for handling segments
 */

static WTPSegment *create_segment(void);
#ifdef next
static void segment_dump(WTPSegment *segment);

static void segment_destroy(WTPSegment *segment);
#endif
static WTPSegment *find_previous_segment(long tid, char sequence_number,
       WTPSegment *first, WTPSegment *next);

static WTPSegment *insert_segment(WTPSegment *previous, WTPSegment *next, 
       WTPSegment *segment);

static int list_missing_segments(WTPSegment *segments_ackd, 
       WTPSegment *segments_list, WTPSegment *missing_segments);

static WTPSegment *make_missing_segments_list(Msg *msg, char number_of_missing);
/*
 * Print a wtp event or a wtp machine state name as a string.
 */

static char *name_event(int name);

static char *name_state(int name);

/*
 * Really removes a WTP state machine. Used only by the garbage collection. 
 */
static void destroy_machine(WTPMachine *machine, WTPMachine *previous);

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
#ifdef next
static int message_header_fixed(char octet);
#endif
static char deduce_pdu_type(char octet);

static int message_type(char octet);

static int protocol_version(char octet);

static WTPEvent *unpack_ack(long tid, char octet);

static WTPEvent *unpack_abort(long tid, char first_octet, char fourth_octet);

static WTPEvent *unpack_invoke(Msg *msg, WTPSegment *segment, long tid, 
       char first_octet, char fourth_octet);

static Octstr *unpack_segmented_invoke(Msg *msg, WTPSegment *segment, long tid, 
       char first_octet, char fourth_octet);

static WTPSegment *unpack_negative_ack(Msg *msg, char octet);

static void tell_about_error(int type, WTPEvent *event);
#ifdef next
static int tpi_short(char octet);
#endif
static WTPEvent *unpack_invoke_flags(WTPEvent *event, long tid, char first_octet, 
                                     char fourth_octet);

static WTPSegment *add_segment_to_message(long tid, Octstr *data, char position);

static int first_segment(WTPEvent *event);

static Octstr *concatenate_message(long tid, WTPSegment *segments_list);

static Address *deduce_segment_ack_address(Msg *msg);

/******************************************************************************
 *
 * EXTERNAL FUNCTIONS:
 */

WTPEvent *wtp_event_create(enum event_name type) {
	WTPEvent *event;
	
	event = gw_malloc(sizeof(WTPEvent));

	event->type = type;
	event->next = NULL;
	
	#define INTEGER(name) p->name = 0
	#define OCTSTR(name) p->name = octstr_create_empty()
	#define EVENT(type, field) { struct type *p = &event->type; field } 
	#include "wtp_events-decl.h"
        return event;
}
/*
 * Note: We must use p everywhere (including events having only integer 
 * fields), otherwise we get a compiler warning. (Defs will be removed when we have
 * an integrated memory freeing policy).
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
 * Mark a WTP state machine unused. Normal functions do not remove machines, just 
 * set a flag. Panics when there is no machine to mark unused. If the machines 
 * list is busy, just wait (fetching the page is the most time-consuming task).
 */
void wtp_machine_mark_unused(WTPMachine *machine){

     if (machines.list == NULL) {
        panic(0, "WTP: the list is empty");
        return;
     }

     mutex_lock(machines.lock);

     machine->in_use = 0;

     mutex_unlock(machines.lock);

     return;
}

/* 
 * Removes from the machines list all machines having in_use-flag cleared. Panics  if 
 * machines list is empty. If machines list is busy, does nothing (garbage collection 
 * will eventually start again).
 */
void wtp_machines_list_clear(void){

     WTPMachine *this_machine = NULL,
                *previous = NULL; 

     if (mutex_try_lock(machines.lock) == -1)
        return;

     else {
        if (machines.list == NULL){
           panic(0, "WTP: wtp_machines_list_clear: list is empty");
           return;
        }

        this_machine = machines.first;
        previous = machines.first;

        while (this_machine != NULL){
              if (this_machine->in_use == 0)
                  destroy_machine(this_machine, previous);
              previous = this_machine;
              this_machine = this_machine->next;
        }
     }
} 

/*
 * Write state machine fields, using debug function from a project library 
 * wapitlib.c.
 */
void wtp_machine_dump(WTPMachine *machine){

        if (machine != NULL){

           debug(0, "WTPMachine %p: dump starting", (void *) machine); 
	   #define INTEGER(name) \
	           debug(0, "  %s: %ld", #name, machine->name)
           #define MSG(name) \
                   debug(0, "Result field %s: ", #name); \
                   msg_dump(machine->name)
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
	
	} else {
           debug(0, "WTP: dump: machine does not exist");
        }
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
                       tid = event->RcvAbort.tid;
                  break;

                  default:
                       debug(0, "WTP: machine_find_or_create: unhandled event");
                       wtp_event_dump(event);
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
 * the host byte order. Updates the log and sends protocol error messages. Re-
 * assembles segmented messages, too.
 *
 * Return event, when we have a single message; NULL, when we have a segment.
 */
WTPEvent *wtp_unpack_wdp_datagram(Msg *msg){

         static WTPEvent *event = NULL;
         static WTPSegment *segments_list = NULL,
                           *missing_segments = NULL;

         char first_octet,
              fourth_octet,
              pdu_type;
 
         long tid = 0;

         if (octstr_len(msg->wdp_datagram.user_data) < 3){
            tell_about_error(pdu_too_short_error, event);
            debug(0, "Got too short PDU (less than three octets)");
            msg_dump(msg);
            return NULL;
         }

         tid = deduce_tid(msg);
         first_octet = octstr_get_char(msg->wdp_datagram.user_data, 0);
         pdu_type = deduce_pdu_type(first_octet);

         switch (pdu_type){
/*
 * Message type cannot be result, because we are a server.
 */
                case ERRONEOUS: case RESULT: case SEGMENTED_RESULT:
                     tell_about_error(illegal_header, event);
                     return NULL;
                break;

                case NOT_ALLOWED:
                     tell_about_error(no_concatenation, event);
                     debug(0, "WTP: pdu type was %d", pdu_type);
                     return NULL;
                break;
/*
 * Invoke PDU is used by first segment of a segmented message, too. 
 */       
	        case INVOKE:
                     fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 3);

                     if (fourth_octet == -1){
                         tell_about_error(pdu_too_short_error, event);
                         debug(0, "WTP: unpack_datagram; missing fourth octet
                              (invoke)");
                         msg_dump(msg);
                         return NULL;
                     }

                     event = unpack_invoke(msg, segments_list, tid, first_octet, 
                                           fourth_octet);

                     if (first_segment(event)) {
                        return NULL;
                     } else {
                        return event;
                     }   
               break;

               case ACK:
                    return unpack_ack(tid, first_octet);
               break;

	       case ABORT:
                    fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 3);

                    if (fourth_octet == -1){
                       tell_about_error(pdu_too_short_error, event);
                       debug(0, "WTP: unpack_datagram; missing fourth octet 
                            (abort)");
                         msg_dump(msg);
                       return NULL;
                    }

                    return unpack_abort(tid, first_octet, fourth_octet);
               break;

               case SEGMENTED_INVOKE:
                    fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 3);

                    if (fourth_octet == -1){
                       tell_about_error(pdu_too_short_error, event);
                       return NULL;
                    }

                    event->RcvInvoke.user_data = unpack_segmented_invoke(
                           msg, segments_list, tid, first_octet, fourth_octet);

                    if (message_type(first_octet) == transmission_trailer_segment) 
                       return event;
                    else
                       return NULL;
              break;
                  
              case NEGATIVE_ACK:
                   fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 3);

                   if (fourth_octet == -1){
                      tell_about_error(pdu_too_short_error, event);
                      return NULL;
                   }

                   missing_segments = unpack_negative_ack(msg, fourth_octet);
                   return NULL;
              break;
         } /* switch */
         panic(0, "Following return is unnecessary but are required by the 
               compiler");
         return NULL;
} /* function */

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
	  debug(0, "WTP: handle_event: machine %p, state %s, event %s.",
	  		(void *) machine,
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
}

unsigned long wtp_tid_next(void){
     static unsigned long next_tid = 0;
     return ++next_tid;
}


void wtp_init(void) {
	machines.lock = mutex_create();
}


/*****************************************************************************
 *
 * INTERNAL FUNCTIONS:
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


/*
 * If the machines list is busy, just waits. We are interested only machines in use,
 * it is, having in_use-flag 1.
 */
static WTPMachine *wtp_machine_find(Octstr *source_address, long source_port,
       Octstr *destination_address, long destination_port, long tid){

           WTPMachine *this_machine;

           if (machines.list == NULL){
              debug(0, "WTP: machine_find: list is empty");
              return NULL;
           }

           mutex_lock(machines.lock);

           this_machine = machines.first;
          
           while (this_machine != NULL){
   
	         if ((octstr_compare(this_machine->source_address, 
                                     source_address) == 0) &&
                      this_machine->source_port == source_port && 
                      (octstr_compare(this_machine->destination_address,
                                      destination_address) == 0) &&
                      this_machine->destination_port == destination_port &&
		      this_machine->tid == tid && 
                      this_machine->in_use == 1){

                    mutex_unlock(machines.lock);
                   
                    return this_machine;
                 
		 } else {
                    this_machine = this_machine->next;  
                 }              
          }

          mutex_unlock(machines.lock); 
          return this_machine;
}

/*
 * Iniatilizes the global lock for inserting and removing machines. If machines 
 * list is busy, just wait.
 */
static WTPMachine *wtp_machine_create_empty(void){
        WTPMachine *machine = NULL;

        machine = gw_malloc(sizeof(WTPMachine));
        
        #define INTEGER(name) machine->name = 0
        #define ENUM(name) machine->name = LISTEN
        #define MSG(name) machine->name = msg_create(wdp_datagram)
        #define OCTSTR(name) machine->name = octstr_create_empty()
        #define QUEUE(name) machine->name = NULL
        #define MUTEX(name) machine->name = mutex_create()
        #define TIMER(name) machine->name = wtp_timer_create()
        #define NEXT(name) machine->name = NULL
        #define MACHINE(field) field
        #include "wtp_machine-decl.h"

	mutex_lock(machines.lock);

	if (machines.list == NULL)
		machines.first = machine;
	else
		machines.list->next = machine;
        machines.list = machine;

	mutex_unlock(machines.lock);

        return machine;
}

/*
 * Create a new WTPMachine for a given transaction, identified by the five-tuple 
 * in the arguments. In addition, update the transaction class field of the 
 * machine.
 */
WTPMachine *wtp_machine_create(Octstr *source_address, 
           long source_port, Octstr *destination_address, 
           long destination_port, long tid, long tcl) {

	   WTPMachine *machine;
	   
	   machine = wtp_machine_create_empty();

           machine->source_address = source_address;
           machine->source_port = source_port;
           machine->destination_address = destination_address;
           machine->destination_port = destination_port;
           machine->tid = tid;
           machine->tcl = tcl;

           return machine;
} 

static WTPSegment *create_segment(void){

       WTPSegment *segment;

       segment = gw_malloc(sizeof(WTPSegment));
       segment->tid = 0;
       segment->packet_sequence_number = 0;
       segment->data = octstr_create_empty();
       
       segment->next = NULL;

       return segment;
}
#ifdef next
static void segment_dump(WTPSegment *segment){

       debug(0, "WTP: segment was:");
       debug(0, "tid was: %ld", segment->tid);
       debug(0, "psn was: %d", segment->packet_sequence_number);
       debug(0, "segment itself was:");
       octstr_dump(segment->data);
       debug(0, "WTP: segment dump ends");
}

static void segment_destroy(WTPSegment *segment){

       octstr_destroy(segment->data);
       gw_free(segment->next);
       gw_free(segment);
}
#endif
/*
 * Packs a wsp event. Fetches flags and user data from a wtp event. Address 
 * five-tuple and tid are fields of the wtp machine.
 */
static WSPEvent *pack_wsp_event(WSPEventType wsp_name, WTPEvent *wtp_event, 
         WTPMachine *machine){

         WSPEvent *event = wsp_event_create(wsp_name);

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

#ifdef next
static int message_header_fixed(char octet){

       return !(octet>>7); 
}
#endif

static char deduce_pdu_type(char octet){

       int type;

       if ((type = octet>>3&15) > 7){
          return -1;
       } else {
          return type; 
       }
}

static int message_type(char octet){

       char this_octet,
            gtr,
            ttr;

       this_octet = octet;
       gtr = this_octet>>2&1;
       this_octet = octet;
       ttr = this_octet>>1&1;

       if (gtr == 1 && ttr == 1)
	  return single_message;  
       if (gtr == 0 && ttr == 0)
          return body_segment;
       if (gtr == 1 && ttr == 0)
          return group_trailer_segment;
       if (gtr == 0 && ttr == 1)
          return transmission_trailer_segment;
       panic(0, "Following return is unnecessary but required by the compiler");
       return 0;
}

static int protocol_version(char octet){

       return octet>>6&3;
}

static WTPEvent *unpack_ack(long tid, char octet){

      WTPEvent *event;
      char this_octet;

      event = wtp_event_create(RcvAck);

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

/*
 * Fields of an unsegmented invoke are transferred to WTPEvent having type 
 * RcvInvoke.
 *
 * A segmented message is indicated by a cleared ttr flag. This causes the protocol
 * to add the received segment to the message identified by tid. Invoke message has 
 * an implicit sequence number 0 (it being the first segment).
 */
WTPEvent *unpack_invoke(Msg *msg, WTPSegment *segments_list, long tid, 
                        char first_octet, char fourth_octet){

         WTPEvent *event = NULL;

         event = wtp_event_create(RcvInvoke);

         if (protocol_version(fourth_octet) != CURRENT){
            tell_about_error(wrong_version, event);
            return NULL;
         }
/*
 * First invoke message includes all event flags, even when we are receiving a 
 * segmented message. So we first fetch event flags, and then handle user_data 
 * differently: if message was unsegmented, we tranfer all data to event; if it
 * was segmented, we begin reassembly.
 */
         event = unpack_invoke_flags(event, tid, first_octet, fourth_octet);
         octstr_delete(msg->wdp_datagram.user_data, 0, 4);
 
         switch (message_type(first_octet)) {
         
	        case group_trailer_segment:
                     debug(0, "WTP: Got a segmented message");
                     msg_dump(msg);
                     segments_list = add_segment_to_message(tid, 
                                     msg->wdp_datagram.user_data, 0);
                     return NULL;
	        break;

	        case  single_message:
                      event->RcvInvoke.user_data = msg->wdp_datagram.user_data; 
                      return event;
                break;

	        default:
                      tell_about_error(illegal_header, event);
                      return NULL;
                break;
         }
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
            case pdu_too_short_error:
                 gw_free(event);
                 error(0, "WTP: PDU too short");
            break;
/*
 * TBD: Reason to panic?
 */
           case no_datagram:   
                gw_free(event);
                error(0, "WTP: No datagram received");
           break;

           case no_concatenation:
                free(event);
                error(0, "WTP: No connectionless mode nor concatenation supported");
           break;
     }
}

static Octstr *unpack_segmented_invoke(Msg *msg, WTPSegment *segments_list, 
       long tid, char first_octet, char fourth_octet){
       
       Octstr *event_data = NULL;
       static WTPSegment *segments_ackd = NULL;
       WTPSegment *missing_segments = NULL;
       Address *address = NULL;
       
       char packet_sequence_number = 0;
       int segments_missing = 0;

       static int negative_ack_sent = 0,
              group_ack_sent = 0;

       debug(0, "WTP: got a segmented invoke package");

       tid = deduce_tid(msg);
       packet_sequence_number = fourth_octet;
       address = deduce_segment_ack_address(msg);

       if (message_type(first_octet) == body_segment){
          debug(0, "WTP: Got a body segment");
          msg_dump(msg);
          segments_list = add_segment_to_message(tid, 
                          msg->wdp_datagram.user_data, packet_sequence_number);
          return NULL;
       }

       if (message_type(first_octet) == group_trailer_segment){
          debug(0, "WTP: Got the last segment of the group");
          msg_dump(msg);
          segments_list = add_segment_to_message(tid, 
                          msg->wdp_datagram.user_data, packet_sequence_number);
          segments_missing = list_missing_segments(segments_ackd, segments_list,
                             missing_segments);

          if (segments_missing) {
             wtp_send_negative_ack(address, tid, negative_ack_sent,
                                   segments_missing, missing_segments);
             negative_ack_sent = 1;

          } else {
            wtp_send_group_ack(address, tid, group_ack_sent, 
                               packet_sequence_number);
            group_ack_sent = 1;
          }

          segments_ackd = segments_list;
          return NULL;
      }

      if (message_type(first_octet) == transmission_trailer_segment){
         debug(0, "WTP: Got last segment of a message");
         msg_dump(msg);

         segments_list = add_segment_to_message(tid, 
                         msg->wdp_datagram.user_data, packet_sequence_number);
         segments_missing = list_missing_segments(segments_ackd, segments_list,
                            missing_segments);

         if (segments_missing) {
             wtp_send_negative_ack(address, tid, negative_ack_sent, 
                                   segments_missing, missing_segments);
             negative_ack_sent = 1;
             return NULL;

         } else {
            wtp_send_group_ack(address, tid, group_ack_sent, 
                               packet_sequence_number);
            group_ack_sent = 1;
         }         

         event_data = concatenate_message(tid, segments_list);
         
         missing_segments = NULL;
         segments_missing = 0;
         group_ack_sent = 0;
         negative_ack_sent = 0;        

         return event_data;
      }
      panic(0, "Following return is unnecessary but is required by the compiler");
      return NULL;
}

static WTPSegment *unpack_negative_ack(Msg *msg, char fourth_octet){

       WTPSegment *missing_segments = NULL;
       char number_of_missing_packets = 0;
       
       debug(0, "WTP: got a negative ack");
       number_of_missing_packets = fourth_octet; 
       missing_segments = make_missing_segments_list(msg, 
                          number_of_missing_packets);      

       return missing_segments;
}

#ifdef next
static int tpi_short(char octet){
       
       return octet>>2&1;
}
#endif

static WTPEvent *unpack_invoke_flags(WTPEvent *event, long tid, char first_octet, 
                                     char fourth_octet){

         char this_octet,
              tcl;

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

         return event;
}

/*
 * Returns: pointer to the segment added, if OK
 */
static WTPSegment *add_segment_to_message(long tid, Octstr *data, char position){

       static WTPSegment *first = NULL;
       WTPSegment *previous = NULL,
                  *next = NULL,
                  *segments_list = NULL;

       debug (0, "WTP: Adding a segment into the segments list");

       segments_list = create_segment();
       segments_list->tid = tid;
       segments_list->packet_sequence_number = position;
       segments_list->data = data;

       if (first == NULL){
          first = segments_list;
          return first;

       } else {
          previous = find_previous_segment(tid, position, first, next);
          return insert_segment(previous, next, segments_list);
       }

       panic(0, "Following return is not necessary but is required by the 
             compiler");
       return NULL;      
}

static int first_segment(WTPEvent *event){

       int segmented = 0;

       if (event->RcvInvoke.user_data == NULL)
          segmented = 1;
       else
          segmented = 0;

       return segmented;
}

static Octstr *concatenate_message(long tid, WTPSegment *segments_list){

       debug(0, "WTP: concatenation not yet supported");
       
       return NULL;
}

/*
 * We must swap the source and the destination address, because we are sending an
 * acknowledgement to a received message.
 */
static Address *deduce_segment_ack_address(Msg *msg){

       Address *address = NULL;

       address = gw_malloc(sizeof(Address));
       address->source_address = 
                octstr_duplicate(msg->wdp_datagram.destination_address);
       address->source_port = msg->wdp_datagram.destination_port;
       address->destination_address = 
                octstr_duplicate(msg->wdp_datagram.source_address);
       address->destination_port = msg->wdp_datagram.source_port;

       return NULL;
}

static WTPSegment *find_previous_segment(long tid, char packet_sequence_number,
       WTPSegment *first, WTPSegment *next){

       WTPSegment *previous = NULL,
                  *current = NULL;

       current = first;
       previous = first;

       while (current->tid < tid){
	     while (current->packet_sequence_number < packet_sequence_number){
                   previous = current;
                   if (current->next != NULL)
                       current = current->next;
             }
       }

       if (current == NULL)
          debug(0, "WTP: find_previous_segment: tid not found from the segments 
                    list");
       else
          if (current->next == NULL)
             next = current;
          else 
             next = current->next;

       return previous;
}

static WTPSegment *insert_segment(WTPSegment *previous, WTPSegment *next, 
       WTPSegment *this_segment){

       previous->next = this_segment;
       this_segment->next = next;

       return this_segment;
}

static int list_missing_segments(WTPSegment *segments_ackd, 
       WTPSegment *segments_list, WTPSegment *missing_segments){

       int segments_missing = 0;

       return segments_missing;
}

/*
 * Makes a list of missing segments based on negative ack PDU.
 */
static WTPSegment *make_missing_segments_list(Msg *msg, 
       char number_of_missing_packets){

       WTPSegment *missing_segments = NULL;

       return missing_segments;
}

/*
 * Really removes a WTP state machine. Used only by the garbage collection. 
 */
static void destroy_machine(WTPMachine *machine, WTPMachine *previous){

     if (machine == previous) {
        machines.first = machine->next;
     } else {
        previous->next = machine->next;
     }

     #define INTEGER(name)
     #define ENUM(name)  
     #define MSG(name) msg_destroy(machine->name)     
     #define OCTSTR(name) octstr_destroy(machine->name)
     #define TIMER(name) wtp_timer_destroy(machine->name)
     #define QUEUE(name) if (machine->name != NULL) \
             panic(0, "WTP: machine_destroy: Event queue was not empty")
     #define MUTEX(name) mutex_destroy(machine->name)
     #define NEXT(name)
     #define MACHINE(field) field
     #include "wtp_machine-decl.h"

     gw_free(machine);

     return;
}

/**********************************************************************************/










