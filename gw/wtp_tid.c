/*
 * wtp_tid.c - Implementation of WTP tid validation tests
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp_tid.h"

/*
 * Global data structure:
 *
 * Tid cache: an unordered linked list storing last tid for every iniator
 */

struct WTPCached_tid {
       Octstr *source_address;
       long source_port;
       Octstr *destination_address;
       long destination_port;
       long tid;
       struct WTPCached_tid *next;
};

typedef struct WTPCached_tid WTPCached_tid;

struct Tid_cache {
       WTPCached_tid *first;
       WTPCached_tid *this; /* pointer to the present cache item */
       WTPCached_tid *item; /* pointer to the last cache item */
       Mutex *lock;
};

typedef struct Tid_Cache Tid_Cache;

static Tid_cache tid_cache =
{
       NULL,
       NULL,
       NULL,
       NULL,
};

/*
 * Prototypes of internal functions
 */
static WTPCached_tid *cache_item_create_empty(void);
/*
static void cache_item_destroy(WTPCached_tid *item);

static void cache_item_dump(WTPCached_tid *item);
*/
static int last_tid_exists(WTPMachine *machine);

static void set_tid(long tid);

static void add_tid(WTPMachine *machine, long tid);

static int tid_in_window(long rcv_tid, long last_tid);

/*
 * External functions:
 */

void wtp_tid_cache_init(void) {
     tid_cache.lock = mutex_create();
}

/*
 * Tid verification is invoked, when tid_new flag of the incoming message is 
 * on. It is not, if the iniator is not yet cached. If iniator is cached, the
 * recived tid is stoed.
 */
 
int wtp_tid_is_valid(WTPEvent *event, WTPMachine *machine){

    long rcv_tid = -1,
         last_tid = -1;

    debug("wap.wtp.tid", 0, "starting validation");
    rcv_tid = machine->tid;
   
    if (event->RcvInvoke.tid_new == 0) {
       
       if ((last_tid = last_tid_exists(machine)) == no_cache) {
          if (event->RcvInvoke.no_cache_supported == 1)
             return no_cached_tid;
          else {
             debug("wap.wtp.tid", 0, "empty cache");    
	     add_tid(machine, rcv_tid);
             return ok;
         }
      }
      
      if (tid_in_window(rcv_tid, last_tid) == 0){
         debug("wap.wtp.tid", 0, "tid out of the window");
         return fail;
      } else {
         debug("wap.wtp.tid", 0, "tid in the window");
         set_tid(rcv_tid);
         return ok;
      }

    } else {
      info(0, "WTP_TID: tid_new flag on");
      rcv_tid = 0;

      if (tid_cache.item == NULL) {
         add_tid(machine, rcv_tid);
      } else {
         set_tid(rcv_tid);
      }
     
      return fail;
    }
/*
 * This return is unnecessary but our compiler demands it
 */
    return fail;
}

/*
 * Internal functions:
 *
 * Checks whether the received tid is inside the window of acceptable ones. The size 
 * of the window is a half of the tid space. 
 *
 * Inputs: stored tid, received tid. Output 0, if received tid is outside the window,
 * 1, if it is inside.
 */
static int tid_in_window(long rcv_tid, long last_tid){

       debug("wap.wtp.tid", 0, "tids were %ld and %ld", rcv_tid, last_tid); 
       if (last_tid == rcv_tid) {
          return 0;
       } 

       if (rcv_tid > last_tid) {
	  if (abs(rcv_tid - last_tid) <= window_size) {
             return 1;
          } else {
             return 0;
          }
       }
       
       if (rcv_tid < last_tid) {
	  if (abs(rcv_tid - last_tid) >= window_size){
             return 1;
          } else {
             return 0;
          }
       }

/*
 * Following return is unnecessary but our compiler demands it");
 */
       return 0;
}

static WTPCached_tid *cache_item_create_empty(void){

       WTPCached_tid *item = NULL;

       item = gw_malloc(sizeof(WTPCached_tid));

       item->source_address = octstr_create_empty(); 
       item->source_port = 0;
       item->destination_address = octstr_create_empty();
       item->destination_port = 0;
       item->tid = 0;

       return item;
}
#ifdef next
static void cache_item_destroy(WTPCached_tid *item){

       octstr_destroy(item->destination_address);
       octstr_destroy(item->source_address);
       gw_free(item);
}

static void cache_item_dump(WTPCached_tid *item){

       debug("wap.wtp.tid", 0, "WTP_TID: dumping of a cache item starts");
       debug("wap.wtp.tid", 0, "source address");
       octstr_dump(item->source_address);
       debug("wap.wtp.tid", 0, "source port %ld", item->source_port);
       debug (0, "destination address");
       octstr_dump(item->destination_address);
       debug("wap.wtp.tid", 0, "destination port %ld", item->destination_port);
}
#endif
/*
 * Checks if there is any tid stored for a spesific iniator. Receives address 
 * quadruplet - the identifier it uses - from object WTPMachine. Ditto tid.
 * Returns the cached tid or no_cache, if there is not one.
 */
static int tid_cached(WTPMachine *machine){

       WTPCached_tid *this_item = NULL;
       
       mutex_lock(tid_cache.lock);

       if (tid_cache.item == NULL){
          mutex_unlock(tid_cache.lock);
          return no_cache;
       }   

       this_item = tid_cache.first;
       
       while (this_item != NULL){

             if ((octstr_compare(this_item->source_address, 
                         machine->source_address) == 0) &&
                 this_item->source_port == machine->source_port && 
                 (octstr_compare(this_item->destination_address,
                         machine->destination_address) == 0) &&
                 this_item->destination_port == 
		 machine->destination_port){
               
               mutex_unlock(tid_cache.lock);
               return this_item->tid;

	     } else {
               this_item = this_item->next;
             }
       }

       mutex_unlock(tid_cache.lock);
       return no_cache;
}

static void set_tid(long tid){

       mutex_lock(tid_cache.lock);
       tid_cache.this->tid = tid; 
       mutex_unlock(tid_cache.lock);
}

/*
 * Adds an item to the tid cache, one item per every iniator. Iniator is 
 * identified by the address four-tuple, fetched from wtp machine.
 */ 
static void add_tid(WTPMachine *machine, long tid){

       WTPCached_tid *new_item = NULL;
       
       new_item = cache_item_create_empty(); 
       new_item->source_address = octstr_duplicate(machine->source_address);
       new_item->source_port = machine->source_port;
       new_item->destination_address = 
                 octstr_duplicate(machine->destination_address);
       new_item->destination_port = machine->destination_port;
       new_item->tid = tid; 

       mutex_lock(tid_cache.lock);

       if (tid_cache.item == NULL) {
           tid_cache.first = new_item;
           tid_cache.item = tid_cache.first;
           tid_cache.this = tid_cache.first;
       } else {
           tid_cache.item->next = new_item;
           tid_cache.item = new_item;
           tid_cache.this = tid_cache.item;
       }
       
       mutex_unlock(tid_cache.lock); 
}

static int last_tid_exists(WTPMachine *machine){

       int cached_tid = -1;

       if ((cached_tid = tid_cached(machine)) >= 0)
          return cached_tid;
       else
	  return no_cache;
}













