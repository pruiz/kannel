/*
 * wtp_tid.c - Implementation of WTP tid validation tests
 *
 * By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp_tid.h"

/*
 * Constants used for defining the tid cache status
 */
enum {
     no_cache = -1,
     iniatilised = -2,
     not_iniatilised = -3,
     cached = 0
};

/*
 * Global data structure:
 *
 * Tid cache is implemented by using a library object List
 */
    static List *tid_cache = NULL;   

/*
 * Prototypes of internal functions
 */
static WTPCached_tid *cache_item_create_empty(void);

static void cache_item_destroy(WTPCached_tid *item);
/*
static void cache_item_dump(WTPCached_tid *item);
*/

static void add_tid(WTPMachine *machine, long tid);

static void set_tid_by_item(WTPCached_tid *item, long tid);

static int tid_in_window(long rcv_tid, long last_tid);

static WTPCached_tid *tid_cached(WTPMachine *machine);

/*
 * External functions:
 */

void wtp_tid_cache_init(void) {

     tid_cache = list_create();
}

void wtp_tid_cache_shutdown(void) {

    while (list_len(tid_cache) > 0)
          cache_item_destroy(list_extract_first(tid_cache));
    list_destroy(tid_cache);
}

/*
 * Tid verification is invoked, when tid_new flag of the incoming message is 
 * on. It is not, if the iniator is not yet cached. If iniator is cached, the
 * received tid is stored.
 */
int wtp_tid_is_valid(WAPEvent *event, WTPMachine *machine){

    long rcv_tid = -1,
         last_tid = -1;

    WTPCached_tid *item = NULL;

#if 0
    debug("wap.wtp.tid", 0, "starting validation");
#endif
    rcv_tid = machine->tid;
   
    if (!event->u.RcvInvoke.tid_new) {
/*
 * First we check whether the current iniator has a cache item for it.
 */      
       if ((item = tid_cached(machine)) == NULL) {

          if (event->u.RcvInvoke.no_cache_supported)
             return no_cached_tid;
          else {
#if 0
             debug("wap.wtp.tid", 0, "empty cache");    
#endif
	     add_tid(machine, rcv_tid);
             return ok;
         }
      }
/*
 * If it has, we check if the message is a duplicate or has tid wrapped up confusingly.
 */      
     last_tid = item->tid; 
      
     if (tid_in_window(rcv_tid, last_tid) == 0){
         info(0, "WTP_TID: tid out of the window");
         return fail;
      } else {
#if 0
         debug("wap.wtp.tid", 0, "tid in the window");
#endif
         set_tid_by_item(item, rcv_tid);
         return ok;
      }

    } else {
      info(0, "WTP_TID: tid_new flag on");
      rcv_tid = 0;

      if (item == NULL) {
         add_tid(machine, rcv_tid);
      } else {
         set_tid_by_item(item, rcv_tid);
      }
     
      return fail;
    }
/*
 * This return is unnecessary but our compiler demands it
 */
    return fail;
}

/*
 * Changes tid value used by an existing iniator. Input machine and the 
 * new tid.
 */
void wtp_tid_set_by_machine(WTPMachine *machine, long tid){
     WTPCached_tid *item = NULL;
       
     item = tid_cached(machine);

     if (item != NULL){
        list_lock(tid_cache);
        item->tid = tid;
        list_unlock(tid_cache);
     }
}

/*
 * Internal functions:
 *
 * Checks whether the received tid is inside the window of acceptable ones. The size 
 * of the window is set by the constant WTP_TID_WINDOW_SIZE (half of the tid space is
 * the recommended value). 
 *
 * Inputs: stored tid, received tid. Output 0, if received tid is outside the window,
 * 1, if it is inside.
 */
static int tid_in_window(long rcv_tid, long last_tid){

#if 1
       debug("wap.wtp.tid", 0, "tids were rcv_tid, %ld and last_tid, %ld and test window %ld", rcv_tid, last_tid, WTP_TID_WINDOW_SIZE); 
#endif
       if (last_tid == rcv_tid) {
	 return 0;
       } 

       if (rcv_tid > last_tid) {
	  if (abs(rcv_tid - last_tid) <= WTP_TID_WINDOW_SIZE) {
             return 1;
          } else {
             return 0;
          }
       }
       
       if (rcv_tid < last_tid) {
	  if (abs(rcv_tid - last_tid) >= WTP_TID_WINDOW_SIZE){
             return 1;
          } else {
             return 0;
          }
       }

/*
 * Following return is unnecessary but our compiler demands it
 */
       return 0;
}

static WTPCached_tid *cache_item_create_empty(void){

       WTPCached_tid *item = NULL;

       item = gw_malloc(sizeof(WTPCached_tid));

       item->source_address = NULL;
       item->source_port = 0;
       item->destination_address = NULL;
       item->destination_port = 0;
       item->tid = 0;
       item->next = NULL;

       return item;
}

static void cache_item_destroy(WTPCached_tid *item){

       octstr_destroy(item->destination_address);
       octstr_destroy(item->source_address);
       gw_free(item);
}

#ifdef next
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
 * Checking whether there is an item stored for a specific iniator. Receives address 
 * quadruplet - the identifier it uses - from object WTPMachine. Ditto tid.
 * Returns the item or NULL, if there is not one. Iniator is identified by the 
 * address four-tuple.
 */
struct profile {
       Octstr *source_address;
       Octstr *destination_address;
       long source_port;
       long destination_port;
};

static int tid_is_cached(void *a, void *b){

       struct profile *iniator_profile;
       WTPCached_tid *item;

       item = a;
       iniator_profile = b;

       return octstr_compare(item->source_address, 
                             iniator_profile->source_address) == 0 &&
              octstr_compare(item->destination_address,
                             iniator_profile->destination_address) == 0 &&
              item->source_port == iniator_profile->source_port &&
              item->destination_port == iniator_profile->destination_port;             
}

static WTPCached_tid *tid_cached(WTPMachine *machine){

       WTPCached_tid *item = NULL;
       struct profile iniator_profile;

       iniator_profile.source_address = machine->addr_tuple->client->address;
       iniator_profile.destination_address = 
       		machine->addr_tuple->server->address;
       iniator_profile.source_port = machine->addr_tuple->client->port;
       iniator_profile.destination_port = machine->addr_tuple->server->port;

       item = list_search(tid_cache, &iniator_profile, tid_is_cached);

       return item;
}

/*
 * Adds an item to the tid cache, one item per every iniator. Iniator is 
 * identified by the address four-tuple, fetched from wtp machine.
 */ 
static void add_tid(WTPMachine *machine, long tid){

       WTPCached_tid *new_item = NULL;
       
       new_item = cache_item_create_empty(); 
       octstr_destroy(new_item->source_address);
       new_item->source_address = 
       	octstr_duplicate(machine->addr_tuple->client->address);
       new_item->source_port = machine->addr_tuple->client->port;
       octstr_destroy(new_item->destination_address);
       new_item->destination_address = 
                 octstr_duplicate(machine->addr_tuple->server->address);
       new_item->destination_port = machine->addr_tuple->server->port;
       new_item->tid = tid; 

       list_append(tid_cache, new_item);
}

/*
 * Set tid for an existing iniator. Input a cache item and the new tid.
 */
static void set_tid_by_item(WTPCached_tid *item, long tid){

        list_lock(tid_cache);
        item->tid = tid;
        list_unlock(tid_cache);
}










