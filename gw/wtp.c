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

/*
 * Possible errors in incoming messages.
 */
enum {
    no_datagram,
    wrong_version,
    illegal_header,
    no_segmentation,
    pdu_too_short_error,
    no_concatenation
};

/*
 * Protocol version (currently, there is only one)
 */
enum {
   CURRENT = 0x00
};

/*
 * Abort types (i.e., provider abort codes defined by WAP)
 */
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
 * Message segmentation data. Position of a segment in a message, if the message 
 * is segmented, otherwise indication of this being a single message.
 */
enum {
   body_segment,
   group_trailer_segment,
   transmission_trailer_segment,
   single_message
};

/*
 * Global data structuresc:
 *
 * wtp machines list
 */

static List *machines = NULL;

/*
 * Global WTP transaction identifier and its lock (this is used by WSP when it 
 * wants to start a new transaction)
 */

static unsigned long wtp_tid = 0;

Mutex *wtp_tid_lock = NULL;

/*
 * Data structure for handling reassembly, containing, for instance, various segment lists.
 * See field comments for details.
 */

struct Segments {

       WTPSegment *list;             /* List of segments received */
       WTPSegment *ackd;             /* List of segments acknowledged */
       WTPSegment *missing;          /* Missing segments list */
       WTPSegment *first;            /* pointer to first of the segments */
       WTPEvent *event;              /* event perhaps containing a segment (instead of a 
                                        complete message*/
       int negative_ack_sent;
       Mutex *lock;                  /* Lock for serialising reassembly operations */
};

typedef struct Segments Segments;

static Segments *segments = NULL;

/*****************************************************************************
 *
 * Prototypes of internal functions:
 *
 * Create an uniniatilized wtp state machine.
 */

static WTPMachine *wtp_machine_create_empty(void);
static void wtp_machine_destroy(WTPMachine *sm);

/*
 * Functions for handling segments
 *
 * Both segment lists  data structure
 */

static Segments *segment_lists_create_empty(void);
#ifdef todo
static void segment_lists_dump(Segments *segments);
#endif
static void segment_lists_destroy(Segments *segments);

/*
 * and segments
 */
static WTPSegment *create_segment(void);

#if 0
static void segment_dump(WTPSegment *segment);
#endif

static void segment_destroy(WTPSegment *segment);

static WTPSegment *find_previous_segment(long tid, unsigned char sequence_number,
       WTPSegment *first, WTPSegment *next);

static WTPSegment *insert_segment(WTPSegment *previous, WTPSegment *next, 
       WTPSegment *segment);

static int list_missing_segments(WTPSegment *segments_ackd, 
       WTPSegment *segments_list, WTPSegment *missing_segments);

static WTPSegment *make_missing_segments_list(Msg *msg, 
                  unsigned char number_of_missing);
/*
 * Print a wtp event or a wtp machine state name as a string.
 */

static unsigned char *name_event(int name);

static unsigned char *name_state(int name);

static WTPEvent *duplicate_event(WTPEvent *event);

/*
 * Really removes a WTP state machine. Used only by the garbage collection. 
 */
#if 0
static void destroy_machine(WTPMachine *machine, WTPMachine *previous);
#endif

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

static void append_to_event_queue(WTPMachine *machine, WTPEvent *event);
 
static WTPEvent *remove_from_event_queue(WTPMachine *machine);

static long deduce_tid(Msg *msg);
#ifdef todo
static int message_header_fixed(unsigned char octet);
#endif
static unsigned char deduce_pdu_type(unsigned char octet);

static int message_type(unsigned char octet);

static int protocol_version(unsigned char octet);

static WTPEvent *unpack_ack(long tid, unsigned char octet);

static WTPEvent *unpack_abort(Msg *msg, long tid, unsigned char first_octet, 
                              unsigned char fourth_octet);

static WTPEvent *unpack_invoke(Msg *msg, WTPSegment *segment, long tid, 
       unsigned char first_octet, unsigned char fourth_octet);

static Octstr *unpack_segmented_invoke(Msg *msg, WTPSegment *segment, long tid, 
       unsigned char first_octet, unsigned char fourth_octet);

static WTPSegment *unpack_negative_ack(Msg *msg, unsigned char octet);

static WTPEvent *tell_about_error(int type, WTPEvent *event, Msg *msg, long tid);
#ifdef todo
static int tpi_short(unsigned char octet);
#endif
static WTPEvent *unpack_invoke_flags(WTPEvent *event, Msg *msg, long tid, 
       unsigned char first_octet, unsigned char fourth_octet);

static WTPSegment *add_segment_to_message(long tid, Octstr *data, unsigned 
                                          char position);

static int first_segment(WTPEvent *event);

static Octstr *concatenate_message(long tid, WTPSegment *segments_list);

static Address *deduce_reply_address(Msg *msg);

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
	#define OCTSTR(name) p->name = NULL
	#define EVENT(type, field) { struct type *p = &event->type; field } 
	#include "wtp_events-decl.h"
        return event;
}

/*
 * Note: We must use p everywhere (including events having only integer 
 * fields), otherwise we get a compiler warning. 
 */
void wtp_event_destroy(WTPEvent *event) {

	if (event != NULL) {
	   #define INTEGER(name) p->name = 0
	   #define OCTSTR(name) octstr_destroy(p->name)
	   #define EVENT(type, field) { struct type *p = &event->type; field }
	   #include "wtp_events-decl.h"
        }

	gw_free(event);
}

void wtp_event_dump(WTPEvent *event) {

  	debug("wap.wtp", 0, "WTPEvent %p:", (void *) event); 
	debug("wap.wtp", 0, "  type = %s", name_event(event->type));
	#define INTEGER(name) debug("wap.wtp", 0, "  %s.%s: %ld", t, #name, p->name)
	#define OCTSTR(name) \
		debug("wap.wtp", 0, "  %s.%s:", t, #name); \
		octstr_dump(p->name, 1)
	#define EVENT(tt, field) \
		if (tt == event->type) \
			{ char *t = #tt; struct tt *p = &event->tt; field } 
	#include "wtp_events-decl.h"
  	debug("wap.wtp", 0, "WTPEvent %p ends.", (void *) event); 
}

/*
 * Mark a WTP state machine unused. Normal functions do not remove machines, just 
 * set a flag. In addition, destroys the timer.
 */
void wtp_machine_mark_unused(WTPMachine *machine){

     machine->in_use = 0;
     wtp_timer_destroy(machine->timer);
     machine->timer = NULL;
}

/* 
 * Removes from the machines list all machines having in_use-flag cleared. Does nothing,
 * if machines list is empty, nor if machines list is busy (garbage collection will 
 * eventually start again).
 */
void wtp_machines_list_clear(void){
#if 0
        long remove_pat;

        remove_pat = 0;

        if (list_len(machines) == 0){
           info(0, "WTP: machines_list_clear: list is empty");
           return;
        }

        list_delete_all(machines, remove_pat, machine_not_in_use);
#endif
} 

/*
 * Write state machine fields, using debug function from a project library 
 * wapitlib.c.
 */
void wtp_machine_dump(WTPMachine *machine){
 
       if (machine != NULL){

           debug("wap.wtp", 0, "WTPMachine %p: dump starting", (void *) machine); 
	   #define INTEGER(name) \
	           debug("wap.wtp", 0, "  %s: %ld", #name, machine->name)
           #define MSG(name) \
                   debug("wap.wtp", 0, "Field %s: ", #name); \
                   msg_dump(machine->name, 1)
           #define WSP_EVENT(name) \
                   debug("wap.wtp", 0, "WSP event %s:", #name); \
                   wsp_event_dump(machine->name)
           #define ENUM(name) debug("wap.wtp", 0, "  state = %s.", name_state(machine->name))
	   #define OCTSTR(name)  \
	   	debug("wap.wtp", 0, "  Octstr field %s :", #name); \
                octstr_dump(machine->name, 1)
           #define TIMER(name)   debug("wap.wtp", 0, "  Machine timer %p:", (void *) \
                                       machine->name)
           #define MUTEX(name)   if (mutex_try_lock(machine->name) == -1) \
                                    debug("wap.wtp", 0, "%s locked", #name);\
                                 else {\
                                    debug("wap.wtp", 0, "%s unlocked", #name);\
                                    mutex_unlock(machine->name);\
                                 }
           #define NEXT(name) 
	   #define MACHINE(field) field
	   #define LIST(name) \
	           debug("wap.wtp", 0, "  %s %s", #name, \
		   machine->name ? "non-NULL" : "NULL")
	   #include "wtp_machine-decl.h"
           debug("wap.wtp", 0, "WTPMachine dump ends");
	
	} else {
           debug("wap.wtp", 0, "WTP: dump: machine does not exist");
        }
}


WTPMachine *wtp_machine_find_or_create(Msg *msg, WTPEvent *event){

          WTPMachine *machine = NULL;
          long tid;

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

	          case RcvErrorPDU:
                       tid = event->RcvErrorPDU.tid;
                  break;

                  default:
                       debug("wap.wtp", 0, "WTP: machine_find_or_create: unhandled event"); 
                       wtp_event_dump(event);
                       return NULL;
                  break;
	   }

           machine = wtp_machine_find(msg->wdp_datagram.source_address,
                     msg->wdp_datagram.source_port, 
                     msg->wdp_datagram.destination_address,
                     msg->wdp_datagram.destination_port, tid);
           
           if (machine == NULL){

              switch (event->type){
/*
 * When PDU with an illegal header is received, its tcl-field is irrelevant (and possibly 
 * meaningless).
 */
	              case RcvInvoke: case RcvErrorPDU:
	                   machine = wtp_machine_create(
                                     msg->wdp_datagram.source_address,
				     msg->wdp_datagram.source_port, 
				     msg->wdp_datagram.destination_address,
				     msg->wdp_datagram.destination_port,
				     tid, event->RcvInvoke.tcl);
                           machine->in_use = 1;
                      break;

	              case RcvAck: 
			   info(0, "WTP: machine_find_or_create: ack received, yet having no machine");
                      break;

                      case RcvAbort: 
			   info(0, "WTP: machine_find_or_create: abort received, yet having no machine");
                      break;
                 
	              default:
                           debug("wap.wtp", 0, "WTP: machine_find_or_create: unhandled event");
                           wtp_event_dump(event);
                           return NULL;
                      break;
              }
	   }
           
           return machine;
}

/*
 * Transfers data from fields of a message to fields of WTP event. User data has
 * the host byte order. Updates the log and sends protocol error messages. Reassembles 
 * segmented messages, too.
 *
 * First empty instance of segment_lists is created by wapbox.c. This function allocates 
 * and deallocates memory for its member lists. After deallocation a new instance of an 
 * empty segments data structure is creted. For result, an wtp event is created, if 
 * appropiate. The memory for this data structure is deallocated either by this module, if 
 * its data is added to a message to be reassembled, or by wtp_handle_event.
 *
 * Return event, when we have a single message or have reassembled whole the message; NULL,  * when we have a segment inside of a segmented message.
 */
WTPEvent *wtp_unpack_wdp_datagram(Msg *msg){

         WTPEvent *event = NULL;

         unsigned char first_octet,
                  pdu_type;
         int fourth_octet;              /* if error, -1 is stored into this variable */
 
         long tid = 0;
         
         tid = deduce_tid(msg);

         if (octstr_len(msg->wdp_datagram.user_data) < 3){
            event = tell_about_error(pdu_too_short_error, event, msg, tid);
            debug("wap.wtp", 0, "Got too short PDU (less than three octets)");
            msg_dump(msg, 0);
            return event;
         }

         first_octet = octstr_get_char(msg->wdp_datagram.user_data, 0);
         pdu_type = deduce_pdu_type(first_octet);

         switch (pdu_type){
/*
 * Message type cannot be result, because we are a server.
 */
                case ERRONEOUS: case RESULT: case SEGMENTED_RESULT:
                     event = tell_about_error(illegal_header, event, msg, tid);
                     return event;
                break;
/*
 * "Not allowed" means (when specification language is applied) concatenated PDUs.
 */
                case NOT_ALLOWED:
                     event = tell_about_error(no_concatenation, event, msg, tid);
                     return event;
                break;
/*
 * Invoke PDU is used by first segment of a segmented message, too. 
 */       
	       case INVOKE:
                     fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 3);

                     if (fourth_octet == -1){
                         event = tell_about_error(pdu_too_short_error, event, msg, tid);
                         debug("wap.wtp", 0, "WTP: unpack_datagram; missing fourth octet (invoke)");
                         msg_dump(msg, 0);
                         return event;
                     }
                     
                     mutex_lock(segments->lock);
                     event = unpack_invoke(msg, segments->list, tid, first_octet, 
                                           fourth_octet);

                     if (first_segment(event)) {
			gw_assert(segments->event == NULL);
                        segments->event = duplicate_event(event);
                        wtp_event_destroy(event);
                        mutex_unlock(segments->lock);
                        return NULL;

                     } else {
                        mutex_unlock(segments->lock);
                        return event;
                     }   
               break;

               case ACK:
		    return unpack_ack(tid, first_octet);   
               break;

	       case ABORT:

                    fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 3);

                    if (fourth_octet == -1){
                       event = tell_about_error(pdu_too_short_error, event, msg, tid);
                       debug("wap.wtp", 0, "WTP: unpack_datagram; missing fourth octet (abort)");
                       msg_dump(msg, 0);
                       return event;
                    }

                    return unpack_abort(msg, tid, first_octet, fourth_octet);
               break;

               case SEGMENTED_INVOKE:
                    fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 3);

                    if (fourth_octet == -1){
                       event = tell_about_error(pdu_too_short_error, event, msg, tid);
                       return event;
                    }
                    
                    mutex_lock(segments->lock);
                    segments->event->RcvInvoke.user_data = unpack_segmented_invoke(
                              msg, segments->list, tid, first_octet, fourth_octet);

                    if (message_type(first_octet) == transmission_trailer_segment){
                       event = duplicate_event(segments->event);
                       mutex_unlock(segments->lock);
                       segment_lists_destroy(segments);
                       segments = segment_lists_create_empty();
                       return event;

                    } else {
                       mutex_unlock(segments->lock);
                       return NULL;
                    }
              break;
                  
              case NEGATIVE_ACK:
                   fourth_octet = octstr_get_char(msg->wdp_datagram.user_data, 3);

                   if (fourth_octet == -1){
                      event = tell_about_error(pdu_too_short_error, event, msg, tid);
                      return event;
                   }

                   mutex_lock(segments->lock);
                   segments->missing = unpack_negative_ack(msg, fourth_octet);
                   mutex_unlock(segments->lock);
                   return NULL;
              break;
         } /* switch */
/* Following return is unnecessary but required by the compiler */
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
     WTPEvent *timer_event = NULL;

/* 
 * If we're already handling events for this machine, add the event to the 
 * queue.
 */
     if (mutex_try_lock(machine->mutex) == -1) {
	append_to_event_queue(machine, event);
	return;
     }

     do {
	  debug("wap.wtp", 0, "WTP: machine %p, state %s, event %s.", 
	  	(void *) machine, 
		name_state(machine->state), 
		name_event(event->type));

	  #define STATE_NAME(state)
	  #define ROW(wtp_state, event_type, condition, action, next_state) \
		  if (machine->state == wtp_state && \
		     event->type == event_type && \
		     (condition)) { \
		     action \
                     machine->state = next_state; \
		  } else 
	  #include "wtp_state-decl.h"
		  {
		     error(0, "WTP: handle_event: unhandled event!");
		     debug("wap.wtp", 0, "WTP: handle_event: Unhandled event was:");
		     wtp_event_dump(event);
                     return;
		  }

	  if (event != NULL) {
	     wtp_event_destroy(event);  
          }

          event = remove_from_event_queue(machine);
     } while (event != NULL);
     
     mutex_unlock(machine->mutex);
 
     return;
}

unsigned long wtp_tid_next(void){
     
     mutex_lock(wtp_tid_lock);
     ++wtp_tid;
     mutex_unlock(wtp_tid_lock);

     return wtp_tid;
} 


void wtp_init(void) {
     machines = list_create();
     wtp_tid_lock = mutex_create();
     segments = segment_lists_create_empty();
}

void wtp_shutdown(void) {
     while (list_len(machines) > 0)
	wtp_machine_destroy(list_extract_first(machines));
     list_destroy(machines);
     segment_lists_destroy(segments);
     mutex_destroy(wtp_tid_lock);
}

/*****************************************************************************
 *
 * INTERNAL FUNCTIONS:
 *
 * Give the name of an event in a readable form. 
 */

static unsigned char *name_event(int s){

       switch (s){
              #define EVENT(type, field) case type: return #type;
              #include "wtp_events-decl.h"
              default:
                      return "unknown event";
       }
 }


static unsigned char *name_state(int s){

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

struct machine_pattern {
	Octstr *source_address;
	long source_port;
	Octstr *destination_address;
	long destination_port;
	long tid;
};

static int is_wanted_machine(void *a, void *b) {
	struct machine_pattern *pat;
	WTPMachine *m;
	
	m = a;
	pat = b;

	return octstr_compare(m->source_address, pat->source_address) == 0 &&
               m->source_port == pat->source_port && 
               octstr_compare(m->destination_address, 
	                      pat->destination_address) == 0 &&
               m->destination_port == pat->destination_port &&
	       m->tid == pat->tid && 
	       m->in_use == 1;
}

static WTPMachine *wtp_machine_find(Octstr *source_address, long source_port,
       Octstr *destination_address, long destination_port, long tid){
	struct machine_pattern pat;
	WTPMachine *m;
	
	pat.source_address = source_address;
	pat.source_port = source_port;
	pat.destination_address = destination_address;
	pat.destination_port = destination_port;
	pat.tid = tid;
	
	m = list_search(machines, &pat, is_wanted_machine);
#if 0
	debug("wap.wtp", 0, "WTP: wtp_machine_find: %p", (void *) m);
#endif
	return m;
}

/*
 * Iniatilizes wtp machine and adds it to machines list. 
 */
static WTPMachine *wtp_machine_create_empty(void){
       WTPMachine *machine = NULL;

        machine = gw_malloc(sizeof(WTPMachine));
        
        #define INTEGER(name) machine->name = 0
        #define ENUM(name) machine->name = LISTEN
        #define MSG(name) machine->name = msg_create(wdp_datagram)
        #define OCTSTR(name) machine->name = NULL
        #define WSP_EVENT(name) machine->name = NULL
        #define MUTEX(name) machine->name = mutex_create()
        #define TIMER(name) machine->name = wtp_timer_create()
        #define NEXT(name) machine->name = NULL
        #define MACHINE(field) field
	#define LIST(name) machine->name = list_create()
        #include "wtp_machine-decl.h"

	list_append(machines, machine);

        return machine;
}

/*
 * Destroys a WTPMachine. Assumes it is safe to do so. Assumes it has already
 * been deleted from the machines list.
 */
static void wtp_machine_destroy(WTPMachine *machine){
        #define INTEGER(name) machine->name = 0
        #define ENUM(name) machine->name = LISTEN
        #define MSG(name) msg_destroy(machine->name)
        #define OCTSTR(name) octstr_destroy(machine->name)
        #define WSP_EVENT(name) machine->name = NULL
        #define MUTEX(name) mutex_destroy(machine->name)
        #define TIMER(name) wtp_timer_destroy(machine->name)
        #define NEXT(name) machine->name = NULL
        #define MACHINE(field) field
	#define LIST(name) list_destroy(machine->name)
        #include "wtp_machine-decl.h"
	gw_free(machine);
}

/*
 * Create a new WTPMachine for a given transaction, identified by the five-tuple 
 * in the arguments. In addition, update the transaction class field of the 
 * machine. If machines list is busy, just wait.
 */
WTPMachine *wtp_machine_create(Octstr *source_address, 
           long source_port, Octstr *destination_address, 
           long destination_port, long tid, long tcl) {

	   WTPMachine *machine = NULL;
	   
           machine = wtp_machine_create_empty();

           machine->source_address = octstr_duplicate(source_address);
           machine->source_port = source_port;
           machine->destination_address = octstr_duplicate(destination_address);
           machine->destination_port = destination_port;
           machine->tid = tid;
           machine->tcl = tcl;

           return machine;
} 

static Segments *segment_lists_create_empty(void){

       Segments *segments = NULL;

       segments = gw_malloc(sizeof(Segments));

       segments->list = create_segment();   
       segments->ackd = create_segment();    
       segments->missing = create_segment(); 
       segments->first = create_segment();            
       segments->event = NULL;                      /* there is no empty event */
       segments->negative_ack_sent = 0;
       segments->lock = mutex_create();                  

       return segments;
}
#ifdef todo
static void segment_lists_dump(Segments *segments){

       debug("wap.wtp", 0, "segments list at %p", (void *) segments->list);
       debug("wap.wtp", 0, "ackd segments list at %p", (void *) segments->ackd); 
       debug("wap.wtp", 0, "missing segments list at %p", (void *) segments->missing); 
       debug("wap.wtp", 0, "first segment was");
       segment_dump(segments->first);         
       debug("wap.wtp", 0, "event to be added");
       wtp_event_dump(segments->event);                   
       debug("wap.wtp", 0, "have we send a negative acknowledgement %d", segments->negative_ack_sent); 
       if (mutex_try_lock(segments->lock) == -1)
           debug("wap.wtp", 0, "segments list locked");
       else {
           debug("wap.wtp", 0, "segments list unlocked"); 
           mutex_unlock(segments->lock);
       }
}
#endif
static void segment_lists_destroy(Segments *segments){

       segment_destroy(segments->list);   
       segment_destroy(segments->ackd);    
       segment_destroy(segments->missing); 
       segment_destroy(segments->first);            
       wtp_event_destroy(segments->event);
       mutex_destroy(segments->lock);   
       gw_free(segments);   
}

static WTPSegment *create_segment(void){

       WTPSegment *segment = NULL;

       segment = gw_malloc(sizeof(WTPSegment));
       segment->tid = 0;
       segment->packet_sequence_number = 0;
       segment->data = octstr_create_empty();
       
       segment->next = NULL;

       return segment;
}

#if 0
static void segment_dump(WTPSegment *segment){

       debug("wap.wtp", 0, "WTP: segment was:");
       debug("wap.wtp", 0, "tid was: %ld", segment->tid);
       debug("wap.wtp", 0, "psn was: %d", segment->packet_sequence_number);
       debug("wap.wtp", 0, "segment itself was:");
       octstr_dump(segment->data, 1);
       debug("wap.wtp", 0, "WTP: segment dump ends");
}
#endif

static void segment_destroy(WTPSegment *segment){

       octstr_destroy(segment->data);
       gw_free(segment->next);
       gw_free(segment);
}

/*
 * Packs a wsp event. Fetches flags and user data from a wtp event. Address 
 * five-tuple and tid are fields of the wtp machine.
 */
static WSPEvent *pack_wsp_event(WSPEventType wsp_name, WTPEvent *wtp_event, 
         WTPMachine *machine){

         WSPEvent *event = wsp_event_create(wsp_name);

         switch (wsp_name){
                
	        case TR_Invoke_Ind:
                     event->TR_Invoke_Ind.ack_type = machine->u_ack;
                     event->TR_Invoke_Ind.user_data =
                            octstr_duplicate(wtp_event->RcvInvoke.user_data);
                     event->TR_Invoke_Ind.tcl = wtp_event->RcvInvoke.tcl;
                     event->TR_Invoke_Ind.wsp_tid = wtp_tid_next();
                     event->TR_Invoke_Ind.machine = machine;
                break;

	        case TR_Invoke_Cnf:
                     event->TR_Invoke_Cnf.wsp_tid =
                            event->TR_Invoke_Ind.wsp_tid;
                     event->TR_Invoke_Cnf.machine = machine;
                break;
                
	        case TR_Result_Cnf:
                     event->TR_Result_Cnf.exit_info =
                            octstr_duplicate(wtp_event->RcvInvoke.exit_info);
                     event->TR_Result_Cnf.exit_info_present =
                            wtp_event->RcvInvoke.exit_info_present;
                     event->TR_Result_Cnf.wsp_tid =
                            event->TR_Invoke_Ind.wsp_tid;
                     event->TR_Result_Cnf.machine = machine;
                break;

	        case TR_Abort_Ind:
                     event->TR_Abort_Ind.abort_code =
                            wtp_event->RcvAbort.abort_reason;
                     event->TR_Abort_Ind.wsp_tid =
                            event->TR_Invoke_Ind.wsp_tid;
                     event->TR_Abort_Ind.machine = machine;
                break;
                
	        default:
                break;
         }

         return event;
} 

/*
 * Append an event to the event queue of a WTPMachine. 
 */
static void append_to_event_queue(WTPMachine *machine, WTPEvent *event) {

       list_append(machine->event_queue, event);
}


/*
 * Return the first event from the event queue of a WTPMachine, and remove
 * it from the queue, NULL if the queue was empty.
 */
static WTPEvent *remove_from_event_queue(WTPMachine *machine) {

       return list_extract_first(machine->event_queue);
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
static int message_header_fixed(unsigned char octet){

       return !(octet>>7); 
}
#endif

static unsigned char deduce_pdu_type(unsigned char octet){

       int type;

       if ((type = octet>>3&15) > 7){
          return -1;
       } else {
          return type; 
       }
}

static int message_type(unsigned char octet){

       unsigned char this_octet,
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

/* Following return is unnecessary but required by the compiler */
       return 0;
}

static int protocol_version(unsigned char octet){

       return octet>>6&3;
}

static WTPEvent *unpack_ack(long tid, unsigned char octet){

      WTPEvent *event = NULL;
      unsigned char this_octet;

      event = wtp_event_create(RcvAck);

      event->RcvAck.tid = tid;
      this_octet = octet;
      event->RcvAck.tid_ok = this_octet>>2&1;
      this_octet = octet;
      event->RcvAck.rid = this_octet&1;

      return event;
}

WTPEvent *unpack_abort(Msg *msg, long tid, unsigned char first_octet, unsigned 
                       char fourth_octet){

         WTPEvent *event = NULL;
         unsigned char abort_type;      

         event = wtp_event_create(RcvAbort);

         abort_type = first_octet&7;
/*
 * Counting of abort types starts at zero.
 */
	 if (abort_type > NUMBER_OF_ABORT_TYPES-1 || 
             fourth_octet > NUMBER_OF_ABORT_REASONS-1){
            event = tell_about_error(illegal_header, event, msg, tid);
            return event;
         }
                
         event->RcvAbort.tid = tid;  
         event->RcvAbort.abort_type = abort_type;   
         event->RcvAbort.abort_reason = fourth_octet;
         debug("wap.wtp", 0, "WTP: unpack_abort: abort event packed");
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
                        unsigned char first_octet, unsigned char fourth_octet){

         WTPEvent *event = NULL;

         if (protocol_version(fourth_octet) != CURRENT){
            event = tell_about_error(wrong_version, event, msg, tid);
            debug("wap.wtp", 0, "WTP: unpack_invoke: handling version error");
            return event;
         }

         event = wtp_event_create(RcvInvoke);
/*
 * First invoke message includes all event flags, even when we are receiving a 
 * segmented message. So we first fetch event flags, and then handle user_data 
 * differently: if message was unsegmented, we transfer all data to event; if it
 * was segmented, we begin reassembly.
 */
         event = unpack_invoke_flags(event, msg, tid, first_octet, fourth_octet);
         octstr_delete(msg->wdp_datagram.user_data, 0, 4);
 
         switch (message_type(first_octet)) {
         
	        case group_trailer_segment:
                     debug("wap.wtp", 0, "WTP: Got a segmented message");
                     msg_dump(msg, 0);
                     segments_list = add_segment_to_message(tid, 
                                     msg->wdp_datagram.user_data, 0);
                     return NULL;
	        break;

	        case  single_message:
#if 0
                      debug("wap.wtp", 0, "WTP: Got a single message");
#endif
                      event->RcvInvoke.user_data = octstr_duplicate(
                                                   msg->wdp_datagram.user_data); 
                      return event;
                break;

	        default:
                      debug("wap.wtp", 0, "WTP: Got a strange message");
                      event = tell_about_error(illegal_header, event, msg, tid);
                      return event;
                break;
         }
}

/*
 * Returns event RcvErrorPDU, when the error is an illegal header, otherwise NULL.
 */
static WTPEvent *tell_about_error(int type, WTPEvent *event, Msg *msg, long tid){

       Address *address = NULL;

       address = deduce_reply_address(msg);
       debug("wap.wtp", 0, "WTP: tell:");
       wtp_send_address_dump(address);

       switch (type){
/*
 * Sending  Abort(WTPVERSIONZERO)
 */
              case wrong_version:
                   gw_free(event);
                   wtp_do_not_start(PROVIDER, WTPVERSIONZERO, address, tid);
                   error(0, "WTP: Version not supported");
              return NULL;
/*
 * Sending  Abort(NOTIMPLEMENTEDSAR)
 */
              case no_segmentation:
                   gw_free(event);
                   wtp_do_not_start(PROVIDER, NOTIMPLEMENTEDSAR, address, tid);
                   error(0, "WTP: No segmentation implemented");
              return NULL;
/*
 * Illegal headers are events, because their handling depends on the protocol state. 
 */
             case illegal_header:
                  error(0, "WTP: Illegal header structure");
                  gw_free(event);
                  event = wtp_event_create(RcvErrorPDU);
                  event->RcvErrorPDU.tid = tid;
             return event;

             case pdu_too_short_error:
                  error(0, "WTP: PDU too short");
                  gw_free(event);
                  event = wtp_event_create(RcvErrorPDU);
                  event->RcvErrorPDU.tid = tid;
             return event;

             case no_datagram: 
                  error(0, "WTP: No datagram received");
                  gw_free(event);
                  event = wtp_event_create(RcvErrorPDU);
                  event->RcvErrorPDU.tid = tid;
             return event;

             case no_concatenation:
                  wtp_do_not_start(PROVIDER, UNKNOWN, address, tid);
                  error(0, "WTP: No concatenation supported");
                  gw_free(event);
             return NULL;
     }
/* Following return is unnecessary but required by the compiler */
     return NULL;
}

static Octstr *unpack_segmented_invoke(Msg *msg, WTPSegment *segments_list, 
       long tid, unsigned char first_octet, unsigned char fourth_octet){
       
       Octstr *event_data = NULL;
       static WTPSegment *segments_ackd = NULL;
       WTPSegment *missing_segments = NULL;
       Address *address = NULL;
       
       unsigned char packet_sequence_number = 0;
       int segments_missing = 0;

       static int negative_ack_sent = 0,
              group_ack_sent = 0;

       debug("wap.wtp", 0, "WTP: got a segmented invoke package");

       tid = deduce_tid(msg);
       packet_sequence_number = fourth_octet;
       address = deduce_reply_address(msg);

       if (message_type(first_octet) == body_segment){
          debug("wap.wtp", 0, "WTP: Got a body segment");
          msg_dump(msg, 0);
          segments_list = add_segment_to_message(tid, 
                          msg->wdp_datagram.user_data, packet_sequence_number);
          return NULL;
       }

       if (message_type(first_octet) == group_trailer_segment){
          debug("wap.wtp", 0, "WTP: Got the last segment of the group");
          msg_dump(msg, 0);
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
         debug("wap.wtp", 0, "WTP: Got last segment of a message");
         msg_dump(msg, 0);

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
/* Following return is unnecessary but is required by the compiler */
      return NULL;
}

static WTPSegment *unpack_negative_ack(Msg *msg, unsigned char fourth_octet){

       WTPSegment *missing_segments = NULL;
       unsigned char number_of_missing_packets = 0;
       
       debug("wap.wtp", 0, "WTP: got a negative ack");
       number_of_missing_packets = fourth_octet; 
       missing_segments = make_missing_segments_list(msg, 
                          number_of_missing_packets);      

       return missing_segments;
}

#ifdef next
static int tpi_short(unsigned char octet){
       
       return octet>>2&1;
}
#endif

static WTPEvent *unpack_invoke_flags(WTPEvent *event, Msg *msg, long tid, 
                unsigned char first_octet, unsigned char fourth_octet){

         unsigned char this_octet,
                       tcl;

         this_octet = fourth_octet;

         tcl = this_octet&3; 
         if (tcl > NUMBER_OF_TRANSACTION_CLASSES-1){
/* XXX this is a potential memory leak, yes? --liw */
            event = tell_about_error(illegal_header, event, msg, tid);
            return event;
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
static WTPSegment *add_segment_to_message(long tid, Octstr *data, unsigned char 
                                          position){

/* XXX static variable? probably not thread safe --liw */
       static WTPSegment *first = NULL;
       WTPSegment *previous = NULL,
                  *next = NULL,
                  *segments_list = NULL;

       debug("wap.wtp", 0, "WTP: Adding a segment into the segments list");

       segments_list = create_segment();
       segments_list->tid = tid;
       segments_list->packet_sequence_number = position;
       octstr_destroy(segments_list->data);
       segments_list->data = data;

       if (first == NULL){
          first = segments_list;
          return first;

       } else {
          previous = find_previous_segment(tid, position, first, next);
          return insert_segment(previous, next, segments_list);
       }

/* Following return is not necessary but is required by the compiler */
       return NULL;      
}

/*
 * If there is no data yet collected at userdata field of WTPEvent, we have the first
 * segment. Of course, this not a general function for deciding whether some segment 
 * of a message is the first one. 
 */
static int first_segment(WTPEvent *event){

       int first_segment = 0;

       if (event->RcvInvoke.user_data == NULL)
          first_segment = 1;
       else
          first_segment = 0;

       return first_segment;
}

static Octstr *concatenate_message(long tid, WTPSegment *segments_list){

       Octstr *message = NULL;

       debug("wap.wtp", 0, "WTP: concatenation not yet supported");
       gw_assert(segments_list != NULL);
       
       return message;
}

/*
 * We must swap the source and the destination address, because we are sending a
 * reply to a received message.
 */
static Address *deduce_reply_address(Msg *msg){

       Address *address = NULL;

       address = gw_malloc(sizeof(Address));
       address->source_address = 
                octstr_duplicate(msg->wdp_datagram.destination_address);
       address->source_port = msg->wdp_datagram.destination_port;
       address->destination_address = 
                octstr_duplicate(msg->wdp_datagram.source_address);
       address->destination_port = msg->wdp_datagram.source_port;

       return address;
}

static WTPSegment *find_previous_segment(long tid, unsigned char packet_sequence_number,
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
          debug("wap.wtp", 0, "WTP: find_previous_segment: tid not found from the segments list");
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

       gw_assert(segments_ackd != NULL);
       gw_assert(segments_list != NULL);
       gw_assert(missing_segments != NULL);

       return segments_missing;
}

/*
 * Makes a list of missing segments based on negative ack PDU.
 */
static WTPSegment *make_missing_segments_list(Msg *msg, 
       unsigned char number_of_missing_packets){

       WTPSegment *missing_segments = NULL;

       gw_assert(msg != NULL);
       gw_assert(number_of_missing_packets != 0);

       return missing_segments;
}

/*
 * Duplicates an event data structure, allocating the required memory.
 *
 * Returns an event when successfull, NULL otherwise.
 */
static WTPEvent *duplicate_event(WTPEvent *original){

       WTPEvent *copy = NULL;

       copy = wtp_event_create(original->type);
       if (copy == NULL)
          return NULL;

       #define INTEGER(name) p->name = q->name
       #define OCTSTR(name) \
               if (q->name == NULL) p->name = NULL; \
               else p->name = octstr_copy(q->name, 0, octstr_len(q->name))
       #define EVENT(type, field) { \
                    struct type *p = &copy->type; \
                    struct type *q = &original->type; \
                    field }
       #include "wtp_events-decl.h"
       return copy;
}
#if 0
static int machine_not_in_use(void *a, void *b){

	WTPMachine *machine;
        long remove_pat;
	
	machine = a;
        remove_pat = b;
        return machine.in_use == remove_pat;
}

/*
 * Really removes a WTP state machine. Used only by the garbage collection. 
 */
static void destroy_machine(WTPMachine *machine, WTPMachine *previous){

     #define INTEGER(name)
     #define ENUM(name)  
     #define MSG(name) msg_destroy(machine->name)
     #define WSP_EVENT(name) wsp_event_destroy(machine->name)  
     #define OCTSTR(name) octstr_destroy(machine->name)
     #define TIMER(name) wtp_timer_destroy(machine->name)
     #define MUTEX(name) mutex_destroy(machine->name)
     #define NEXT(name)
     #define MACHINE(field) field
     #define LIST(name) if (machine->name != NULL) \
             error(0, "WTP: machine_destroy: Event queue was not empty")
     #include "wtp_machine-decl.h"

     gw_free(machine);

     return;
}
#endif
/**********************************************************************************/
